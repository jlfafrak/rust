#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define EOS_SYNC_MUTEX_MAGIC UINT32_C(0x45534d31)
#define EOS_SYNC_MUTEX_ATTR_MAGIC UINT32_C(0x45534d41)
#define EOS_SYNC_MUTEX_ATTR_DEAD UINT32_C(0x45534d44)
#define EOS_SYNC_OBJECT_DEAD UINT32_C(0xffffffff)

typedef struct eos_mutex_record {
    uint32_t identity;
    uint32_t type;
    uint32_t active_operations;
    uint32_t waiters;
    eos_rust_thread_t owner;
    uint32_t depth;
    eos_port_mutex native;
    struct eos_mutex_record *next;
} eos_mutex_record;

static eos_mutex_record *eos_mutex_records;
static uint32_t eos_sync_next_identity = UINT32_C(1);
static uint32_t eos_sync_identity_exhausted;
#ifdef EOS_RUST_HOST_TEST
static _Atomic uint32_t eos_sync_last_mutex_kind;
#endif

_Static_assert(sizeof(eos_rust_timespec) == 16,
               "timespec ABI size changed");
_Static_assert(_Alignof(eos_rust_timespec) == _Alignof(int64_t),
               "timespec ABI alignment changed");
_Static_assert(offsetof(eos_rust_timespec, tv_sec) == 0,
               "timespec seconds offset changed");
_Static_assert(offsetof(eos_rust_timespec, tv_nsec) == 8,
               "timespec nanoseconds offset changed");
_Static_assert(sizeof(eos_rust_pthread_mutex) == 16,
               "mutex ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_mutex) == _Alignof(uint32_t),
               "mutex ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_mutex, words) == 0,
               "mutex ABI offset changed");
_Static_assert(sizeof(eos_rust_pthread_mutexattr) == 8,
               "mutex attribute ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_mutexattr) == _Alignof(uint32_t),
               "mutex attribute ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_mutexattr, words) == 0,
               "mutex attribute ABI offset changed");

static int32_t eos_sync_status_error(int32_t status, const char *operation) {
    eos_error_result mapped =
        eos_error_from_port_status_impl(status, operation);
    return mapped.kind == EOS_ERROR_ERRNO ? mapped.error_number : EOS_ERRNO_IO;
}

static int32_t eos_sync_registry_lock(void) {
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_SYNC_REGISTRY);
    return status == 0 ? 0 : eos_sync_status_error(status, "sync.registry");
}

static void eos_sync_registry_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_SYNC_REGISTRY) != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: synchronization registry unlock failed\n");
        eos_rust_abort();
    }
}

static int32_t eos_sync_issue_identity_locked(uint32_t *identity) {
    if (eos_sync_identity_exhausted != 0) return EOS_ERRNO_WOULD_BLOCK;
    *identity = eos_sync_next_identity;
    if (eos_sync_next_identity == UINT32_MAX) {
        eos_sync_identity_exhausted = UINT32_C(1);
    } else {
        ++eos_sync_next_identity;
    }
    return 0;
}

static int eos_sync_words_zero(const uint32_t *words, uint32_t count) {
    uint32_t index;
    for (index = 0; index != count; ++index) {
        if (words[index] != 0) return 0;
    }
    return 1;
}

static void eos_sync_free(void *memory) {
    if (eos_port_memory_free(memory) != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: synchronization memory free failed\n");
        eos_rust_abort();
    }
}

static eos_mutex_record *eos_mutex_find_locked(uint32_t identity) {
    eos_mutex_record *record = eos_mutex_records;
    while (record != NULL && record->identity != identity) {
        record = record->next;
    }
    return record;
}

static int32_t eos_mutex_attribute_type(
    const eos_rust_pthread_mutexattr *attribute, uint32_t *type) {
    if (type == NULL) return EOS_ERRNO_INVALID;
    if (attribute == NULL ||
        eos_sync_words_zero(attribute->words, UINT32_C(2))) {
        *type = EOS_RUST_PTHREAD_MUTEX_NORMAL;
        return 0;
    }
    if (attribute->words[0] != EOS_SYNC_MUTEX_ATTR_MAGIC ||
        attribute->words[1] > EOS_RUST_PTHREAD_MUTEX_RECURSIVE) {
        return EOS_ERRNO_INVALID;
    }
    *type = attribute->words[1];
    return 0;
}

static int32_t eos_mutex_allocate(uint32_t type, eos_mutex_record **output) {
    eos_mutex_record *record = NULL;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*record),
                                           (void **)&record);
    if (status != 0 || record == NULL) {
        return eos_sync_status_error(status, "mutex.allocate");
    }
    (void)memset(record, 0, sizeof(*record));
    record->type = type;
    status = eos_port_mutex_create(
        type == EOS_RUST_PTHREAD_MUTEX_RECURSIVE ? UINT32_C(1) : UINT32_C(0),
        &record->native);
    if (status != 0) {
        eos_sync_free(record);
        return eos_sync_status_error(status, "mutex.create");
    }
#ifdef EOS_RUST_HOST_TEST
    atomic_store(&eos_sync_last_mutex_kind, type);
#endif
    *output = record;
    return 0;
}

static void eos_mutex_discard(eos_mutex_record *record) {
    if (eos_port_mutex_destroy(record->native) != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: private mutex destruction failed\n");
        eos_rust_abort();
    }
    eos_sync_free(record);
}

static int32_t eos_mutex_acquire_existing_locked(
    eos_rust_pthread_mutex *mutex, eos_mutex_record **output) {
    eos_mutex_record *record;
    if (mutex == NULL || mutex->words[0] == 0 ||
        mutex->words[1] != EOS_SYNC_MUTEX_MAGIC || mutex->words[2] != 0 ||
        mutex->words[3] != 0) {
        return EOS_ERRNO_INVALID;
    }
    record = eos_mutex_find_locked(mutex->words[0]);
    if (record == NULL) return EOS_ERRNO_INVALID;
    ++record->active_operations;
    *output = record;
    return 0;
}

static int32_t eos_mutex_acquire(eos_rust_pthread_mutex *mutex,
                                 uint32_t lazy,
                                 eos_mutex_record **output) {
    eos_mutex_record *candidate = NULL;
    int32_t status;
    if (mutex == NULL || output == NULL) return EOS_ERRNO_INVALID;
    for (;;) {
        status = eos_sync_registry_lock();
        if (status != 0) return status;
        if (!eos_sync_words_zero(mutex->words, UINT32_C(4))) {
            status = eos_mutex_acquire_existing_locked(mutex, output);
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_mutex_discard(candidate);
            return status;
        }
        if (lazy == 0) {
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_mutex_discard(candidate);
            return EOS_ERRNO_INVALID;
        }
        if (candidate != NULL) {
            status = eos_sync_issue_identity_locked(&candidate->identity);
            if (status == 0) {
                candidate->next = eos_mutex_records;
                eos_mutex_records = candidate;
                mutex->words[0] = candidate->identity;
                mutex->words[1] = EOS_SYNC_MUTEX_MAGIC;
                mutex->words[2] = 0;
                mutex->words[3] = 0;
                ++candidate->active_operations;
                *output = candidate;
                candidate = NULL;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_mutex_discard(candidate);
            return status;
        }
        eos_sync_registry_unlock();
        status = eos_mutex_allocate(EOS_RUST_PTHREAD_MUTEX_NORMAL,
                                    &candidate);
        if (status != 0) return status;
    }
}

static void eos_mutex_release_operation(eos_mutex_record *record) {
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    if (record->active_operations == 0) eos_rust_abort();
    --record->active_operations;
    eos_sync_registry_unlock();
}

int32_t eos_rust_pthread_mutexattr_init(
    eos_rust_pthread_mutexattr *attribute) {
    if (attribute == NULL) return EOS_ERRNO_INVALID;
    attribute->words[0] = EOS_SYNC_MUTEX_ATTR_MAGIC;
    attribute->words[1] = EOS_RUST_PTHREAD_MUTEX_NORMAL;
    return 0;
}

int32_t eos_rust_pthread_mutexattr_destroy(
    eos_rust_pthread_mutexattr *attribute) {
    uint32_t type;
    if (attribute == NULL || eos_mutex_attribute_type(attribute, &type) != 0) {
        return EOS_ERRNO_INVALID;
    }
    attribute->words[0] = EOS_SYNC_MUTEX_ATTR_DEAD;
    attribute->words[1] = 0;
    return 0;
}

int32_t eos_rust_pthread_mutexattr_settype(
    eos_rust_pthread_mutexattr *attribute, int32_t type) {
    uint32_t old_type;
    if (attribute == NULL) return EOS_ERRNO_INVALID;
    if (eos_mutex_attribute_type(attribute, &old_type) != 0) {
        return EOS_ERRNO_INVALID;
    }
    if (type != EOS_RUST_PTHREAD_MUTEX_NORMAL &&
        type != EOS_RUST_PTHREAD_MUTEX_RECURSIVE) {
        return EOS_ERRNO_NOT_SUPPORTED;
    }
    attribute->words[0] = EOS_SYNC_MUTEX_ATTR_MAGIC;
    attribute->words[1] = (uint32_t)type;
    return 0;
}

int32_t eos_rust_pthread_mutex_init(
    eos_rust_pthread_mutex *mutex,
    const eos_rust_pthread_mutexattr *attribute) {
    eos_mutex_record *record = NULL;
    uint32_t type;
    int32_t status;
    if (mutex == NULL) return EOS_ERRNO_INVALID;
    status = eos_mutex_attribute_type(attribute, &type);
    if (status != 0) return status;
    status = eos_mutex_allocate(type, &record);
    if (status != 0) return status;
    status = eos_sync_registry_lock();
    if (status != 0) {
        eos_mutex_discard(record);
        return status;
    }
    if (!eos_sync_words_zero(mutex->words, UINT32_C(4))) {
        status = mutex->words[1] == EOS_SYNC_MUTEX_MAGIC &&
                         mutex->words[2] == 0 && mutex->words[3] == 0 &&
                         eos_mutex_find_locked(mutex->words[0]) != NULL
                     ? EOS_ERRNO_BUSY
                     : EOS_ERRNO_INVALID;
        eos_sync_registry_unlock();
        eos_mutex_discard(record);
        return status;
    }
    status = eos_sync_issue_identity_locked(&record->identity);
    if (status == 0) {
        record->next = eos_mutex_records;
        eos_mutex_records = record;
        mutex->words[0] = record->identity;
        mutex->words[1] = EOS_SYNC_MUTEX_MAGIC;
        mutex->words[2] = 0;
        mutex->words[3] = 0;
        record = NULL;
    }
    eos_sync_registry_unlock();
    if (record != NULL) eos_mutex_discard(record);
    return status;
}

static int32_t eos_mutex_lock_common(eos_rust_pthread_mutex *mutex,
                                     uint32_t timeout_ticks) {
    eos_mutex_record *record = NULL;
    eos_rust_thread_t self = eos_rust_pthread_self();
    int32_t status = eos_mutex_acquire(mutex, UINT32_C(1), &record);
    if (status != 0) return status;
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    ++record->waiters;
    eos_sync_registry_unlock();
    status = eos_port_mutex_lock(record->native, timeout_ticks);
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    if (record->waiters == 0 || record->active_operations == 0) eos_rust_abort();
    --record->waiters;
    if (status == 0) {
        if (record->depth == 0) record->owner = self;
        if (record->owner != self) eos_rust_abort();
        ++record->depth;
    }
    --record->active_operations;
    eos_sync_registry_unlock();
    if (status == 0) return 0;
    if (timeout_ticks == EOS_PORT_NO_WAIT &&
        (status == 19 || status == 22)) {
        return EOS_ERRNO_BUSY;
    }
    return eos_sync_status_error(status, "mutex.lock");
}

int32_t eos_rust_pthread_mutex_lock(eos_rust_pthread_mutex *mutex) {
    return eos_mutex_lock_common(mutex, EOS_PORT_WAIT_FOREVER);
}

int32_t eos_rust_pthread_mutex_trylock(eos_rust_pthread_mutex *mutex) {
    return eos_mutex_lock_common(mutex, EOS_PORT_NO_WAIT);
}

int32_t eos_rust_pthread_mutex_unlock(eos_rust_pthread_mutex *mutex) {
    eos_mutex_record *record = NULL;
    eos_rust_thread_t self = eos_rust_pthread_self();
    int32_t status = eos_sync_registry_lock();
    if (status != 0) return status;
    status = eos_mutex_acquire_existing_locked(mutex, &record);
    if (status != 0) {
        eos_sync_registry_unlock();
        return status;
    }
    if (record->depth == 0 || record->owner != self) {
        --record->active_operations;
        eos_sync_registry_unlock();
        return EOS_ERRNO_INVALID;
    }
    --record->depth;
    if (record->depth == 0) record->owner = 0;
    eos_sync_registry_unlock();
    status = eos_port_mutex_unlock(record->native);
    if (status != 0) {
        eos_port_direct_diagnostic("libeos_rust_abi: mutex unlock failed\n");
        eos_rust_abort();
    }
    eos_mutex_release_operation(record);
    return 0;
}

int32_t eos_rust_pthread_mutex_destroy(eos_rust_pthread_mutex *mutex) {
    eos_mutex_record *record;
    eos_mutex_record **link;
    int32_t status;
    if (mutex == NULL) return EOS_ERRNO_INVALID;
    status = eos_sync_registry_lock();
    if (status != 0) return status;
    if (eos_sync_words_zero(mutex->words, UINT32_C(4))) {
        eos_sync_registry_unlock();
        return 0;
    }
    if (mutex->words[1] != EOS_SYNC_MUTEX_MAGIC || mutex->words[2] != 0 ||
        mutex->words[3] != 0 ||
        (record = eos_mutex_find_locked(mutex->words[0])) == NULL) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_INVALID;
    }
    if (record->active_operations != 0 || record->waiters != 0 ||
        record->depth != 0) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_BUSY;
    }
    link = &eos_mutex_records;
    while (*link != record) link = &(*link)->next;
    *link = record->next;
    mutex->words[0] = EOS_SYNC_OBJECT_DEAD;
    mutex->words[1] = EOS_SYNC_MUTEX_MAGIC;
    mutex->words[2] = 0;
    mutex->words[3] = 0;
    eos_sync_registry_unlock();
    if (eos_port_mutex_destroy(record->native) != 0) {
        eos_port_direct_diagnostic("libeos_rust_abi: mutex destroy failed\n");
        eos_rust_abort();
    }
    eos_sync_free(record);
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_sync_test_last_mutex_kind(void) {
    return atomic_load(&eos_sync_last_mutex_kind);
}

uint32_t eos_sync_test_live_mutexes(void) {
    uint32_t count = 0;
    eos_mutex_record *record;
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    for (record = eos_mutex_records; record != NULL; record = record->next) {
        ++count;
    }
    eos_sync_registry_unlock();
    return count;
}

void eos_sync_test_exhaust_identities(void) {
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    eos_sync_identity_exhausted = UINT32_C(1);
    eos_sync_registry_unlock();
}
#endif
