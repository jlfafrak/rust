#ifndef EOS_PORT_H
#define EOS_PORT_H

#include <stdint.h>

typedef struct eos_hash_seed_sources {
    uint64_t timer_usec;
    uint64_t tick_count;
    uint64_t application_id;
    uint64_t application_name_hash;
    uint64_t thread_identity;
    uint64_t code_address;
    uint64_t heap_address;
    uint64_t stack_address;
    uint64_t device_diversifier;
    uint64_t application_diversifier;
} eos_hash_seed_sources;

#define EOS_PORT_ENV_VALUE_CAPACITY UINT32_C(128)

/* Implemented with internal linkage by the one C source selected by CMake. */
static int32_t *eos_port_errno_location(void);
static int32_t eos_port_memory_alloc(uint32_t byte_count, void **memory);
static int32_t eos_port_memory_alloc_aligned(uint32_t byte_count,
                                             uint32_t alignment,
                                             void **memory);
static int32_t eos_port_memory_realloc(uint32_t byte_count, void **memory);
static int32_t eos_port_memory_free(void *memory);
static int32_t eos_port_environment_get(const char *name,
                                        char *value,
                                        uint32_t value_capacity);
static int32_t eos_port_environment_set(const char *name, const char *value);
static int32_t eos_port_environment_unset(const char *name);
static void eos_port_hash_seed_sources(eos_hash_seed_sources *sources);

#endif
