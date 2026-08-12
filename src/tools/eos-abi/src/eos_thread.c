#include "eos_tls.h"

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define EOS_THREAD_ATTR_MAGIC UINT32_C(0x45504131)
#define EOS_THREAD_ATTR_DEAD UINT32_C(0x45504431)
#define EOS_THREAD_STATE_JOINABLE UINT32_C(0)
#define EOS_THREAD_STATE_JOIN_CLAIMED UINT32_C(1)
#define EOS_THREAD_STATE_DETACHED UINT32_C(2)
#define EOS_THREAD_STATE_EXTERNAL UINT32_C(3)

typedef struct eos_thread_record {
    eos_rust_thread_t identity;
    uint32_t references;
    uint32_t state;
    uint32_t in_registry;
    _Atomic uint32_t completed;
    void *result;
    void *(*start_routine)(void *);
    void *argument;
    eos_port_sync completion;
    char name[64];
    struct eos_thread_record *next;
} eos_thread_record;

static eos_thread_record *eos_thread_records;
static uint32_t eos_thread_next_identity = UINT32_C(1);
static uint32_t eos_thread_identity_exhausted;

#ifdef EOS_RUST_HOST_TEST
static _Atomic uint32_t eos_thread_test_completion_pause;
static _Atomic uint32_t eos_thread_test_completion_entered;
static _Atomic uint32_t eos_thread_test_completion_release;
static _Atomic uint32_t eos_thread_test_completion_cleanup_done;

void eos_thread_test_pause_after_completion(int enabled) {
    atomic_store(&eos_thread_test_completion_entered, UINT32_C(0));
    atomic_store(&eos_thread_test_completion_cleanup_done, UINT32_C(0));
    atomic_store(&eos_thread_test_completion_release,
                 enabled ? UINT32_C(0) : UINT32_C(1));
    atomic_store(&eos_thread_test_completion_pause,
                 enabled ? UINT32_C(1) : UINT32_C(0));
}

uint32_t eos_thread_test_completion_pause_entered(void) {
    return atomic_load(&eos_thread_test_completion_entered);
}

void eos_thread_test_resume_after_completion(void) {
    atomic_store(&eos_thread_test_completion_release, UINT32_C(1));
    atomic_store(&eos_thread_test_completion_pause, UINT32_C(0));
}

uint32_t eos_thread_test_completion_cleanup_finished(void) {
    return atomic_load(&eos_thread_test_completion_cleanup_done);
}
#endif

_Static_assert(sizeof(eos_rust_thread_t) == 4,
               "thread identity must remain 32-bit");
_Static_assert(sizeof(eos_rust_tls_key_t) == 4,
               "TLS key must remain 32-bit");
_Static_assert(sizeof(eos_rust_pthread_attr) == 16,
               "pthread attr ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_attr) == _Alignof(uint32_t),
               "pthread attr ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_attr, words) == 0,
               "pthread attr ABI offset changed");

static int32_t eos_thread_status_error(int32_t status) {
    eos_error_result mapped = eos_error_from_port_status_impl(status, "thread");
    return mapped.kind == EOS_ERROR_ERRNO ? mapped.error_number : EOS_ERRNO_IO;
}

static int32_t eos_thread_lock_direct(void) {
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_THREAD_REGISTRY);
    return status == 0 ? 0 : eos_thread_status_error(status);
}

static void eos_thread_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_THREAD_REGISTRY) != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: thread registry unlock failed\n");
        eos_rust_abort();
    }
}

static eos_thread_record *eos_thread_find_locked(eos_rust_thread_t identity) {
    eos_thread_record *record = eos_thread_records;
    while (record != NULL && record->identity != identity) record = record->next;
    return record;
}

static int eos_thread_unref_locked(eos_thread_record *record);

static int eos_thread_drop_registry_locked(eos_thread_record *record) {
    eos_thread_record **link = &eos_thread_records;
    if (record->in_registry == 0) return 0;
    while (*link != NULL && *link != record) link = &(*link)->next;
    if (*link != record) eos_rust_abort();
    *link = record->next;
    record->in_registry = 0;
    return eos_thread_unref_locked(record);
}

static int eos_thread_unref_locked(eos_thread_record *record) {
    if (record->references == 0) eos_rust_abort();
    --record->references;
    return record->references == 0;
}

static void eos_thread_destroy_record(eos_thread_record *record) {
    if (eos_port_sync_destroy(record->completion) != 0) eos_rust_abort();
    if (eos_port_memory_free(record) != 0) eos_rust_abort();
}

static int32_t eos_thread_allocate_record(eos_thread_record **output,
                                          uint32_t child_reference) {
    eos_thread_record *record = NULL;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*record),
                                           (void **)&record);
    if (status != 0 || record == NULL) return eos_thread_status_error(status);
    (void)memset(record, 0, sizeof(*record));
    status = eos_port_sync_create(&record->completion);
    if (status != 0) {
        if (eos_port_memory_free(record) != 0) eos_rust_abort();
        return eos_thread_status_error(status);
    }

    status = eos_thread_lock_direct();
    if (status != 0) {
        eos_thread_destroy_record(record);
        return status;
    }
    if (eos_thread_identity_exhausted != 0) {
        eos_thread_unlock();
        eos_thread_destroy_record(record);
        return EOS_ERRNO_WOULD_BLOCK;
    }
    record->identity = eos_thread_next_identity;
    if (eos_thread_next_identity == UINT32_MAX) {
        eos_thread_identity_exhausted = UINT32_C(1);
    } else {
        ++eos_thread_next_identity;
    }
    record->references = UINT32_C(1) + child_reference;
    record->in_registry = UINT32_C(1);
    record->state = child_reference != 0 ? EOS_THREAD_STATE_JOINABLE
                                         : EOS_THREAD_STATE_EXTERNAL;
    record->next = eos_thread_records;
    eos_thread_records = record;
    eos_thread_unlock();
    *output = record;
    return 0;
}

static void eos_thread_trampoline(void *opaque) {
    eos_thread_record *record = (eos_thread_record *)opaque;
    void *result;
    int destroy = 0;
    int32_t status;
#ifdef EOS_RUST_HOST_TEST
    int test_was_paused = 0;
#endif

    eos_tls_set_thread_identity(record->identity);
    result = record->start_routine(record->argument);
    eos_tls_cleanup_current();

    status = eos_port_sync_lock(record->completion);
    if (status != 0) eos_rust_abort();
    record->result = result;
    atomic_store_explicit(&record->completed, UINT32_C(1), memory_order_release);
    if (eos_port_sync_broadcast(record->completion, UINT32_C(1)) != 0) {
        eos_rust_abort();
    }
    if (eos_port_sync_unlock(record->completion) != 0) eos_rust_abort();

#ifdef EOS_RUST_HOST_TEST
    if (atomic_load(&eos_thread_test_completion_pause) != 0) {
        test_was_paused = 1;
        atomic_store(&eos_thread_test_completion_entered, UINT32_C(1));
        while (atomic_load(&eos_thread_test_completion_release) == 0) {}
    }
#endif

    if (eos_thread_lock_direct() != 0) eos_rust_abort();
    if (record->state == EOS_THREAD_STATE_DETACHED) {
        destroy = eos_thread_drop_registry_locked(record);
    }
    if (eos_thread_unref_locked(record)) destroy = 1;
    eos_thread_unlock();
    if (destroy) eos_thread_destroy_record(record);
#ifdef EOS_RUST_HOST_TEST
    if (test_was_paused) {
        atomic_store(&eos_thread_test_completion_cleanup_done, UINT32_C(1));
    }
#endif
}

static int32_t eos_thread_stack_from_attr(const eos_rust_pthread_attr *attribute,
                                          uint32_t *stack_size) {
    if (stack_size == NULL) return EOS_ERRNO_INVALID;
    if (attribute == NULL || attribute->words[0] == 0) {
        *stack_size = EOS_RUST_PTHREAD_STACK_MIN;
        return 0;
    }
    if (attribute->words[0] != EOS_THREAD_ATTR_MAGIC ||
        attribute->words[1] < EOS_RUST_PTHREAD_STACK_MIN ||
        (attribute->words[1] & UINT32_C(7)) != 0) {
        return EOS_ERRNO_INVALID;
    }
    *stack_size = attribute->words[1];
    return 0;
}

int32_t eos_rust_pthread_attr_init(eos_rust_pthread_attr *attribute) {
    if (attribute == NULL) return EOS_ERRNO_INVALID;
    (void)memset(attribute, 0, sizeof(*attribute));
    attribute->words[0] = EOS_THREAD_ATTR_MAGIC;
    attribute->words[1] = EOS_RUST_PTHREAD_STACK_MIN;
    return 0;
}

int32_t eos_rust_pthread_attr_destroy(eos_rust_pthread_attr *attribute) {
    if (attribute == NULL || attribute->words[0] == EOS_THREAD_ATTR_DEAD ||
        (attribute->words[0] != 0 && attribute->words[0] != EOS_THREAD_ATTR_MAGIC)) {
        return EOS_ERRNO_INVALID;
    }
    (void)memset(attribute, 0, sizeof(*attribute));
    attribute->words[0] = EOS_THREAD_ATTR_DEAD;
    return 0;
}

int32_t eos_rust_pthread_attr_getstacksize(
    const eos_rust_pthread_attr *attribute, uint32_t *stack_size) {
    if (attribute == NULL) return EOS_ERRNO_INVALID;
    return eos_thread_stack_from_attr(attribute, stack_size);
}

int32_t eos_rust_pthread_attr_setstacksize(eos_rust_pthread_attr *attribute,
                                           uint32_t stack_size) {
    if (attribute == NULL || attribute->words[0] == EOS_THREAD_ATTR_DEAD ||
        (attribute->words[0] != 0 && attribute->words[0] != EOS_THREAD_ATTR_MAGIC) ||
        stack_size < EOS_RUST_PTHREAD_STACK_MIN ||
        (stack_size & UINT32_C(7)) != 0) {
        return EOS_ERRNO_INVALID;
    }
    if (attribute->words[0] == 0) {
        (void)memset(attribute, 0, sizeof(*attribute));
        attribute->words[0] = EOS_THREAD_ATTR_MAGIC;
    }
    attribute->words[1] = stack_size;
    return 0;
}

int32_t eos_rust_pthread_create(eos_rust_thread_t *thread,
                                const eos_rust_pthread_attr *attribute,
                                void *(*start_routine)(void *),
                                void *argument) {
    eos_thread_record *record = NULL;
    uint32_t stack_size;
    int32_t status;
    int destroy = 0;
    if (thread == NULL || start_routine == NULL) return EOS_ERRNO_INVALID;
    *thread = 0;
    status = eos_thread_stack_from_attr(attribute, &stack_size);
    if (status != 0) return status;
    status = eos_thread_allocate_record(&record, UINT32_C(1));
    if (status != 0) return status;
    record->start_routine = start_routine;
    record->argument = argument;
    (void)memcpy(record->name, "eos.rust", sizeof("eos.rust"));

    status = eos_port_thread_create("eos.rust", eos_thread_trampoline, record,
                                    stack_size);
    if (status != 0) {
        if (eos_thread_lock_direct() != 0) eos_rust_abort();
        if (atomic_load_explicit(&record->completed, memory_order_acquire) != 0) {
            eos_thread_unlock();
            eos_port_direct_diagnostic(
                "libeos_rust_abi: native create failed after start\n");
            eos_rust_abort();
        }
        (void)eos_thread_drop_registry_locked(record);
        destroy = eos_thread_unref_locked(record);
        eos_thread_unlock();
        if (destroy) eos_thread_destroy_record(record);
        return eos_thread_status_error(status);
    }
    *thread = record->identity;
    return 0;
}

eos_rust_thread_t eos_rust_pthread_self(void) {
    uint32_t identity = eos_tls_thread_identity();
    eos_thread_record *record;
    int32_t status;
    if (identity != 0) return identity;
    status = eos_thread_allocate_record(&record, UINT32_C(0));
    if (status != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: external thread identity allocation failed\n");
        eos_rust_abort();
    }
    eos_tls_set_thread_identity(record->identity);
    return record->identity;
}

int32_t eos_rust_pthread_equal(eos_rust_thread_t left,
                               eos_rust_thread_t right) {
    return left == right ? 1 : 0;
}

int32_t eos_rust_pthread_join(eos_rust_thread_t identity, void **result) {
    eos_thread_record *record;
    int32_t status;
    int destroy = 0;

    if (identity != 0 && identity == eos_tls_thread_identity()) {
        return EOS_ERRNO_DEADLOCK;
    }
    status = eos_thread_lock_direct();
    if (status != 0) return status;
    record = eos_thread_find_locked(identity);
    if (record == NULL || record->state == EOS_THREAD_STATE_EXTERNAL) {
        eos_thread_unlock();
        return EOS_ERRNO_NO_PROCESS;
    }
    if (record->state != EOS_THREAD_STATE_JOINABLE) {
        eos_thread_unlock();
        return EOS_ERRNO_INVALID;
    }
    record->state = EOS_THREAD_STATE_JOIN_CLAIMED;
    ++record->references;
    eos_thread_unlock();

    status = eos_port_sync_lock(record->completion);
    if (status == 0) {
        while (atomic_load_explicit(&record->completed,
                                    memory_order_acquire) == 0) {
            status = eos_port_sync_wait(record->completion, UINT32_C(1));
            if (status != 0) break;
        }
        if (eos_port_sync_unlock(record->completion) != 0) eos_rust_abort();
    }
    if (status != 0) {
        int32_t mapped = eos_thread_status_error(status);
        if (eos_thread_lock_direct() != 0) eos_rust_abort();
        if (record->state == EOS_THREAD_STATE_JOIN_CLAIMED) {
            record->state = EOS_THREAD_STATE_JOINABLE;
        }
        destroy = eos_thread_unref_locked(record);
        eos_thread_unlock();
        if (destroy) eos_thread_destroy_record(record);
        return mapped;
    }

    if (result != NULL) *result = record->result;
    if (eos_thread_lock_direct() != 0) eos_rust_abort();
    (void)eos_thread_drop_registry_locked(record);
    destroy = eos_thread_unref_locked(record);
    eos_thread_unlock();
    if (destroy) eos_thread_destroy_record(record);
    return 0;
}

int32_t eos_rust_pthread_detach(eos_rust_thread_t identity) {
    eos_thread_record *record;
    int32_t status = eos_thread_lock_direct();
    int destroy = 0;
    if (status != 0) return status;
    record = eos_thread_find_locked(identity);
    if (record == NULL || record->state == EOS_THREAD_STATE_EXTERNAL) {
        eos_thread_unlock();
        return EOS_ERRNO_NO_PROCESS;
    }
    if (record->state != EOS_THREAD_STATE_JOINABLE) {
        eos_thread_unlock();
        return EOS_ERRNO_INVALID;
    }
    record->state = EOS_THREAD_STATE_DETACHED;
    if (atomic_load_explicit(&record->completed, memory_order_acquire) != 0) {
        destroy = eos_thread_drop_registry_locked(record);
    }
    eos_thread_unlock();
    if (destroy) eos_thread_destroy_record(record);
    return 0;
}

int32_t eos_rust_pthread_getname_np(eos_rust_thread_t identity,
                                    char *name, uint32_t capacity) {
    eos_thread_record *record;
    size_t length;
    uint32_t copied;
    int32_t status;
    if (name == NULL) return EOS_ERRNO_INVALID;
    status = eos_thread_lock_direct();
    if (status != 0) return status;
    record = eos_thread_find_locked(identity);
    if (record == NULL) {
        eos_thread_unlock();
        return EOS_ERRNO_NO_PROCESS;
    }
    length = strlen(record->name);
    copied = capacity == 0 ? 0 :
             (length < (size_t)capacity - 1U ? (uint32_t)length : capacity - 1U);
    if (copied != 0) (void)memcpy(name, record->name, copied);
    if (capacity != 0) name[copied] = '\0';
    eos_thread_unlock();
    return length + 1U > (size_t)capacity ? EOS_ERRNO_RANGE : 0;
}

int32_t eos_rust_pthread_setname_np(eos_rust_thread_t identity,
                                    const char *name) {
    eos_thread_record *record;
    size_t length;
    int32_t status;
    if (name == NULL) return EOS_ERRNO_INVALID;
    length = strlen(name);
    if (length > EOS_RUST_PTHREAD_NAME_MAX) return EOS_ERRNO_NAME_TOO_LONG;
    status = eos_thread_lock_direct();
    if (status != 0) return status;
    record = eos_thread_find_locked(identity);
    if (record == NULL) {
        eos_thread_unlock();
        return EOS_ERRNO_NO_PROCESS;
    }
    (void)memcpy(record->name, name, length + 1U);
    eos_thread_unlock();
    return 0;
}

int32_t eos_rust_pthread_yield(void) {
    return EOS_ERRNO_NOT_SUPPORTED;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_thread_test_live_records(void) {
    eos_thread_record *record;
    uint32_t count = 0;
    if (eos_thread_lock_direct() != 0) eos_rust_abort();
    for (record = eos_thread_records; record != NULL; record = record->next) ++count;
    eos_thread_unlock();
    return count;
}
uint32_t eos_thread_test_state(eos_rust_thread_t identity) {
    eos_thread_record *record;
    uint32_t state = UINT32_MAX;
    if (eos_thread_lock_direct() != 0) eos_rust_abort();
    record = eos_thread_find_locked(identity);
    if (record != NULL) state = record->state;
    eos_thread_unlock();
    return state;
}
#endif
