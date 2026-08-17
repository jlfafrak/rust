#ifndef EOS_PORT_MARTOS_SYNC_CONTRACT_H
#define EOS_PORT_MARTOS_SYNC_CONTRACT_H

/* Exact synchronization/time mapping isolated for deterministic fakes. */
static inline int32_t eos_martos_mutex_create_native(uint32_t recursive,
                                                     os_mutex **mutex) {
    return recursive != 0 ? (int32_t)os_mutex_recursive_create(mutex)
                          : (int32_t)os_mutex_create(mutex);
}

static inline int32_t eos_martos_mutex_lock_native(os_mutex *mutex,
                                                   uint32_t timeout_ticks) {
    return (int32_t)os_mutex_lock(mutex, (uint32)timeout_ticks);
}

static inline int32_t eos_martos_mutex_unlock_native(os_mutex *mutex) {
    return (int32_t)os_mutex_unlock(mutex);
}

static inline int32_t eos_martos_mutex_delete_native(os_mutex *mutex) {
    return (int32_t)os_mutex_delete(mutex);
}

static inline int32_t eos_martos_semaphore_create_native(
    uint32_t maximum_count, uint32_t initial_count, os_sem **semaphore) {
    return (int32_t)os_sem_counting_create(
        semaphore, (uint32)maximum_count, (uint32)initial_count);
}

static inline int32_t eos_martos_semaphore_take_native(
    os_sem *semaphore, uint32_t timeout_ticks) {
    return (int32_t)os_sem_take(semaphore, (uint32)timeout_ticks);
}

static inline int32_t eos_martos_semaphore_give_native(os_sem *semaphore) {
    return (int32_t)os_sem_give(semaphore);
}

static inline int32_t eos_martos_semaphore_delete_native(os_sem *semaphore) {
    return (int32_t)os_sem_delete(semaphore);
}

static inline uint64_t eos_martos_monotonic_usec_native(void) {
    return (uint64_t)os_timer_get_usec();
}

static inline int32_t eos_martos_realtime_usec_native(uint64_t *usec) {
    return (int32_t)os_utc_get_usec((uint64 *)usec, NULL);
}

static inline uint32_t eos_martos_tick_rate_native(void) {
    return (uint32_t)os_tick_rate_hz();
}

static inline int32_t eos_martos_delay_ticks_native(uint32_t ticks) {
    return (int32_t)os_delay((uint32)ticks);
}

static inline int32_t eos_martos_delay_usec_native(uint32_t usec) {
    return (int32_t)os_delay_usec((uint32)usec);
}

#endif
