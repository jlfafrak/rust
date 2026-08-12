#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

static _Thread_local int32_t eos_host_errno;
static pthread_mutex_t eos_host_locks[EOS_PORT_LOCK_COUNT] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};

#ifdef EOS_RUST_HOST_TEST
static int32_t eos_host_next_alloc_status;
static int32_t eos_host_next_aligned_status;
static int32_t eos_host_next_realloc_status;
static int32_t eos_host_next_lock_status;
static int32_t eos_host_next_unlock_status;
static int32_t eos_host_next_environment_set_status;
static int32_t eos_host_next_environment_unset_status;
static int eos_host_native_environment_present;
static char eos_host_native_environment_name[64];
static char eos_host_native_environment_value[EOS_PORT_ENV_VALUE_CAPACITY];
static int32_t eos_host_console_failure[3];
static _Atomic uint32_t eos_host_console_open_count[3];

void eos_host_test_reset(void) {
    eos_host_next_alloc_status = 0;
    eos_host_next_aligned_status = 0;
    eos_host_next_realloc_status = 0;
    eos_host_next_lock_status = 0;
    eos_host_next_unlock_status = 0;
    eos_host_next_environment_set_status = 0;
    eos_host_next_environment_unset_status = 0;
    eos_host_native_environment_present = 0;
    for (uint32_t stream = 0; stream < UINT32_C(3); ++stream) {
        eos_host_console_failure[stream] = 0;
        atomic_store_explicit(&eos_host_console_open_count[stream],
                              UINT32_C(0), memory_order_relaxed);
    }
}
void eos_host_test_fail_next_alloc(int32_t status) { eos_host_next_alloc_status = status; }
void eos_host_test_fail_next_aligned_alloc(int32_t status) { eos_host_next_aligned_status = status; }
void eos_host_test_fail_next_realloc(int32_t status) { eos_host_next_realloc_status = status; }
void eos_host_test_fail_next_lock(int32_t status) { eos_host_next_lock_status = status; }
void eos_host_test_fail_next_unlock(int32_t status) { eos_host_next_unlock_status = status; }
void eos_host_test_fail_next_environment_set(int32_t status) { eos_host_next_environment_set_status = status; }
void eos_host_test_fail_next_environment_unset(int32_t status) { eos_host_next_environment_unset_status = status; }
void eos_host_test_fail_console(uint32_t stream, int32_t status) {
    if (stream < UINT32_C(3)) eos_host_console_failure[stream] = status;
}
uint32_t eos_host_test_console_open_count(uint32_t stream) {
    if (stream >= UINT32_C(3)) return UINT32_C(0);
    return atomic_load_explicit(&eos_host_console_open_count[stream],
                                memory_order_relaxed);
}
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
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_unlock_status != 0) {
        int32_t status = eos_host_next_unlock_status;
        eos_host_next_unlock_status = 0;
        return status;
    }
#endif
    if (lock_id >= EOS_PORT_LOCK_COUNT) return 1;
    return pthread_mutex_unlock(&eos_host_locks[lock_id]) == 0 ? 0 : 20;
}

static int32_t eos_port_console_establish(uint32_t stream,
                                          uintptr_t *native_console) {
    int32_t status;
    if (stream >= UINT32_C(3) || native_console == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    status = eos_host_console_failure[stream];
    if (status != 0) {
        eos_host_console_failure[stream] = 0;
        return status;
    }
#else
    status = 0;
#endif
    *native_console = (uintptr_t)stream;
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add_explicit(&eos_host_console_open_count[stream],
                                    UINT32_C(1), memory_order_relaxed);
#endif
    return status;
}

static void eos_port_console_release(uintptr_t native_console) {
    (void)native_console;
}

static void eos_port_direct_diagnostic(const char *message) {
    size_t length = strlen(message);
    while (length != 0U) {
        ssize_t written = write(STDERR_FILENO, message, length);
        if (written <= 0) return;
        message += (size_t)written;
        length -= (size_t)written;
    }
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
