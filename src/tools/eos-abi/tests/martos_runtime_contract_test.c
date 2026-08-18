#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t uint32;

#define OS_MAX_CORE_CNT UINT32_C(2)

static uint32_t eos_fake_core_count = UINT32_C(2);
static uint32_t eos_fake_core_count_calls;

static uint32 eos_fake_core_get_count(void) {
    ++eos_fake_core_count_calls;
    return eos_fake_core_count;
}

#define os_core_get_count eos_fake_core_get_count
#include "eos_port_martos_runtime_contract.h"

static int expect(int condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    (void)fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

int main(void) {
    return expect(eos_martos_cpu_count_native() == UINT32_C(2) &&
                      eos_fake_core_count_calls == UINT32_C(1),
                  "MARTOS CPU count must call os_core_get_count exactly once");
}
