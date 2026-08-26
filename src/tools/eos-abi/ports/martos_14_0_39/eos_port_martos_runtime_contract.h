#ifndef EOS_PORT_MARTOS_RUNTIME_CONTRACT_H
#define EOS_PORT_MARTOS_RUNTIME_CONTRACT_H

_Static_assert(OS_MAX_CORE_CNT == UINT32_C(2),
               "supported EOS processors must have two Cortex-A9 cores");

static inline uint32_t eos_martos_cpu_count_native(void) {
    return (uint32_t)os_core_get_count();
}

#endif
