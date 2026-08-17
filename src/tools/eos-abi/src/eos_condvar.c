#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define EOS_SYNC_COND_MAGIC UINT32_C(0x45534331)
#define EOS_SYNC_COND_ATTR_MAGIC UINT32_C(0x45534341)
#define EOS_SYNC_COND_ATTR_DEAD UINT32_C(0x45534344)

typedef struct eos_condition_waiter {
    eos_port_semaphore semaphore;
    uint32_t selected;
    uint32_t listed;
    struct eos_condition_waiter *next;
} eos_condition_waiter;

typedef struct eos_condition_record {
    uint32_t identity;
    uint32_t active_operations;
    uint32_t waiter_count;
    eos_port_mutex lock;
    eos_condition_waiter *head;
    eos_condition_waiter *tail;
    struct eos_condition_record *next;
} eos_condition_record;

static eos_condition_record *eos_condition_records;

#ifdef EOS_RUST_HOST_TEST
static _Atomic uint32_t eos_condition_test_timeout_pause;
static _Atomic uint32_t eos_condition_test_timeout_entered;
static _Atomic uint32_t eos_condition_test_timeout_release;

void eos_sync_test_pause_after_condition_timeout(int enabled) {
    atomic_store(&eos_condition_test_timeout_entered, UINT32_C(0));
    atomic_store(&eos_condition_test_timeout_release,
                 enabled ? UINT32_C(0) : UINT32_C(1));
    atomic_store(&eos_condition_test_timeout_pause,
                 enabled ? UINT32_C(1) : UINT32_C(0));
}

uint32_t eos_sync_test_condition_timeout_pause_entered(void) {
    return atomic_load(&eos_condition_test_timeout_entered);
}

void eos_sync_test_resume_after_condition_timeout(void) {
    atomic_store(&eos_condition_test_timeout_release, UINT32_C(1));
    atomic_store(&eos_condition_test_timeout_pause, UINT32_C(0));
}
#endif

_Static_assert(sizeof(eos_rust_pthread_cond) == 16,
               "condition ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_cond) == _Alignof(uint32_t),
               "condition ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_cond, words) == 0,
               "condition ABI offset changed");
_Static_assert(sizeof(eos_rust_pthread_condattr) == 8,
               "condition attribute ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_condattr) == _Alignof(uint32_t),
               "condition attribute ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_condattr, words) == 0,
               "condition attribute ABI offset changed");

static eos_condition_record *eos_condition_find_locked(uint32_t identity) {
    eos_condition_record *record = eos_condition_records;
    while (record != NULL && record->identity != identity) {
        record = record->next;
    }
    return record;
}

static int32_t eos_condition_attribute_validate(
    const eos_rust_pthread_condattr *attribute) {
    if (attribute == NULL ||
        eos_sync_words_zero(attribute->words, UINT32_C(2))) {
        return 0;
    }
    return attribute->words[0] == EOS_SYNC_COND_ATTR_MAGIC &&
                   attribute->words[1] == EOS_RUST_CLOCK_MONOTONIC
               ? 0
               : EOS_ERRNO_INVALID;
}

static int32_t eos_condition_allocate(eos_condition_record **output) {
    eos_condition_record *record = NULL;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*record),
                                           (void **)&record);
    if (status != 0 || record == NULL) {
        return eos_sync_status_error(status, "condition.allocate");
    }
    (void)memset(record, 0, sizeof(*record));
    status = eos_port_mutex_create(UINT32_C(0), &record->lock);
    if (status != 0) {
        eos_sync_free(record);
        return eos_sync_status_error(status, "condition.lock.create");
    }
    *output = record;
    return 0;
}

static void eos_condition_discard(eos_condition_record *record) {
    if (eos_port_mutex_destroy(record->lock) != 0) eos_rust_abort();
    eos_sync_free(record);
}

static int32_t eos_condition_acquire(eos_rust_pthread_cond *condition,
                                     uint32_t lazy,
                                     eos_condition_record **output) {
    eos_condition_record *candidate = NULL;
    int32_t status;
    if (condition == NULL || output == NULL) return EOS_ERRNO_INVALID;
    for (;;) {
        status = eos_sync_registry_lock();
        if (status != 0) return status;
        if (!eos_sync_words_zero(condition->words, UINT32_C(4))) {
            eos_condition_record *record;
            if (condition->words[1] != EOS_SYNC_COND_MAGIC ||
                condition->words[2] != 0 || condition->words[3] != 0 ||
                (record = eos_condition_find_locked(condition->words[0])) ==
                    NULL) {
                status = EOS_ERRNO_INVALID;
            } else {
                ++record->active_operations;
                *output = record;
                status = 0;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_condition_discard(candidate);
            return status;
        }
        if (lazy == 0) {
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_condition_discard(candidate);
            return EOS_ERRNO_INVALID;
        }
        if (candidate != NULL) {
            status = eos_sync_issue_identity_locked(&candidate->identity);
            if (status == 0) {
                candidate->next = eos_condition_records;
                eos_condition_records = candidate;
                condition->words[0] = candidate->identity;
                condition->words[1] = EOS_SYNC_COND_MAGIC;
                condition->words[2] = 0;
                condition->words[3] = 0;
                ++candidate->active_operations;
                *output = candidate;
                candidate = NULL;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_condition_discard(candidate);
            return status;
        }
        eos_sync_registry_unlock();
        status = eos_condition_allocate(&candidate);
        if (status != 0) return status;
    }
}

static void eos_condition_release_operation(eos_condition_record *record) {
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    if (record->active_operations == 0) eos_rust_abort();
    --record->active_operations;
    eos_sync_registry_unlock();
}

int32_t eos_rust_pthread_condattr_init(
    eos_rust_pthread_condattr *attribute) {
    if (attribute == NULL) return EOS_ERRNO_INVALID;
    attribute->words[0] = EOS_SYNC_COND_ATTR_MAGIC;
    attribute->words[1] = EOS_RUST_CLOCK_MONOTONIC;
    return 0;
}

int32_t eos_rust_pthread_condattr_destroy(
    eos_rust_pthread_condattr *attribute) {
    if (attribute == NULL || eos_condition_attribute_validate(attribute) != 0) {
        return EOS_ERRNO_INVALID;
    }
    attribute->words[0] = EOS_SYNC_COND_ATTR_DEAD;
    attribute->words[1] = 0;
    return 0;
}

int32_t eos_rust_pthread_condattr_setclock(
    eos_rust_pthread_condattr *attribute, int32_t clock_id) {
    if (attribute == NULL || eos_condition_attribute_validate(attribute) != 0) {
        return EOS_ERRNO_INVALID;
    }
    if (clock_id == EOS_RUST_CLOCK_REALTIME) return EOS_ERRNO_NOT_SUPPORTED;
    if (clock_id != EOS_RUST_CLOCK_MONOTONIC) return EOS_ERRNO_INVALID;
    attribute->words[0] = EOS_SYNC_COND_ATTR_MAGIC;
    attribute->words[1] = EOS_RUST_CLOCK_MONOTONIC;
    return 0;
}

int32_t eos_rust_pthread_cond_init(
    eos_rust_pthread_cond *condition,
    const eos_rust_pthread_condattr *attribute) {
    eos_condition_record *record;
    int32_t status;
    if (condition == NULL || eos_condition_attribute_validate(attribute) != 0) {
        return EOS_ERRNO_INVALID;
    }
    status = eos_condition_allocate(&record);
    if (status != 0) return status;
    status = eos_sync_registry_lock();
    if (status != 0) {
        eos_condition_discard(record);
        return status;
    }
    if (!eos_sync_words_zero(condition->words, UINT32_C(4))) {
        status = condition->words[1] == EOS_SYNC_COND_MAGIC &&
                         condition->words[2] == 0 &&
                         condition->words[3] == 0 &&
                         eos_condition_find_locked(condition->words[0]) != NULL
                     ? EOS_ERRNO_BUSY
                     : EOS_ERRNO_INVALID;
        eos_sync_registry_unlock();
        eos_condition_discard(record);
        return status;
    }
    status = eos_sync_issue_identity_locked(&record->identity);
    if (status == 0) {
        record->next = eos_condition_records;
        eos_condition_records = record;
        condition->words[0] = record->identity;
        condition->words[1] = EOS_SYNC_COND_MAGIC;
        condition->words[2] = 0;
        condition->words[3] = 0;
        record = NULL;
    }
    eos_sync_registry_unlock();
    if (record != NULL) eos_condition_discard(record);
    return status;
}

static int32_t eos_condition_signal_common(eos_rust_pthread_cond *condition,
                                           uint32_t broadcast) {
    eos_condition_record *record = NULL;
    eos_condition_waiter *waiter;
    int32_t status = eos_condition_acquire(condition, UINT32_C(1), &record);
    if (status != 0) return status;
    status = eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER);
    if (status != 0) {
        eos_condition_release_operation(record);
        return eos_sync_status_error(status, "condition.lock");
    }
    for (waiter = record->head; waiter != NULL; waiter = waiter->next) {
        if (waiter->selected == 0) {
            waiter->selected = UINT32_C(1);
            if (eos_port_semaphore_give(waiter->semaphore) != 0) {
                eos_rust_abort();
            }
            if (broadcast == 0) break;
        }
    }
    if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
    eos_condition_release_operation(record);
    return 0;
}

int32_t eos_rust_pthread_cond_signal(eos_rust_pthread_cond *condition) {
    return eos_condition_signal_common(condition, UINT32_C(0));
}

int32_t eos_rust_pthread_cond_broadcast(eos_rust_pthread_cond *condition) {
    return eos_condition_signal_common(condition, UINT32_C(1));
}

static void eos_condition_remove_waiter_locked(eos_condition_record *record,
                                               eos_condition_waiter *waiter) {
    eos_condition_waiter **link = &record->head;
    while (*link != NULL && *link != waiter) link = &(*link)->next;
    if (*link != waiter || waiter->listed == 0 || record->waiter_count == 0) {
        eos_rust_abort();
    }
    *link = waiter->next;
    if (record->tail == waiter) {
        eos_condition_waiter *tail = record->head;
        while (tail != NULL && tail->next != NULL) tail = tail->next;
        record->tail = tail;
    }
    waiter->listed = 0;
    --record->waiter_count;
}

static int32_t eos_condition_wait_common(
    eos_rust_pthread_cond *condition, eos_rust_pthread_mutex *mutex,
    const eos_rust_timespec *absolute_deadline) {
    eos_condition_record *record = NULL;
    eos_condition_waiter *waiter = NULL;
    int32_t status;
    int32_t wait_result;
    if (absolute_deadline != NULL &&
        !eos_time_timespec_valid(absolute_deadline)) {
        return EOS_ERRNO_INVALID;
    }
    status = eos_condition_acquire(condition, UINT32_C(1), &record);
    if (status != 0) return status;
    status = eos_port_memory_alloc((uint32_t)sizeof(*waiter),
                                   (void **)&waiter);
    if (status != 0 || waiter == NULL) {
        eos_condition_release_operation(record);
        return eos_sync_status_error(status, "condition.waiter.allocate");
    }
    (void)memset(waiter, 0, sizeof(*waiter));
    status = eos_port_semaphore_create(UINT32_C(1), UINT32_C(0),
                                       &waiter->semaphore);
    if (status != 0) {
        eos_sync_free(waiter);
        eos_condition_release_operation(record);
        return eos_sync_status_error(status, "condition.waiter.create");
    }
    status = eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER);
    if (status != 0) {
        if (eos_port_semaphore_destroy(waiter->semaphore) != 0) eos_rust_abort();
        eos_sync_free(waiter);
        eos_condition_release_operation(record);
        return eos_sync_status_error(status, "condition.lock");
    }
    waiter->listed = UINT32_C(1);
    if (record->tail == NULL) {
        record->head = waiter;
    } else {
        record->tail->next = waiter;
    }
    record->tail = waiter;
    ++record->waiter_count;
    if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();

    status = eos_rust_pthread_mutex_unlock(mutex);
    if (status != 0) {
        if (eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER) != 0) {
            eos_rust_abort();
        }
        eos_condition_remove_waiter_locked(record, waiter);
        if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
        if (eos_port_semaphore_destroy(waiter->semaphore) != 0) eos_rust_abort();
        eos_sync_free(waiter);
        eos_condition_release_operation(record);
        return status;
    }

    if (absolute_deadline == NULL) {
        wait_result = eos_port_semaphore_take(waiter->semaphore,
                                              EOS_PORT_WAIT_FOREVER);
        if (wait_result != 0) {
            wait_result = eos_sync_status_error(wait_result,
                                                "condition.wait");
        }
    } else {
        wait_result = eos_time_wait_semaphore_until(waiter->semaphore,
                                                    absolute_deadline);
    }

#ifdef EOS_RUST_HOST_TEST
    if (wait_result == EOS_ERRNO_TIMED_OUT &&
        atomic_load(&eos_condition_test_timeout_pause) != 0) {
        atomic_store(&eos_condition_test_timeout_entered, UINT32_C(1));
        while (atomic_load(&eos_condition_test_timeout_release) == 0) {}
    }
#endif

    if (eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER) != 0) {
        eos_rust_abort();
    }
    if (waiter->listed != 0) eos_condition_remove_waiter_locked(record, waiter);
    if (waiter->selected != 0 && wait_result == EOS_ERRNO_TIMED_OUT) {
        /* Signal selection wins a timeout race. Consume its queued token. */
        if (eos_port_semaphore_take(waiter->semaphore, EOS_PORT_NO_WAIT) == 0) {
            wait_result = 0;
        }
    }
    if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
    if (eos_port_semaphore_destroy(waiter->semaphore) != 0) eos_rust_abort();
    eos_sync_free(waiter);
    eos_condition_release_operation(record);

    status = eos_rust_pthread_mutex_lock(mutex);
    if (status != 0) {
        eos_port_direct_diagnostic(
            "libeos_rust_abi: condition failed to reacquire user mutex\n");
        eos_rust_abort();
    }
    return wait_result;
}

int32_t eos_rust_pthread_cond_wait(eos_rust_pthread_cond *condition,
                                   eos_rust_pthread_mutex *mutex) {
    return eos_condition_wait_common(condition, mutex, NULL);
}

int32_t eos_rust_pthread_cond_timedwait(
    eos_rust_pthread_cond *condition, eos_rust_pthread_mutex *mutex,
    const eos_rust_timespec *absolute_deadline) {
    return eos_condition_wait_common(condition, mutex, absolute_deadline);
}

int32_t eos_rust_pthread_cond_destroy(eos_rust_pthread_cond *condition) {
    eos_condition_record *record;
    eos_condition_record **link;
    int32_t status;
    if (condition == NULL) return EOS_ERRNO_INVALID;
    status = eos_sync_registry_lock();
    if (status != 0) return status;
    if (eos_sync_words_zero(condition->words, UINT32_C(4))) {
        eos_sync_registry_unlock();
        return 0;
    }
    if (condition->words[1] != EOS_SYNC_COND_MAGIC ||
        condition->words[2] != 0 || condition->words[3] != 0 ||
        (record = eos_condition_find_locked(condition->words[0])) == NULL) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_INVALID;
    }
    if (record->active_operations != 0 || record->waiter_count != 0) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_BUSY;
    }
    link = &eos_condition_records;
    while (*link != record) link = &(*link)->next;
    *link = record->next;
    condition->words[0] = EOS_SYNC_OBJECT_DEAD;
    condition->words[1] = EOS_SYNC_COND_MAGIC;
    condition->words[2] = 0;
    condition->words[3] = 0;
    eos_sync_registry_unlock();
    if (eos_port_mutex_destroy(record->lock) != 0) eos_rust_abort();
    eos_sync_free(record);
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_sync_test_live_conditions(void) {
    uint32_t count = 0;
    eos_condition_record *record;
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    for (record = eos_condition_records; record != NULL;
         record = record->next) {
        ++count;
    }
    eos_sync_registry_unlock();
    return count;
}
#endif
