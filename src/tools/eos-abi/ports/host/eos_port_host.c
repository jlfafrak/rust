#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Thread_local int32_t eos_host_errno;

static int32_t *eos_port_errno_location(void) {
    return &eos_host_errno;
}

static int32_t eos_port_memory_alloc(uint32_t byte_count, void **memory) {
    void *allocated = malloc((size_t)byte_count);
    if (allocated == NULL) {
        *memory = NULL;
        return 15;
    }
    *memory = allocated;
    return 0;
}

static int32_t eos_port_memory_alloc_aligned(uint32_t byte_count,
                                             uint32_t alignment,
                                             void **memory) {
    void *allocated;
    if (alignment <= (uint32_t)_Alignof(max_align_t)) {
        allocated = malloc((size_t)byte_count);
    } else {
        const size_t rounded =
            ((size_t)byte_count + (size_t)alignment - 1U) &
            ~((size_t)alignment - 1U);
        allocated = aligned_alloc((size_t)alignment, rounded);
    }
    if (allocated == NULL) {
        *memory = NULL;
        return 15;
    }
    *memory = allocated;
    return 0;
}

static int32_t eos_port_memory_realloc(uint32_t byte_count, void **memory) {
    void *resized = realloc(*memory, (size_t)byte_count);
    if (resized == NULL) {
        return 15;
    }
    *memory = resized;
    return 0;
}

static int32_t eos_port_memory_free(void *memory) {
    free(memory);
    return 0;
}

/* The host backend starts with an empty controlled environment. */
static int32_t eos_port_environment_get(const char *name,
                                        char *value,
                                        uint32_t value_capacity) {
    (void)name;
    (void)value;
    (void)value_capacity;
    return 12;
}

static int32_t eos_port_environment_set(const char *name, const char *value) {
    (void)name;
    (void)value;
    return 0;
}

static int32_t eos_port_environment_unset(const char *name) {
    (void)name;
    return 0;
}

static uint64_t eos_port_hash_bytes(const char *text) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    while (*text != '\0') {
        hash ^= (uint8_t)*text;
        hash *= UINT64_C(0x100000001b3);
        ++text;
    }
    return hash;
}

static void eos_port_hash_seed_sources(eos_hash_seed_sources *sources) {
    struct timespec now = {0, 0};
    unsigned char *heap_marker = malloc(1U);
    uint64_t stack_marker = 0;
    static const uint64_t code_marker = UINT64_C(0x454f532d484f5354);

    (void)timespec_get(&now, TIME_UTC);
    sources->timer_usec =
        (uint64_t)now.tv_sec * UINT64_C(1000000) +
        (uint64_t)now.tv_nsec / UINT64_C(1000);
    sources->tick_count = (uint64_t)clock();
    sources->application_id = (uint64_t)(uintptr_t)&code_marker;
    sources->application_name_hash = eos_port_hash_bytes("eos-abi-host");
    sources->thread_identity = (uint64_t)(uintptr_t)&stack_marker;
    sources->code_address = (uint64_t)(uintptr_t)&code_marker;
    sources->heap_address = (uint64_t)(uintptr_t)heap_marker;
    sources->stack_address = (uint64_t)(uintptr_t)&stack_marker;
    sources->device_diversifier = UINT64_C(0x7a796e712d686f73);
    sources->application_diversifier = UINT64_C(0x656f732d72757374);
    free(heap_marker);
}
