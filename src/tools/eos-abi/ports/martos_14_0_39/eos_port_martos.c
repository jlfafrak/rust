#include "martos_smp.h"

#ifndef EOS_LIBC_ERRNO_HEADER
#error "EOS_LIBC_ERRNO_HEADER must identify the pinned EOS libc errno.h"
#endif

#include EOS_LIBC_ERRNO_HEADER
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

/* Guard the stable errno ABI against drift in the pinned EOS libc. */
_Static_assert(ENOENT == EOS_ERRNO_NO_ENTRY, "unexpected EOS ENOENT value");
_Static_assert(EIO == EOS_ERRNO_IO, "unexpected EOS EIO value");
_Static_assert(ENOMEM == EOS_ERRNO_NO_MEMORY, "unexpected EOS ENOMEM value");
_Static_assert(EACCES == EOS_ERRNO_ACCESS, "unexpected EOS EACCES value");
_Static_assert(EBUSY == EOS_ERRNO_BUSY, "unexpected EOS EBUSY value");
_Static_assert(EEXIST == EOS_ERRNO_EXISTS, "unexpected EOS EEXIST value");
_Static_assert(EINVAL == EOS_ERRNO_INVALID, "unexpected EOS EINVAL value");
_Static_assert(EROFS == EOS_ERRNO_READ_ONLY_FS, "unexpected EOS EROFS value");
_Static_assert(EWOULDBLOCK == EOS_ERRNO_WOULD_BLOCK,
               "unexpected EOS EWOULDBLOCK value");
_Static_assert(EPROTONOSUPPORT == EOS_ERRNO_PROTOCOL_NOT_SUPPORTED,
               "unexpected EOS EPROTONOSUPPORT value");
_Static_assert(ENOTSUP == EOS_ERRNO_NOT_SUPPORTED, "unexpected EOS ENOTSUP value");
_Static_assert(ETIMEDOUT == EOS_ERRNO_TIMED_OUT, "unexpected EOS ETIMEDOUT value");

/* Guard the numeric translation table against drift in the pinned SDK. */
_Static_assert(OS_STS_OK == 0, "unexpected OS_STS_OK value");
_Static_assert(OS_STS_INVALID_PARAM1 == 1, "unexpected OS_STS_INVALID_PARAM1 value");
_Static_assert(OS_STS_INVALID_PARAM2 == 2, "unexpected OS_STS_INVALID_PARAM2 value");
_Static_assert(OS_STS_INVALID_PARAM3 == 3, "unexpected OS_STS_INVALID_PARAM3 value");
_Static_assert(OS_STS_INVALID_PARAM4 == 4, "unexpected OS_STS_INVALID_PARAM4 value");
_Static_assert(OS_STS_INVALID_PARAM5 == 5, "unexpected OS_STS_INVALID_PARAM5 value");
_Static_assert(OS_STS_INVALID_PARAM6 == 6, "unexpected OS_STS_INVALID_PARAM6 value");
_Static_assert(OS_STS_INVALID_PARAM7 == 7, "unexpected OS_STS_INVALID_PARAM7 value");
_Static_assert(OS_STS_INVALID_PARAM8 == 8, "unexpected OS_STS_INVALID_PARAM8 value");
_Static_assert(OS_STS_INVALID_PARAM9 == 9, "unexpected OS_STS_INVALID_PARAM9 value");
_Static_assert(OS_STS_INVALID_PARAM10 == 10, "unexpected OS_STS_INVALID_PARAM10 value");
_Static_assert(OS_STS_INVALID_OBJECT_TYPE == 11,
               "unexpected OS_STS_INVALID_OBJECT_TYPE value");
_Static_assert(OS_STS_OBJECT_NOT_FOUND == 12,
               "unexpected OS_STS_OBJECT_NOT_FOUND value");
_Static_assert(OS_STS_OBJECT_EXISTS == 13, "unexpected OS_STS_OBJECT_EXISTS value");
_Static_assert(OS_STS_NOT_CALLABLE_FROM_ISR == 14,
               "unexpected OS_STS_NOT_CALLABLE_FROM_ISR value");
_Static_assert(OS_STS_ALLOC_ERROR == 15, "unexpected OS_STS_ALLOC_ERROR value");
_Static_assert(OS_STS_INSUFFICIENT_ACL == 16,
               "unexpected OS_STS_INSUFFICIENT_ACL value");
_Static_assert(OS_STS_OBJECT_IN_USE == 17, "unexpected OS_STS_OBJECT_IN_USE value");
_Static_assert(OS_STS_OBJECT_IS_READ_ONLY == 18,
               "unexpected OS_STS_OBJECT_IS_READ_ONLY value");
_Static_assert(OS_STS_TIMEOUT_EXPIRED == 19,
               "unexpected OS_STS_TIMEOUT_EXPIRED value");
_Static_assert(OS_STS_MUTEX_WAS_NOT_LOCKED == 20,
               "unexpected OS_STS_MUTEX_WAS_NOT_LOCKED value");
_Static_assert(OS_STS_WOULD_BLOCK_FROM_ISR == 21,
               "unexpected OS_STS_WOULD_BLOCK_FROM_ISR value");
_Static_assert(OS_STS_OBJECT_WAS_NOT_TAKEN == 22,
               "unexpected OS_STS_OBJECT_WAS_NOT_TAKEN value");
_Static_assert(OS_STS_MEM_MISALIGNMENT == 23,
               "unexpected OS_STS_MEM_MISALIGNMENT value");
_Static_assert(OS_STS_SYSTEM_NOT_INITIALIZED == 24,
               "unexpected OS_STS_SYSTEM_NOT_INITIALIZED value");
_Static_assert(OS_STS_DEVICE_ERROR == 25, "unexpected OS_STS_DEVICE_ERROR value");
_Static_assert(OS_STS_DEVICE_READ_ERROR == 26,
               "unexpected OS_STS_DEVICE_READ_ERROR value");
_Static_assert(OS_STS_DEVICE_WRITE_ERROR == 27,
               "unexpected OS_STS_DEVICE_WRITE_ERROR value");
_Static_assert(OS_STS_DEVICE_ERASE_ERROR == 28,
               "unexpected OS_STS_DEVICE_ERASE_ERROR value");
_Static_assert(OS_STS_PARTITION_ERROR == 29,
               "unexpected OS_STS_PARTITION_ERROR value");
_Static_assert(OS_STS_INVALID_HASH == 30, "unexpected OS_STS_INVALID_HASH value");
_Static_assert(OS_STS_THREAD_NOT_STARTED == 31,
               "unexpected OS_STS_THREAD_NOT_STARTED value");
_Static_assert(OS_STS_END_OF_OBJECT == 32, "unexpected OS_STS_END_OF_OBJECT value");
_Static_assert(OS_STS_SYMBOL_ERROR == 33, "unexpected OS_STS_SYMBOL_ERROR value");
_Static_assert(OS_STS_PARSE_ERROR == 34, "unexpected OS_STS_PARSE_ERROR value");
_Static_assert(OS_STS_COUNT == 35, "unexpected OS_STS_COUNT value");

/*
 * This bootstrap cell keeps the MARTOS-native dependency inside this port.
 * Task 7 replaces it with the reserved EOS user-TLS slot 7 adapter.
 */
static int32_t eos_martos_bootstrap_errno;

static int32_t *eos_port_errno_location(void) {
    return &eos_martos_bootstrap_errno;
}

static _Atomic(os_mutex *) eos_martos_locks[EOS_PORT_LOCK_COUNT];

static int32_t eos_port_lock_acquire(uint32_t lock_id) {
    os_mutex *lock;
    if (lock_id >= EOS_PORT_LOCK_COUNT) return OS_STS_INVALID_PARAM1;
    lock = atomic_load_explicit(&eos_martos_locks[lock_id],
                                memory_order_acquire);
    if (lock == NULL) {
        os_mutex *candidate = NULL;
        os_mutex *expected = NULL;
        int32_t status = (int32_t)os_mutex_create(&candidate);
        if (status != OS_STS_OK) return status;
        if (atomic_compare_exchange_strong_explicit(
                &eos_martos_locks[lock_id], &expected, candidate,
                memory_order_acq_rel, memory_order_acquire)) {
            lock = candidate;
        } else {
            status = (int32_t)os_mutex_delete(candidate);
            if (status != OS_STS_OK) return status;
            lock = expected;
        }
    }
    return (int32_t)os_mutex_lock(lock, OS_WAIT_FOREVER);
}

static int32_t eos_port_lock_release(uint32_t lock_id) {
    os_mutex *lock;
    if (lock_id >= EOS_PORT_LOCK_COUNT) return OS_STS_INVALID_PARAM1;
    lock = atomic_load_explicit(&eos_martos_locks[lock_id],
                                memory_order_acquire);
    if (lock == NULL) return OS_STS_INVALID_PARAM1;
    return (int32_t)os_mutex_unlock(lock);
}

static int32_t eos_port_memory_alloc(uint32_t byte_count, void **memory) {
    return (int32_t)os_mem_alloc(OS_MEM_NORMAL_CACHEABLE_DATA,
                                 (uint32)byte_count,
                                 memory);
}

static int32_t eos_port_memory_alloc_aligned(uint32_t byte_count,
                                             uint32_t alignment,
                                             void **memory) {
    if (alignment <= OS_MEM_HEAP_MINIMUM_ALIGNMENT) {
        return (int32_t)os_mem_alloc(OS_MEM_NORMAL_CACHEABLE_DATA,
                                     (uint32)byte_count,
                                     memory);
    }
    return (int32_t)os_mem_alloc_aligned(OS_MEM_NORMAL_CACHEABLE_DATA,
                                         (uint32)byte_count,
                                         (uint32)alignment,
                                         memory);
}

static int32_t eos_port_memory_realloc(uint32_t byte_count, void **memory) {
    return (int32_t)os_mem_realloc((uint32)byte_count, memory);
}

static int32_t eos_port_memory_free(void *memory) {
    return (int32_t)os_mem_free(memory);
}

static int32_t eos_port_environment_get(const char *name,
                                        char *value,
                                        uint32_t value_capacity) {
    return (int32_t)os_system_env_get_string(name,
                                              value,
                                              (uint32)value_capacity);
}

static int32_t eos_port_environment_set(const char *name, const char *value) {
    return (int32_t)os_system_env_set_string(name, value);
}

static int32_t eos_port_environment_unset(const char *name) {
    return (int32_t)os_system_env_unset(name);
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
    uint32 app_id = 0;
    char app_name[OS_NAME_LEN] = {0};
    char target_name[OS_ENV_VAR_VALUE_LEN] = {0};
    os_thread *thread = NULL;
    os_app_info app_info;
    void *heap_marker = NULL;
    uint64_t stack_marker = 0;

    (void)memset(&app_info, 0, sizeof(app_info));
    (void)os_app_get_id(&app_id);
    (void)os_app_get_name(app_id, app_name, (uint32)sizeof(app_name));
    (void)os_system_env_get_string(OS_TARGET_NAME_ENV_VAR,
                                   target_name,
                                   (uint32)sizeof(target_name));
    (void)os_thread_get_current(&thread);
    (void)os_app_get_info(app_id, &app_info);
    (void)os_mem_alloc(OS_MEM_NORMAL_CACHEABLE_DATA, 1U, &heap_marker);

    sources->timer_usec = (uint64_t)os_timer_get_usec();
    sources->tick_count = (uint64_t)os_tick_get_count();
    sources->application_id = (uint64_t)app_id;
    sources->application_name_hash = eos_port_hash_bytes(app_name);
    sources->thread_identity = (uint64_t)(uintptr_t)thread;
    sources->code_address = (uint64_t)app_info.codeBaseAddr;
    sources->heap_address = (uint64_t)(uintptr_t)heap_marker;
    sources->stack_address = (uint64_t)(uintptr_t)&stack_marker;
    sources->device_diversifier =
        eos_port_hash_bytes(target_name) ^ UINT64_C(0x7a796e712d646576);
    sources->application_diversifier =
        eos_port_hash_bytes(app_name) ^ ((uint64_t)app_id << 32) ^
        UINT64_C(0x656f732d72757374);

    if (heap_marker != NULL) {
        (void)os_mem_free(heap_marker);
    }
}
