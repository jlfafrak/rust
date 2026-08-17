#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define EOS_SYNC_RWLOCK_MAGIC UINT32_C(0x45535231)

typedef struct eos_rwlock_record {
    uint32_t identity;
    uint32_t active_operations;
    uint32_t readers;
    uint32_t waiting_readers;
    uint32_t waiting_writers;
    uint32_t writer_active;
    eos_rust_thread_t writer_owner;
    eos_port_mutex lock;
    eos_port_semaphore readers_sem;
    eos_port_semaphore writers_sem;
    struct eos_rwlock_record *next;
} eos_rwlock_record;

static eos_rwlock_record *eos_rwlock_records;

#ifdef EOS_RUST_HOST_TEST
static _Atomic uint32_t eos_rwlock_test_writer_wait_entered;

void eos_sync_test_reset_rwlock_wait_audit(void) {
    atomic_store(&eos_rwlock_test_writer_wait_entered, UINT32_C(0));
}

uint32_t eos_sync_test_rwlock_writer_wait_entered(void) {
    return atomic_load(&eos_rwlock_test_writer_wait_entered);
}
#endif

_Static_assert(sizeof(eos_rust_pthread_rwlock) == 16,
               "rwlock ABI size changed");
_Static_assert(_Alignof(eos_rust_pthread_rwlock) == _Alignof(uint32_t),
               "rwlock ABI alignment changed");
_Static_assert(offsetof(eos_rust_pthread_rwlock, words) == 0,
               "rwlock ABI offset changed");

static eos_rwlock_record *eos_rwlock_find_locked(uint32_t identity) {
    eos_rwlock_record *record = eos_rwlock_records;
    while (record != NULL && record->identity != identity) {
        record = record->next;
    }
    return record;
}

static int32_t eos_rwlock_allocate(eos_rwlock_record **output) {
    eos_rwlock_record *record = NULL;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*record),
                                           (void **)&record);
    if (status != 0 || record == NULL) {
        return eos_sync_status_error(status, "rwlock.allocate");
    }
    (void)memset(record, 0, sizeof(*record));
    status = eos_port_mutex_create(UINT32_C(0), &record->lock);
    if (status == 0) {
        status = eos_port_semaphore_create(UINT32_MAX, UINT32_C(0),
                                           &record->readers_sem);
    }
    if (status == 0) {
        status = eos_port_semaphore_create(UINT32_C(1), UINT32_C(0),
                                           &record->writers_sem);
    }
    if (status != 0) {
        if (record->readers_sem != 0 &&
            eos_port_semaphore_destroy(record->readers_sem) != 0) {
            eos_rust_abort();
        }
        if (record->lock != 0 && eos_port_mutex_destroy(record->lock) != 0) {
            eos_rust_abort();
        }
        eos_sync_free(record);
        return eos_sync_status_error(status, "rwlock.create");
    }
    *output = record;
    return 0;
}

static void eos_rwlock_discard(eos_rwlock_record *record) {
    if (eos_port_semaphore_destroy(record->writers_sem) != 0 ||
        eos_port_semaphore_destroy(record->readers_sem) != 0 ||
        eos_port_mutex_destroy(record->lock) != 0) {
        eos_rust_abort();
    }
    eos_sync_free(record);
}

static int32_t eos_rwlock_acquire(eos_rust_pthread_rwlock *rwlock,
                                  uint32_t lazy,
                                  eos_rwlock_record **output) {
    eos_rwlock_record *candidate = NULL;
    int32_t status;
    if (rwlock == NULL || output == NULL) return EOS_ERRNO_INVALID;
    for (;;) {
        status = eos_sync_registry_lock();
        if (status != 0) return status;
        if (!eos_sync_words_zero(rwlock->words, UINT32_C(4))) {
            eos_rwlock_record *record;
            if (rwlock->words[1] != EOS_SYNC_RWLOCK_MAGIC ||
                rwlock->words[2] != 0 || rwlock->words[3] != 0 ||
                (record = eos_rwlock_find_locked(rwlock->words[0])) == NULL) {
                status = EOS_ERRNO_INVALID;
            } else {
                ++record->active_operations;
                *output = record;
                status = 0;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_rwlock_discard(candidate);
            return status;
        }
        if (lazy == 0) {
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_rwlock_discard(candidate);
            return EOS_ERRNO_INVALID;
        }
        if (candidate != NULL) {
            status = eos_sync_issue_identity_locked(&candidate->identity);
            if (status == 0) {
                candidate->next = eos_rwlock_records;
                eos_rwlock_records = candidate;
                rwlock->words[0] = candidate->identity;
                rwlock->words[1] = EOS_SYNC_RWLOCK_MAGIC;
                rwlock->words[2] = 0;
                rwlock->words[3] = 0;
                ++candidate->active_operations;
                *output = candidate;
                candidate = NULL;
            }
            eos_sync_registry_unlock();
            if (candidate != NULL) eos_rwlock_discard(candidate);
            return status;
        }
        eos_sync_registry_unlock();
        status = eos_rwlock_allocate(&candidate);
        if (status != 0) return status;
    }
}

static void eos_rwlock_release_operation(eos_rwlock_record *record) {
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    if (record->active_operations == 0) eos_rust_abort();
    --record->active_operations;
    eos_sync_registry_unlock();
}

int32_t eos_rust_pthread_rwlock_init(eos_rust_pthread_rwlock *rwlock) {
    eos_rwlock_record *record;
    int32_t status;
    if (rwlock == NULL) return EOS_ERRNO_INVALID;
    status = eos_rwlock_allocate(&record);
    if (status != 0) return status;
    status = eos_sync_registry_lock();
    if (status != 0) {
        eos_rwlock_discard(record);
        return status;
    }
    if (!eos_sync_words_zero(rwlock->words, UINT32_C(4))) {
        status = rwlock->words[1] == EOS_SYNC_RWLOCK_MAGIC &&
                         rwlock->words[2] == 0 && rwlock->words[3] == 0 &&
                         eos_rwlock_find_locked(rwlock->words[0]) != NULL
                     ? EOS_ERRNO_BUSY
                     : EOS_ERRNO_INVALID;
        eos_sync_registry_unlock();
        eos_rwlock_discard(record);
        return status;
    }
    status = eos_sync_issue_identity_locked(&record->identity);
    if (status == 0) {
        record->next = eos_rwlock_records;
        eos_rwlock_records = record;
        rwlock->words[0] = record->identity;
        rwlock->words[1] = EOS_SYNC_RWLOCK_MAGIC;
        rwlock->words[2] = 0;
        rwlock->words[3] = 0;
        record = NULL;
    }
    eos_sync_registry_unlock();
    if (record != NULL) eos_rwlock_discard(record);
    return status;
}

static int32_t eos_rwlock_lock_common(eos_rust_pthread_rwlock *rwlock,
                                      uint32_t write,
                                      uint32_t try_only) {
    eos_rwlock_record *record;
    int32_t status = eos_rwlock_acquire(rwlock, UINT32_C(1), &record);
    uint32_t wait = 0;
    if (status != 0) return status;
    status = eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER);
    if (status != 0) {
        eos_rwlock_release_operation(record);
        return eos_sync_status_error(status, "rwlock.lock");
    }
    if (write != 0) {
        if (record->writer_active == 0 && record->readers == 0) {
            record->writer_active = UINT32_C(1);
            record->writer_owner = eos_rust_pthread_self();
        } else if (try_only != 0) {
            status = EOS_ERRNO_BUSY;
        } else {
            ++record->waiting_writers;
            wait = UINT32_C(1);
        }
    } else if (record->writer_active == 0 && record->waiting_writers == 0) {
        ++record->readers;
    } else if (try_only != 0) {
        status = EOS_ERRNO_BUSY;
    } else {
        ++record->waiting_readers;
        wait = UINT32_C(1);
    }
    if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
    if (wait != 0) {
#ifdef EOS_RUST_HOST_TEST
        if (write != 0) {
            atomic_store(&eos_rwlock_test_writer_wait_entered, UINT32_C(1));
        }
#endif
        status = eos_port_semaphore_take(
            write != 0 ? record->writers_sem : record->readers_sem,
            EOS_PORT_WAIT_FOREVER);
        if (status != 0) {
            if (eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER) != 0) {
                eos_rust_abort();
            }
            if (write != 0) {
                if (record->waiting_writers == 0) eos_rust_abort();
                --record->waiting_writers;
            } else {
                if (record->waiting_readers == 0) eos_rust_abort();
                --record->waiting_readers;
            }
            if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
            status = eos_sync_status_error(status, "rwlock.wait");
        }
        if (status == 0 && write != 0) {
            if (eos_port_mutex_lock(record->lock,
                                    EOS_PORT_WAIT_FOREVER) != 0) {
                eos_rust_abort();
            }
            if (record->writer_active == 0 || record->writer_owner != 0) {
                eos_rust_abort();
            }
            record->writer_owner = eos_rust_pthread_self();
            if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
        }
    }
    eos_rwlock_release_operation(record);
    return status;
}

int32_t eos_rust_pthread_rwlock_rdlock(eos_rust_pthread_rwlock *rwlock) {
    return eos_rwlock_lock_common(rwlock, UINT32_C(0), UINT32_C(0));
}

int32_t eos_rust_pthread_rwlock_tryrdlock(eos_rust_pthread_rwlock *rwlock) {
    return eos_rwlock_lock_common(rwlock, UINT32_C(0), UINT32_C(1));
}

int32_t eos_rust_pthread_rwlock_wrlock(eos_rust_pthread_rwlock *rwlock) {
    return eos_rwlock_lock_common(rwlock, UINT32_C(1), UINT32_C(0));
}

int32_t eos_rust_pthread_rwlock_trywrlock(eos_rust_pthread_rwlock *rwlock) {
    return eos_rwlock_lock_common(rwlock, UINT32_C(1), UINT32_C(1));
}

int32_t eos_rust_pthread_rwlock_unlock(eos_rust_pthread_rwlock *rwlock) {
    eos_rwlock_record *record;
    uint32_t wake_readers = 0;
    int32_t status = eos_rwlock_acquire(rwlock, UINT32_C(0), &record);
    if (status != 0) return status;
    status = eos_port_mutex_lock(record->lock, EOS_PORT_WAIT_FOREVER);
    if (status != 0) {
        eos_rwlock_release_operation(record);
        return eos_sync_status_error(status, "rwlock.unlock.lock");
    }
    if (record->writer_active != 0) {
        if (record->writer_owner != eos_rust_pthread_self()) {
            status = EOS_ERRNO_INVALID;
        } else {
            record->writer_active = 0;
            record->writer_owner = 0;
        }
    } else if (record->readers != 0) {
        --record->readers;
    } else {
        status = EOS_ERRNO_INVALID;
    }
    if (status == 0 && record->writer_active == 0 && record->readers == 0) {
        if (record->waiting_writers != 0) {
            --record->waiting_writers;
            record->writer_active = UINT32_C(1);
            record->writer_owner = 0; /* assigned by the woken writer below */
            if (eos_port_semaphore_give(record->writers_sem) != 0) {
                eos_rust_abort();
            }
        } else if (record->waiting_readers != 0) {
            wake_readers = record->waiting_readers;
            record->readers += wake_readers;
            record->waiting_readers = 0;
            while (wake_readers-- != 0) {
                if (eos_port_semaphore_give(record->readers_sem) != 0) {
                    eos_rust_abort();
                }
            }
        }
    }
    if (eos_port_mutex_unlock(record->lock) != 0) eos_rust_abort();
    eos_rwlock_release_operation(record);
    return status;
}

int32_t eos_rust_pthread_rwlock_destroy(eos_rust_pthread_rwlock *rwlock) {
    eos_rwlock_record *record;
    eos_rwlock_record **link;
    int32_t status;
    if (rwlock == NULL) return EOS_ERRNO_INVALID;
    status = eos_sync_registry_lock();
    if (status != 0) return status;
    if (eos_sync_words_zero(rwlock->words, UINT32_C(4))) {
        eos_sync_registry_unlock();
        return 0;
    }
    if (rwlock->words[1] != EOS_SYNC_RWLOCK_MAGIC || rwlock->words[2] != 0 ||
        rwlock->words[3] != 0 ||
        (record = eos_rwlock_find_locked(rwlock->words[0])) == NULL) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_INVALID;
    }
    if (record->active_operations != 0 || record->readers != 0 ||
        record->waiting_readers != 0 || record->waiting_writers != 0 ||
        record->writer_active != 0) {
        eos_sync_registry_unlock();
        return EOS_ERRNO_BUSY;
    }
    link = &eos_rwlock_records;
    while (*link != record) link = &(*link)->next;
    *link = record->next;
    rwlock->words[0] = EOS_SYNC_OBJECT_DEAD;
    rwlock->words[1] = EOS_SYNC_RWLOCK_MAGIC;
    rwlock->words[2] = 0;
    rwlock->words[3] = 0;
    eos_sync_registry_unlock();
    eos_rwlock_discard(record);
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_sync_test_live_rwlocks(void) {
    uint32_t count = 0;
    eos_rwlock_record *record;
    if (eos_sync_registry_lock() != 0) eos_rust_abort();
    for (record = eos_rwlock_records; record != NULL; record = record->next) {
        ++count;
    }
    eos_sync_registry_unlock();
    return count;
}
#endif
