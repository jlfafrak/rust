#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int32_t os_status;
typedef struct eos_fake_mutex os_mutex;
typedef struct eos_fake_sem os_sem;

#define OS_STS_OK INT32_C(0)
#define OS_NO_WAIT UINT32_C(0)
#define OS_WAIT_FOREVER UINT32_MAX
#define OS_MAX_DELAY UINT32_MAX
#define os_tick_rate_hz() UINT32_C(1000)

static os_mutex *eos_fake_mutex_value = (os_mutex *)(uintptr_t)0x1100;
static os_sem *eos_fake_sem_value = (os_sem *)(uintptr_t)0x2200;
static uint32_t eos_fake_normal_create_calls;
static uint32_t eos_fake_recursive_create_calls;
static os_mutex *eos_fake_mutex_argument;
static uint32_t eos_fake_mutex_timeout;
static uint32_t eos_fake_mutex_unlock_calls;
static uint32_t eos_fake_mutex_delete_calls;
static uint32_t eos_fake_sem_maximum;
static uint32_t eos_fake_sem_initial;
static os_sem *eos_fake_sem_argument;
static uint32_t eos_fake_sem_timeout;
static uint32_t eos_fake_sem_give_calls;
static uint32_t eos_fake_sem_delete_calls;
static uint32_t eos_fake_utc_null_age;
static uint32_t eos_fake_delay_ticks;
static uint32_t eos_fake_delay_usec_argument;

static os_status eos_fake_mutex_create(os_mutex **mutex) {
    ++eos_fake_normal_create_calls;
    *mutex = eos_fake_mutex_value;
    return OS_STS_OK;
}

static os_status eos_fake_mutex_recursive_create(os_mutex **mutex) {
    ++eos_fake_recursive_create_calls;
    *mutex = eos_fake_mutex_value;
    return OS_STS_OK;
}

static os_status eos_fake_mutex_lock(os_mutex *mutex, uint32 timeout) {
    eos_fake_mutex_argument = mutex;
    eos_fake_mutex_timeout = timeout;
    return OS_STS_OK;
}

static os_status eos_fake_mutex_unlock(os_mutex *mutex) {
    eos_fake_mutex_argument = mutex;
    ++eos_fake_mutex_unlock_calls;
    return OS_STS_OK;
}

static os_status eos_fake_mutex_delete(os_mutex *mutex) {
    eos_fake_mutex_argument = mutex;
    ++eos_fake_mutex_delete_calls;
    return OS_STS_OK;
}

static os_status eos_fake_sem_counting_create(os_sem **sem, uint32 maximum,
                                               uint32 initial) {
    eos_fake_sem_maximum = maximum;
    eos_fake_sem_initial = initial;
    *sem = eos_fake_sem_value;
    return OS_STS_OK;
}

static os_status eos_fake_sem_take(os_sem *sem, uint32 timeout) {
    eos_fake_sem_argument = sem;
    eos_fake_sem_timeout = timeout;
    return OS_STS_OK;
}

static os_status eos_fake_sem_give(os_sem *sem) {
    eos_fake_sem_argument = sem;
    ++eos_fake_sem_give_calls;
    return OS_STS_OK;
}

static os_status eos_fake_sem_delete(os_sem *sem) {
    eos_fake_sem_argument = sem;
    ++eos_fake_sem_delete_calls;
    return OS_STS_OK;
}

static uint64 eos_fake_timer_get_usec(void) {
    return UINT64_C(0x123456789abcdef0);
}

static os_status eos_fake_utc_get_usec(uint64 *usec, uint32 *age) {
    *usec = UINT64_C(0x2222333344445555);
    eos_fake_utc_null_age = age == NULL;
    return OS_STS_OK;
}

static os_status eos_fake_delay(uint32 ticks) {
    eos_fake_delay_ticks = ticks;
    return OS_STS_OK;
}

static os_status eos_fake_delay_usec(uint32 usec) {
    eos_fake_delay_usec_argument = usec;
    return OS_STS_OK;
}

#define os_mutex_create eos_fake_mutex_create
#define os_mutex_recursive_create eos_fake_mutex_recursive_create
#define os_mutex_lock eos_fake_mutex_lock
#define os_mutex_unlock eos_fake_mutex_unlock
#define os_mutex_delete eos_fake_mutex_delete
#define os_sem_counting_create eos_fake_sem_counting_create
#define os_sem_take eos_fake_sem_take
#define os_sem_give eos_fake_sem_give
#define os_sem_delete eos_fake_sem_delete
#define os_timer_get_usec eos_fake_timer_get_usec
#define os_utc_get_usec eos_fake_utc_get_usec
#define os_delay eos_fake_delay
#define os_delay_usec eos_fake_delay_usec
#include "eos_port_martos_sync_contract.h"

static int expect(int condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    (void)fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

int main(void) {
    os_mutex *mutex = NULL;
    os_sem *sem = NULL;
    uint64 realtime = 0;
    if (expect(OS_NO_WAIT == 0 && OS_WAIT_FOREVER == UINT32_MAX &&
                   OS_MAX_DELAY == UINT32_MAX &&
                   eos_martos_tick_rate_native() == UINT32_C(1000),
               "MARTOS timeout constants drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_mutex_create_native(0, &mutex) == OS_STS_OK &&
                   eos_fake_normal_create_calls == 1 &&
                   eos_fake_recursive_create_calls == 0 &&
                   mutex == eos_fake_mutex_value,
               "normal mutex mapping drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_mutex_create_native(1, &mutex) == OS_STS_OK &&
                   eos_fake_recursive_create_calls == 1,
               "recursive mutex mapping drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_mutex_lock_native(mutex, UINT32_MAX - 1) ==
                   OS_STS_OK &&
                   eos_fake_mutex_argument == mutex &&
                   eos_fake_mutex_timeout == UINT32_MAX - 1 &&
                   eos_martos_mutex_unlock_native(mutex) == OS_STS_OK &&
                   eos_martos_mutex_delete_native(mutex) == OS_STS_OK &&
                   eos_fake_mutex_unlock_calls == 1 &&
                   eos_fake_mutex_delete_calls == 1,
               "mutex operation mapping drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_semaphore_create_native(UINT32_MAX, 0, &sem) ==
                   OS_STS_OK &&
                   sem == eos_fake_sem_value &&
                   eos_fake_sem_maximum == UINT32_MAX &&
                   eos_fake_sem_initial == 0 &&
                   eos_martos_semaphore_take_native(sem, UINT32_MAX - 1) ==
                       OS_STS_OK &&
                   eos_fake_sem_argument == sem &&
                   eos_fake_sem_timeout == UINT32_MAX - 1 &&
                   eos_martos_semaphore_give_native(sem) == OS_STS_OK &&
                   eos_martos_semaphore_delete_native(sem) == OS_STS_OK &&
                   eos_fake_sem_give_calls == 1 &&
                   eos_fake_sem_delete_calls == 1,
               "counting semaphore mapping drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_monotonic_usec_native() ==
                   UINT64_C(0x123456789abcdef0) &&
                   eos_martos_realtime_usec_native(&realtime) == OS_STS_OK &&
                   realtime == UINT64_C(0x2222333344445555) &&
                   eos_fake_utc_null_age != 0,
               "clock source mapping drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return expect(eos_martos_delay_ticks_native(UINT32_MAX - 1) == OS_STS_OK &&
                      eos_martos_delay_usec_native(UINT32_C(777)) ==
                          OS_STS_OK &&
                      eos_fake_delay_ticks == UINT32_MAX - 1 &&
                      eos_fake_delay_usec_argument == UINT32_C(777),
                  "delay mapping drifted");
}
