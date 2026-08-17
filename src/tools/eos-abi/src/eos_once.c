#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EOS_SYNC_ONCE_MAGIC UINT32_C(0x45534f31)
#define EOS_SYNC_ONCE_NEW UINT32_C(0)
#define EOS_SYNC_ONCE_RUNNING UINT32_C(1)
#define EOS_SYNC_ONCE_COMPLETE UINT32_C(2)

typedef struct eos_once_record {
    uint32_t identity;
    uint32_t state;
    uint32_t active_operations;
    eos_rust_thread_t owner;
    eos_port_sync completion;
    struct eos_once_record *next;
} eos_once_record;

static eos_once_record *eos_once_records;

_Static_assert(sizeof(eos_rust_pthread_once_t) == 8,
               "once ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_once_t) == _Alignof(uint32_t),
               "once ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_once_t, words) == 0,
               "once ABI offset changed");

static eos_once_record *eos_once_find_locked(uint32_t identity) {
    eos_once_record *record = eos_once_records;
    while (record != NULL && record->identity != identity) {
        record = record->next;
    }
    return record;
}

static int32_t eos_once_allocate(eos_once_record **output) {
    eos_once_record *record = NULL;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*record),
                                           (void **)&record);
    if (status != 0 || record == NULL) {
        return eos_sync_status_error(status, "once.allocate");
    }
    (void)memset(record, 0, sizeof(*record));
    status = eos_port_sync_create(&record->completion);
    if (status != 0) {
        eos_sync_free(record);
        return eos_sync_status_error(status, "once.create");
    }
    *output = record;
    return 0;
}

static void eos_once_discard(eos_once_record *record) {
    if (eos_port_sync_destroy(record->completion) != 0) eos_rust_abort();
    eos_sync_free(record);
}

static int32_t eos_once_acquire(eos_rust_pthread_once_t *control,
                                eos_once_record **output) {
    eos_once_record *candidate = NULL;
    int32_t status;
    if (control == NULL || output == NULL) return EOS_ERRNO_INVALID;
    for (;;) {
        status = eos_sync_registry_lock();
        if (status != 0) return status;
        if (!eos_sync_words_zero(control->words, UINT32_C(2))) {
            eos_once_record *record;
            if (control->words[1] != EOS_SYNC_ONCE_MAGIC ||
                (record = eos_once_find_locked(control->words[0])) == NULL) {
                status = EOS_ERRNO_INVALID;
            } else {
                ++record->active_operations;
                *output = record;
                status = 0;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_once_discard(candidate);
            return status;
        }
        if (candidate != NULL) {
            status = eos_sync_issue_identity_locked(&candidate->identity);
            if (status == 0) {
                candidate->next = eos_once_records;
                eos_once_records = candidate;
                control->words[0] = candidate->identity;
                control->words[1] = EOS_SYNC_ONCE_MAGIC;
                ++candidate->active_operations;
                *output = candidate;
                candidate = NULL;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_once_discard(candidate);
            return status;
        }
        eos_sync_registry_unlock();
        status = eos_once_allocate(&candidate);
        if (status != 0) return status;
    }
}

static void eos_once_release_operation(eos_once_record *record) {
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    if (record->active_operations == 0) eos_rust_abort();
    --record->active_operations;
    eos_sync_registry_unlock();
}

int32_t eos_rust_pthread_once(eos_rust_pthread_once_t *control,
                              void (*initialization_routine)(void)) {
    eos_once_record *record;
    eos_rust_thread_t self;
    int run = 0;
    int32_t status;
    if (initialization_routine == NULL) return EOS_ERRNO_INVALID;
    status = eos_once_acquire(control, &record);
    if (status != 0) return status;
    self = eos_rust_pthread_self();
    status = eos_port_sync_lock(record->completion);
    if (status != 0) {
        eos_once_release_operation(record);
        return eos_sync_status_error(status, "once.lock");
    }
    while (record->state == EOS_SYNC_ONCE_RUNNING) {
        status = eos_port_sync_wait(record->completion, UINT32_C(1));
        if (status != 0) {
            if (eos_port_sync_unlock(record->completion) != 0) eos_rust_abort();
            eos_once_release_operation(record);
            return eos_sync_status_error(status, "once.wait");
        }
    }
    if (record->state == EOS_SYNC_ONCE_NEW) {
        record->state = EOS_SYNC_ONCE_RUNNING;
        record->owner = self;
        run = 1;
    }
    if (eos_port_sync_unlock(record->completion) != 0) eos_rust_abort();
    if (run != 0) {
        initialization_routine();
        if (eos_port_sync_lock(record->completion) != 0) eos_rust_abort();
        if (record->state != EOS_SYNC_ONCE_RUNNING || record->owner != self) {
            eos_rust_abort();
        }
        record->owner = 0;
        record->state = EOS_SYNC_ONCE_COMPLETE;
        if (eos_port_sync_broadcast(record->completion, UINT32_C(1)) != 0) {
            eos_rust_abort();
        }
        if (eos_port_sync_unlock(record->completion) != 0) eos_rust_abort();
    }
    eos_once_release_operation(record);
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_sync_test_live_once(void) {
    uint32_t count = 0;
    eos_once_record *record;
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    for (record = eos_once_records; record != NULL; record = record->next) {
        ++count;
    }
    eos_sync_registry_unlock();
    return count;
}
#endif
