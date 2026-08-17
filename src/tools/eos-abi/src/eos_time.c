#include <stdint.h>

static uint64_t eos_time_last_monotonic_usec;

static int32_t eos_time_status_error(int32_t status, const char *operation) {
    eos_error_result mapped = eos_error_from_port_status_impl(status, operation);
    return mapped.kind == EOS_ERROR_ERRNO ? mapped.error_number : EOS_ERRNO_IO;
}

static int32_t eos_time_monotonic_usec(uint64_t *usec) {
    uint64_t observed = 0;
    int32_t status;
    if (usec == NULL) return EOS_ERRNO_INVALID;
    status = eos_port_lock_acquire(EOS_PORT_LOCK_TIME);
    if (status != 0) return eos_time_status_error(status, "time.lock");
    status = eos_port_monotonic_usec(&observed);
    if (status == 0) {
        if (observed < eos_time_last_monotonic_usec) {
            observed = eos_time_last_monotonic_usec;
        } else {
            eos_time_last_monotonic_usec = observed;
        }
        *usec = observed;
    }
    if (eos_port_lock_release(EOS_PORT_LOCK_TIME) != 0) {
        eos_port_direct_diagnostic("libeos_rust_abi: time lock release failed\n");
        eos_rust_abort();
    }
    return status == 0 ? 0 : eos_time_status_error(status, "time.monotonic");
}

static int eos_time_timespec_valid(const eos_rust_timespec *time) {
    return time != NULL && time->tv_sec >= 0 && time->tv_nsec >= 0 &&
           time->tv_nsec < INT64_C(1000000000);
}

static void eos_time_usec_to_timespec(uint64_t usec,
                                      eos_rust_timespec *time) {
    time->tv_sec = (int64_t)(usec / UINT64_C(1000000));
    time->tv_nsec = (int64_t)((usec % UINT64_C(1000000)) * UINT64_C(1000));
}

int32_t eos_rust_clock_gettime(int32_t clock_id, eos_rust_timespec *time) {
    eos_rust_timespec converted;
    uint64_t usec = 0;
    int32_t status;
    if (time == NULL) {
        *eos_tls_errno_location() = EOS_ERRNO_INVALID;
        return -1;
    }
    if (clock_id == EOS_RUST_CLOCK_MONOTONIC) {
        status = eos_time_monotonic_usec(&usec);
    } else if (clock_id == EOS_RUST_CLOCK_REALTIME) {
        status = eos_port_realtime_usec(&usec);
        if (status != 0) {
            status = eos_time_status_error(status, "time.realtime");
        }
    } else {
        status = EOS_ERRNO_INVALID;
    }
    if (status != 0) {
        *eos_tls_errno_location() = status;
        return -1;
    }
    eos_time_usec_to_timespec(usec, &converted);
    *time = converted;
    return 0;
}

static uint32_t eos_time_deadline_ticks_internal(
    uint64_t now_usec, const eos_rust_timespec *deadline,
    uint32_t ticks_per_second, int32_t *expired) {
    uint64_t now_seconds;
    int64_t now_nanoseconds;
    uint64_t seconds;
    uint64_t nanoseconds;
    uint64_t ticks;
    uint64_t fractional_ticks;
    if (expired == NULL) return 0;
    *expired = -1;
    if (!eos_time_timespec_valid(deadline) || ticks_per_second == 0) return 0;
    now_seconds = now_usec / UINT64_C(1000000);
    now_nanoseconds =
        (int64_t)((now_usec % UINT64_C(1000000)) * UINT64_C(1000));
    if ((uint64_t)deadline->tv_sec < now_seconds ||
        ((uint64_t)deadline->tv_sec == now_seconds &&
         deadline->tv_nsec <= now_nanoseconds)) {
        *expired = 1;
        return 0;
    }
    seconds = (uint64_t)deadline->tv_sec - now_seconds;
    if (deadline->tv_nsec < now_nanoseconds) {
        --seconds;
        nanoseconds = UINT64_C(1000000000) +
                      (uint64_t)deadline->tv_nsec -
                      (uint64_t)now_nanoseconds;
    } else {
        nanoseconds = (uint64_t)(deadline->tv_nsec - now_nanoseconds);
    }
    *expired = 0;
    if (seconds > (uint64_t)EOS_PORT_MAX_FINITE_WAIT / ticks_per_second) {
        return EOS_PORT_MAX_FINITE_WAIT;
    }
    ticks = seconds * ticks_per_second;
    fractional_ticks =
        (nanoseconds * ticks_per_second + UINT64_C(999999999)) /
        UINT64_C(1000000000);
    if (fractional_ticks > EOS_PORT_MAX_FINITE_WAIT - ticks) {
        return EOS_PORT_MAX_FINITE_WAIT;
    }
    ticks += fractional_ticks;
    if (ticks == 0) ticks = 1;
    return (uint32_t)ticks;
}

static int32_t eos_time_wait_semaphore_until(
    eos_port_semaphore semaphore, const eos_rust_timespec *deadline) {
    uint32_t tick_rate = eos_port_tick_rate_hz();
    if (!eos_time_timespec_valid(deadline) || tick_rate == 0) {
        return EOS_ERRNO_INVALID;
    }
    for (;;) {
        uint64_t now_usec = 0;
        uint32_t ticks;
        int32_t expired;
        int32_t status = eos_time_monotonic_usec(&now_usec);
        if (status != 0) return status;
        ticks = eos_time_deadline_ticks_internal(now_usec, deadline, tick_rate,
                                                 &expired);
        if (expired < 0) return EOS_ERRNO_INVALID;
        if (expired != 0) return EOS_ERRNO_TIMED_OUT;
        status = eos_port_semaphore_take(semaphore, ticks);
        if (status == 0) return 0;
        if (status != 19) return eos_time_status_error(status, "time.wait");
    }
}

static void eos_time_remaining(uint64_t deadline_usec,
                               eos_rust_timespec *remaining) {
    uint64_t now_usec = deadline_usec;
    if (remaining == NULL) return;
    if (eos_time_monotonic_usec(&now_usec) != 0 || now_usec >= deadline_usec) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
        return;
    }
    eos_time_usec_to_timespec(deadline_usec - now_usec, remaining);
}

int32_t eos_rust_nanosleep(const eos_rust_timespec *requested,
                           eos_rust_timespec *remaining) {
    uint64_t start_usec;
    uint64_t requested_usec;
    uint64_t deadline_usec;
    int32_t status;
    if (!eos_time_timespec_valid(requested)) {
        *eos_tls_errno_location() = EOS_ERRNO_INVALID;
        return -1;
    }
    if ((uint64_t)requested->tv_sec >
        UINT64_MAX / UINT64_C(1000000)) {
        *eos_tls_errno_location() = EOS_ERRNO_OVERFLOW;
        return -1;
    }
    requested_usec = (uint64_t)requested->tv_sec * UINT64_C(1000000) +
                     ((uint64_t)requested->tv_nsec + UINT64_C(999)) /
                         UINT64_C(1000);
    status = eos_time_monotonic_usec(&start_usec);
    if (status != 0) {
        *eos_tls_errno_location() = status;
        return -1;
    }
    if (requested_usec > UINT64_MAX - start_usec) {
        *eos_tls_errno_location() = EOS_ERRNO_OVERFLOW;
        return -1;
    }
    deadline_usec = start_usec + requested_usec;
    while (start_usec < deadline_usec) {
        uint64_t usec = deadline_usec - start_usec;
        if (usec >= UINT64_C(1000)) {
            uint64_t ticks = usec / UINT64_C(1000);
            if (ticks > EOS_PORT_MAX_FINITE_WAIT) {
                ticks = EOS_PORT_MAX_FINITE_WAIT;
            }
            status = eos_port_delay_ticks((uint32_t)ticks);
        } else {
            status = eos_port_delay_usec((uint32_t)usec);
        }
        if (status != 0) {
            eos_time_remaining(deadline_usec, remaining);
            *eos_tls_errno_location() =
                eos_time_status_error(status, "time.sleep");
            return -1;
        }
        status = eos_time_monotonic_usec(&start_usec);
        if (status != 0) {
            eos_time_remaining(deadline_usec, remaining);
            *eos_tls_errno_location() = status;
            return -1;
        }
    }
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
void eos_time_test_reset(void) {
    if (eos_port_lock_acquire(EOS_PORT_LOCK_TIME) != 0) eos_rust_abort();
    eos_time_last_monotonic_usec = 0;
    eos_port_time_test_reset();
    if (eos_port_lock_release(EOS_PORT_LOCK_TIME) != 0) eos_rust_abort();
}

uint32_t eos_time_test_deadline_ticks(uint64_t now_usec,
                                      const eos_rust_timespec *deadline,
                                      uint32_t ticks_per_second,
                                      int32_t *expired) {
    return eos_time_deadline_ticks_internal(now_usec, deadline,
                                            ticks_per_second, expired);
}
#endif
