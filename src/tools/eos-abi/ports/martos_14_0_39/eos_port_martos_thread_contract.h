#ifndef EOS_PORT_MARTOS_THREAD_CONTRACT_H
#define EOS_PORT_MARTOS_THREAD_CONTRACT_H

/*
 * Exact MARTOS thread/TLS mapping isolated for deterministic contract tests.
 * Normal-return completion is compatibility-owned: the kernel auto-reaps its
 * os_thread record, so no native pointer, wait, delete, or termination handler
 * is retained by this adapter.
 */
static inline int32_t eos_martos_thread_create_native(
    const char *name,
    eos_port_thread_start start,
    void *argument,
    uint32_t stack_size) {
    return (int32_t)os_thread_create(
        NULL, name, OS_THREAD_DEFAULT_FEATURES, (os_thread_function)start,
        argument, (uint32)stack_size, OS_PRIO_DEFAULT, NULL);
}

static inline int32_t eos_martos_thread_tls_get_native(uint32_t slot,
                                                        uintptr_t *value) {
    return (int32_t)os_thread_get_user_tls(NULL, (uint32)slot, value);
}

static inline int32_t eos_martos_thread_tls_set_native(uint32_t slot,
                                                        uintptr_t value) {
    return (int32_t)os_thread_set_user_tls(NULL, (uint32)slot, value);
}

#endif
