#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t uint32;
typedef int32_t os_status;
typedef struct eos_fake_thread os_thread;
typedef void (*os_thread_function)(void *);
typedef void (*eos_port_thread_start)(void *);

#define OS_STS_OK INT32_C(0)
#define OS_THREAD_DEFAULT_FEATURES UINT32_C(0)
#define OS_PRIO_DEFAULT UINT32_C(200)

static os_thread **eos_fake_created_output;
static const char *eos_fake_created_name;
static uint32 eos_fake_created_features;
static os_thread_function eos_fake_created_start;
static void *eos_fake_created_argument;
static uint32 eos_fake_created_stack;
static uint32 eos_fake_created_priority;
static void *eos_fake_created_termination;
static os_thread *eos_fake_tls_get_thread;
static uint32 eos_fake_tls_get_slot;
static os_thread *eos_fake_tls_set_thread;
static uint32 eos_fake_tls_set_slot;
static uintptr_t eos_fake_tls_set_value;
static uint32_t eos_fake_wait_calls;
static uint32_t eos_fake_delete_calls;
static uint32_t eos_fake_delay_calls;
static int eos_fake_create_returned;
static int eos_fake_start_before_create_return;

static os_status eos_fake_thread_create(
    os_thread **output,
    const char *name,
    uint32 features,
    os_thread_function start,
    void *argument,
    uint32 stack_size,
    uint32 priority,
    void *termination) {
    eos_fake_created_output = output;
    eos_fake_created_name = name;
    eos_fake_created_features = features;
    eos_fake_created_start = start;
    eos_fake_created_argument = argument;
    eos_fake_created_stack = stack_size;
    eos_fake_created_priority = priority;
    eos_fake_created_termination = termination;
    start(argument);
    return OS_STS_OK;
}

static os_status eos_fake_thread_get_user_tls(os_thread *thread,
                                               uint32 slot,
                                               uintptr_t *value) {
    eos_fake_tls_get_thread = thread;
    eos_fake_tls_get_slot = slot;
    *value = UINT32_C(0x1234);
    return OS_STS_OK;
}

static os_status eos_fake_thread_set_user_tls(os_thread *thread,
                                               uint32 slot,
                                               uintptr_t value) {
    eos_fake_tls_set_thread = thread;
    eos_fake_tls_set_slot = slot;
    eos_fake_tls_set_value = value;
    return OS_STS_OK;
}

static os_status eos_fake_thread_wait(void) {
    ++eos_fake_wait_calls;
    return OS_STS_OK;
}

static os_status eos_fake_thread_delete(void) {
    ++eos_fake_delete_calls;
    return OS_STS_OK;
}

static os_status eos_fake_delay(void) {
    ++eos_fake_delay_calls;
    return OS_STS_OK;
}

#define os_thread_create eos_fake_thread_create
#define os_thread_get_user_tls eos_fake_thread_get_user_tls
#define os_thread_set_user_tls eos_fake_thread_set_user_tls
#define os_thread_wait eos_fake_thread_wait
#define os_thread_delete eos_fake_thread_delete
#define os_delay eos_fake_delay
#include "eos_port_martos_thread_contract.h"

static void eos_fake_start(void *argument) {
    if (!eos_fake_create_returned &&
        argument == (void *)(uintptr_t)UINT32_C(0x5678)) {
        eos_fake_start_before_create_return = 1;
    }
}

static int expect(int condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    (void)fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

int main(void) {
    uintptr_t observed = 0;
    void *const argument = (void *)(uintptr_t)UINT32_C(0x5678);
    (void)eos_fake_thread_wait;
    (void)eos_fake_thread_delete;
    (void)eos_fake_delay;
    if (expect(eos_martos_thread_create_native(
                   "eos.rust", eos_fake_start, argument, UINT32_C(8192)) ==
                   OS_STS_OK &&
                   eos_fake_created_output == NULL &&
                   strcmp(eos_fake_created_name, "eos.rust") == 0 &&
                   eos_fake_created_features == OS_THREAD_DEFAULT_FEATURES &&
                   eos_fake_created_start == eos_fake_start &&
                   eos_fake_created_argument == argument &&
                   eos_fake_created_stack == UINT32_C(8192) &&
                   eos_fake_created_priority == OS_PRIO_DEFAULT &&
                   eos_fake_created_termination == NULL &&
                   eos_fake_start_before_create_return != 0,
               "MARTOS thread creation arguments drifted") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_create_returned = 1;
    if (expect(eos_martos_thread_tls_get_native(UINT32_C(7), &observed) ==
                   OS_STS_OK &&
                   eos_fake_tls_get_thread == NULL &&
                   eos_fake_tls_get_slot == UINT32_C(7) &&
                   observed == UINT32_C(0x1234),
               "MARTOS TLS get must target current thread slot 7") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (expect(eos_martos_thread_tls_set_native(
                   UINT32_C(7), (uintptr_t)UINT32_C(0xabcd)) == OS_STS_OK &&
                   eos_fake_tls_set_thread == NULL &&
                   eos_fake_tls_set_slot == UINT32_C(7) &&
                   eos_fake_tls_set_value == (uintptr_t)UINT32_C(0xabcd),
               "MARTOS TLS set must target current thread slot 7") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return expect(eos_fake_wait_calls == 0 && eos_fake_delete_calls == 0 &&
                      eos_fake_delay_calls == 0,
                  "compatibility mapping must not wait/delete/yield natively");
}
