#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static _Thread_local int32_t eos_host_errno;
static pthread_mutex_t eos_host_locks[EOS_PORT_LOCK_COUNT] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};

#ifdef EOS_RUST_HOST_TEST
static int32_t eos_host_next_alloc_status;
static int32_t eos_host_next_aligned_status;
static int32_t eos_host_next_realloc_status;
static int32_t eos_host_next_lock_status;
static int32_t eos_host_next_environment_set_status;
static int32_t eos_host_next_environment_unset_status;
static int eos_host_native_environment_present;
static char eos_host_native_environment_name[64];
static char eos_host_native_environment_value[EOS_PORT_ENV_VALUE_CAPACITY];

void eos_host_test_reset(void) {
    eos_host_next_alloc_status = 0;
    eos_host_next_aligned_status = 0;
    eos_host_next_realloc_status = 0;
    eos_host_next_lock_status = 0;
    eos_host_next_environment_set_status = 0;
    eos_host_next_environment_unset_status = 0;
    eos_host_native_environment_present = 0;
}
void eos_host_test_fail_next_alloc(int32_t status) { eos_host_next_alloc_status = status; }
void eos_host_test_fail_next_aligned_alloc(int32_t status) { eos_host_next_aligned_status = status; }
void eos_host_test_fail_next_realloc(int32_t status) { eos_host_next_realloc_status = status; }
void eos_host_test_fail_next_lock(int32_t status) { eos_host_next_lock_status = status; }
void eos_host_test_fail_next_environment_set(int32_t status) { eos_host_next_environment_set_status = status; }
void eos_host_test_fail_next_environment_unset(int32_t status) { eos_host_next_environment_unset_status = status; }
void eos_host_test_native_environment(const char *name, const char *value) {
    (void)strncpy(eos_host_native_environment_name, name,
                  sizeof(eos_host_native_environment_name) - 1U);
    eos_host_native_environment_name[sizeof(eos_host_native_environment_name) - 1U] = '\0';
    (void)strncpy(eos_host_native_environment_value, value,
                  sizeof(eos_host_native_environment_value) - 1U);
    eos_host_native_environment_value[sizeof(eos_host_native_environment_value) - 1U] = '\0';
    eos_host_native_environment_present = 1;
}
#endif

static int32_t *eos_port_errno_location(void) {
    return &eos_host_errno;
}

static int32_t eos_port_memory_alloc(uint32_t byte_count, void **memory) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_alloc_status != 0) {
        int32_t status = eos_host_next_alloc_status;
        eos_host_next_alloc_status = 0;
        *memory = NULL;
        return status;
    }
#endif
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
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_aligned_status != 0) {
        int32_t status = eos_host_next_aligned_status;
        eos_host_next_aligned_status = 0;
        *memory = NULL;
        return status;
    }
#endif
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
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_realloc_status != 0) {
        int32_t status = eos_host_next_realloc_status;
        eos_host_next_realloc_status = 0;
        return status;
    }
#endif
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
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_native_environment_present &&
        strcmp(name, eos_host_native_environment_name) == 0) {
        (void)strncpy(value, eos_host_native_environment_value,
                      (size_t)value_capacity - 1U);
        value[value_capacity - UINT32_C(1)] = '\0';
        return 0;
    }
#else
    (void)name; (void)value; (void)value_capacity;
#endif
    return 12;
}

static int32_t eos_port_environment_set(const char *name, const char *value) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_environment_set_status != 0) {
        int32_t status = eos_host_next_environment_set_status;
        eos_host_next_environment_set_status = 0;
        return status;
    }
    eos_host_test_native_environment(name, value);
#else
    (void)name;
    (void)value;
#endif
    return 0;
}

static int32_t eos_port_environment_unset(const char *name) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_environment_unset_status != 0) {
        int32_t status = eos_host_next_environment_unset_status;
        eos_host_next_environment_unset_status = 0;
        return status;
    }
    if (eos_host_native_environment_present &&
        strcmp(name, eos_host_native_environment_name) == 0) {
        eos_host_native_environment_present = 0;
    }
#else
    (void)name;
#endif
    return 0;
}

static int32_t eos_port_lock_acquire(uint32_t lock_id) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_lock_status != 0) {
        int32_t status = eos_host_next_lock_status;
        eos_host_next_lock_status = 0;
        return status;
    }
#endif
    if (lock_id >= EOS_PORT_LOCK_COUNT) return 1;
    return pthread_mutex_lock(&eos_host_locks[lock_id]) == 0 ? 0 : 17;
}

static int32_t eos_port_lock_release(uint32_t lock_id) {
    if (lock_id >= EOS_PORT_LOCK_COUNT) return 1;
    return pthread_mutex_unlock(&eos_host_locks[lock_id]) == 0 ? 0 : 20;
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
