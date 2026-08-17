#include "martos_smp.h"

#ifndef EOS_LIBC_ERRNO_HEADER
#error "EOS_LIBC_ERRNO_HEADER must identify the pinned EOS libc errno.h"
#endif

#include EOS_LIBC_ERRNO_HEADER
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

/* Authoritative EOS libc/include/stdio.h declarations; kept private here. */
extern int rename(const char *old_name, const char *new_name);
extern int rmdir(const char *filename);
extern int unlink(const char *filename);

/* Guard the stable errno ABI against drift in the pinned EOS libc. */
_Static_assert(ENOENT == EOS_ERRNO_NO_ENTRY, "unexpected EOS ENOENT value");
_Static_assert(EINTR == EOS_ERRNO_INTERRUPTED, "unexpected EOS EINTR value");
_Static_assert(EIO == EOS_ERRNO_IO, "unexpected EOS EIO value");
_Static_assert(EBADF == EOS_ERRNO_BAD_DESCRIPTOR, "unexpected EOS EBADF value");
_Static_assert(ENOMEM == EOS_ERRNO_NO_MEMORY, "unexpected EOS ENOMEM value");
_Static_assert(EACCES == EOS_ERRNO_ACCESS, "unexpected EOS EACCES value");
_Static_assert(EFAULT == EOS_ERRNO_FAULT, "unexpected EOS EFAULT value");
_Static_assert(EBUSY == EOS_ERRNO_BUSY, "unexpected EOS EBUSY value");
_Static_assert(EEXIST == EOS_ERRNO_EXISTS, "unexpected EOS EEXIST value");
_Static_assert(ENOTDIR == EOS_ERRNO_NOT_DIRECTORY,
               "unexpected EOS ENOTDIR value");
_Static_assert(EISDIR == EOS_ERRNO_IS_DIRECTORY,
               "unexpected EOS EISDIR value");
_Static_assert(EINVAL == EOS_ERRNO_INVALID, "unexpected EOS EINVAL value");
_Static_assert(EMFILE == EOS_ERRNO_TOO_MANY_OPEN_FILES,
               "unexpected EOS EMFILE value");
_Static_assert(ENOTTY == EOS_ERRNO_NOT_TTY, "unexpected EOS ENOTTY value");
_Static_assert(ENOSPC == EOS_ERRNO_NO_SPACE, "unexpected EOS ENOSPC value");
_Static_assert(ESPIPE == EOS_ERRNO_ILLEGAL_SEEK,
               "unexpected EOS ESPIPE value");
_Static_assert(EROFS == EOS_ERRNO_READ_ONLY_FS, "unexpected EOS EROFS value");
_Static_assert(EPIPE == EOS_ERRNO_PIPE, "unexpected EOS EPIPE value");
_Static_assert(ERANGE == EOS_ERRNO_RANGE, "unexpected EOS ERANGE value");
_Static_assert(EWOULDBLOCK == EOS_ERRNO_WOULD_BLOCK,
               "unexpected EOS EWOULDBLOCK value");
_Static_assert(EINPROGRESS == EOS_ERRNO_IN_PROGRESS,
               "unexpected EOS EINPROGRESS value");
_Static_assert(EALREADY == EOS_ERRNO_ALREADY,
               "unexpected EOS EALREADY value");
_Static_assert(ENOTSOCK == EOS_ERRNO_NOT_SOCKET,
               "unexpected EOS ENOTSOCK value");
_Static_assert(EDESTADDRREQ == EOS_ERRNO_DESTINATION_REQUIRED,
               "unexpected EOS EDESTADDRREQ value");
_Static_assert(EMSGSIZE == EOS_ERRNO_MESSAGE_SIZE,
               "unexpected EOS EMSGSIZE value");
_Static_assert(EPROTOTYPE == EOS_ERRNO_PROTOCOL_TYPE,
               "unexpected EOS EPROTOTYPE value");
_Static_assert(ENOPROTOOPT == EOS_ERRNO_NO_PROTOCOL_OPTION,
               "unexpected EOS ENOPROTOOPT value");
_Static_assert(EPROTONOSUPPORT == EOS_ERRNO_PROTOCOL_NOT_SUPPORTED,
               "unexpected EOS EPROTONOSUPPORT value");
_Static_assert(ENOTSUP == EOS_ERRNO_NOT_SUPPORTED, "unexpected EOS ENOTSUP value");
_Static_assert(EAFNOSUPPORT == EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED,
               "unexpected EOS EAFNOSUPPORT value");
_Static_assert(EADDRINUSE == EOS_ERRNO_ADDRESS_IN_USE,
               "unexpected EOS EADDRINUSE value");
_Static_assert(EADDRNOTAVAIL == EOS_ERRNO_ADDRESS_NOT_AVAILABLE,
               "unexpected EOS EADDRNOTAVAIL value");
_Static_assert(ENETDOWN == EOS_ERRNO_NETWORK_DOWN,
               "unexpected EOS ENETDOWN value");
_Static_assert(ENETUNREACH == EOS_ERRNO_NETWORK_UNREACHABLE,
               "unexpected EOS ENETUNREACH value");
_Static_assert(ENETRESET == EOS_ERRNO_NETWORK_RESET,
               "unexpected EOS ENETRESET value");
_Static_assert(ECONNABORTED == EOS_ERRNO_CONNECTION_ABORTED,
               "unexpected EOS ECONNABORTED value");
_Static_assert(ECONNRESET == EOS_ERRNO_CONNECTION_RESET,
               "unexpected EOS ECONNRESET value");
_Static_assert(ENOBUFS == EOS_ERRNO_NO_BUFFERS,
               "unexpected EOS ENOBUFS value");
_Static_assert(EISCONN == EOS_ERRNO_IS_CONNECTED,
               "unexpected EOS EISCONN value");
_Static_assert(ENOTCONN == EOS_ERRNO_NOT_CONNECTED,
               "unexpected EOS ENOTCONN value");
_Static_assert(ESHUTDOWN == EOS_ERRNO_SHUTDOWN,
               "unexpected EOS ESHUTDOWN value");
_Static_assert(ETIMEDOUT == EOS_ERRNO_TIMED_OUT, "unexpected EOS ETIMEDOUT value");
_Static_assert(ECONNREFUSED == EOS_ERRNO_CONNECTION_REFUSED,
               "unexpected EOS ECONNREFUSED value");
_Static_assert(ENAMETOOLONG == EOS_ERRNO_NAME_TOO_LONG,
               "unexpected EOS ENAMETOOLONG value");
_Static_assert(EHOSTDOWN == EOS_ERRNO_HOST_DOWN,
               "unexpected EOS EHOSTDOWN value");
_Static_assert(EHOSTUNREACH == EOS_ERRNO_HOST_UNREACHABLE,
               "unexpected EOS EHOSTUNREACH value");
_Static_assert(ENOTEMPTY == EOS_ERRNO_NOT_EMPTY,
               "unexpected EOS ENOTEMPTY value");
_Static_assert(EOVERFLOW == EOS_ERRNO_OVERFLOW,
               "unexpected EOS EOVERFLOW value");
_Static_assert(ESRCH == EOS_ERRNO_NO_PROCESS, "unexpected EOS ESRCH value");
_Static_assert(EDEADLK == EOS_ERRNO_DEADLOCK, "unexpected EOS EDEADLK value");

#include "eos_port_martos_fs_contract.h"
#include "eos_port_martos_thread_contract.h"
#include "eos_port_martos_sync_contract.h"
#include "eos_port_martos_network_contract.h"

/* The SDK convenience macro must not rewrite the stable ABI's field name. */
#ifdef sin_addr
#undef sin_addr
#endif

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

_Static_assert(OS_THREAD_USER_TLS_CNT > EOS_RUST_TLS_SLOT,
               "reserved EOS Rust TLS slot is unavailable");
_Static_assert(OS_THREAD_STACK_DEFAULT_BYTE_CNT == EOS_RUST_PTHREAD_STACK_MIN,
               "MARTOS minimum thread stack changed");
_Static_assert(OS_NAME_LEN == EOS_RUST_PTHREAD_NAME_MAX + 1,
               "MARTOS thread name capacity changed");
_Static_assert(OS_NO_WAIT == EOS_PORT_NO_WAIT,
               "MARTOS no-wait sentinel changed");
_Static_assert(OS_WAIT_FOREVER == EOS_PORT_WAIT_FOREVER,
               "MARTOS wait-forever sentinel changed");
_Static_assert(OS_MAX_DELAY == UINT32_MAX,
               "MARTOS maximum delay changed");
_Static_assert(sizeof(os_timer_get_usec()) == sizeof(uint64_t),
               "MARTOS monotonic timer is no longer 64-bit");

static int32_t eos_port_thread_tls_get(uint32_t slot, uintptr_t *value) {
    return eos_martos_thread_tls_get_native(slot, value);
}

static int32_t eos_port_thread_tls_set(uint32_t slot, uintptr_t value) {
    return eos_martos_thread_tls_set_native(slot, value);
}

static int32_t eos_port_thread_create(const char *name,
                                      eos_port_thread_start start,
                                      void *argument,
                                      uint32_t stack_size) {
    return eos_martos_thread_create_native(name, start, argument, stack_size);
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

static int32_t eos_port_console_establish(uint32_t stream,
                                          uintptr_t *native_console) {
    if (stream >= UINT32_C(3) || native_console == NULL) {
        return OS_STS_INVALID_PARAM1;
    }
    *native_console = (uintptr_t)stream;
    return OS_STS_OK;
}

static void eos_port_console_release(uintptr_t native_console) {
    (void)native_console;
}

static void eos_port_direct_diagnostic(const char *message) {
    while (*message != '\0') {
        (void)os_stdio_isr_output(*message);
        ++message;
    }
}

typedef struct eos_martos_sync {
    os_mutex *mutex;
    os_event_group *events;
} eos_martos_sync;

static int32_t eos_port_sync_create(eos_port_sync *sync) {
    eos_martos_sync *created = NULL;
    int32_t status;
    if (sync == NULL) return OS_STS_INVALID_PARAM1;
    status = (int32_t)os_mem_alloc(OS_MEM_NORMAL_CACHEABLE_DATA,
                                   (uint32)sizeof(*created),
                                   (void **)&created);
    if (status != OS_STS_OK) return status;
    created->mutex = NULL;
    created->events = NULL;
    status = (int32_t)os_mutex_create(&created->mutex);
    if (status == OS_STS_OK) {
        status = (int32_t)os_event_group_create(&created->events);
    }
    if (status != OS_STS_OK) {
        if (created->mutex != NULL) (void)os_mutex_delete(created->mutex);
        (void)os_mem_free(created);
        return status;
    }
    *sync = (eos_port_sync)(uintptr_t)created;
    return OS_STS_OK;
}

static int32_t eos_port_sync_lock(eos_port_sync sync) {
    eos_martos_sync *value = (eos_martos_sync *)(uintptr_t)sync;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : (int32_t)os_mutex_lock(value->mutex, OS_WAIT_FOREVER);
}

static int32_t eos_port_sync_unlock(eos_port_sync sync) {
    eos_martos_sync *value = (eos_martos_sync *)(uintptr_t)sync;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : (int32_t)os_mutex_unlock(value->mutex);
}

static int32_t eos_port_sync_wait(eos_port_sync sync, uint32_t events) {
    eos_martos_sync *value = (eos_martos_sync *)(uintptr_t)sync;
    os_event_bits observed = 0;
    int32_t status;
    if (value == NULL || events == 0) return OS_STS_INVALID_PARAM1;
    status = (int32_t)os_event_group_clear_events(
        value->events, &observed, (os_event_bits)events);
    if (status != OS_STS_OK) return status;
    status = (int32_t)os_mutex_unlock(value->mutex);
    if (status != OS_STS_OK) return status;
    status = (int32_t)os_event_group_wait_for_events(
        value->events, &observed, (os_event_bits)events,
        false, false, OS_WAIT_FOREVER);
    if (os_mutex_lock(value->mutex, OS_WAIT_FOREVER) != OS_STS_OK) {
        eos_rust_abort();
    }
    return status;
}

static int32_t eos_port_sync_broadcast(eos_port_sync sync, uint32_t events) {
    eos_martos_sync *value = (eos_martos_sync *)(uintptr_t)sync;
    os_event_bits observed = 0;
    return value == NULL || events == 0
               ? OS_STS_INVALID_PARAM1
               : (int32_t)os_event_group_set_events(
                     value->events, &observed, (os_event_bits)events);
}

static int32_t eos_port_sync_destroy(eos_port_sync sync) {
    eos_martos_sync *value = (eos_martos_sync *)(uintptr_t)sync;
    int32_t status;
    if (value == NULL) return OS_STS_INVALID_PARAM1;
    status = (int32_t)os_event_group_delete(value->events);
    if (status != OS_STS_OK) return status;
    status = (int32_t)os_mutex_delete(value->mutex);
    if (status != OS_STS_OK) return status;
    return (int32_t)os_mem_free(value);
}

static int32_t eos_port_mutex_create(uint32_t recursive,
                                     eos_port_mutex *mutex) {
    os_mutex *created = NULL;
    int32_t status;
    if (mutex == NULL || recursive > UINT32_C(1)) {
        return OS_STS_INVALID_PARAM1;
    }
    status = eos_martos_mutex_create_native(recursive, &created);
    if (status == OS_STS_OK) *mutex = (eos_port_mutex)(uintptr_t)created;
    return status;
}

static int32_t eos_port_mutex_lock(eos_port_mutex mutex,
                                   uint32_t timeout_ticks) {
    os_mutex *value = (os_mutex *)(uintptr_t)mutex;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_mutex_lock_native(value, timeout_ticks);
}

static int32_t eos_port_mutex_unlock(eos_port_mutex mutex) {
    os_mutex *value = (os_mutex *)(uintptr_t)mutex;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_mutex_unlock_native(value);
}

static int32_t eos_port_mutex_destroy(eos_port_mutex mutex) {
    os_mutex *value = (os_mutex *)(uintptr_t)mutex;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_mutex_delete_native(value);
}

static int32_t eos_port_semaphore_create(uint32_t maximum_count,
                                         uint32_t initial_count,
                                         eos_port_semaphore *semaphore) {
    os_sem *created = NULL;
    int32_t status;
    if (semaphore == NULL || maximum_count == 0 ||
        initial_count > maximum_count) {
        return OS_STS_INVALID_PARAM1;
    }
    status = eos_martos_semaphore_create_native(maximum_count, initial_count,
                                                &created);
    if (status == OS_STS_OK) {
        *semaphore = (eos_port_semaphore)(uintptr_t)created;
    }
    return status;
}

static int32_t eos_port_semaphore_take(eos_port_semaphore semaphore,
                                       uint32_t timeout_ticks) {
    os_sem *value = (os_sem *)(uintptr_t)semaphore;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_semaphore_take_native(value,
                                                            timeout_ticks);
}

static int32_t eos_port_semaphore_give(eos_port_semaphore semaphore) {
    os_sem *value = (os_sem *)(uintptr_t)semaphore;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_semaphore_give_native(value);
}

static int32_t eos_port_semaphore_destroy(eos_port_semaphore semaphore) {
    os_sem *value = (os_sem *)(uintptr_t)semaphore;
    return value == NULL ? OS_STS_INVALID_PARAM1
                         : eos_martos_semaphore_delete_native(value);
}

static int32_t eos_port_monotonic_usec(uint64_t *usec) {
    if (usec == NULL) return OS_STS_INVALID_PARAM1;
    *usec = eos_martos_monotonic_usec_native();
    return OS_STS_OK;
}

static int32_t eos_port_realtime_usec(uint64_t *usec) {
    return usec == NULL ? OS_STS_INVALID_PARAM1
                        : eos_martos_realtime_usec_native(usec);
}

static uint32_t eos_port_tick_rate_hz(void) {
    return eos_martos_tick_rate_native();
}

static int32_t eos_port_delay_ticks(uint32_t ticks) {
    return eos_martos_delay_ticks_native(ticks);
}

static int32_t eos_port_delay_usec(uint32_t usec) {
    return eos_martos_delay_usec_native(usec);
}

static void eos_martos_file_encode(os_efs_file_id native,
                                   eos_port_file *file) {
    (void)memset(file, 0, sizeof(*file));
    _Static_assert(sizeof(native) <= sizeof(*file),
                   "EOS file id no longer fits private storage");
    (void)memcpy(file, &native, sizeof(native));
}

static os_efs_file_id eos_martos_file_decode(eos_port_file file) {
    os_efs_file_id native;
    (void)memcpy(&native, &file, sizeof(native));
    return native;
}

static eos_port_result eos_port_file_open(const char *path, uint32_t flags,
                                          eos_port_file *file) {
    os_efs_file_id native;
    eos_port_result result =
        eos_martos_fs_file_open_native(path, flags, &native);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    eos_martos_file_encode(native, file);
    return result;
}

static int32_t eos_port_file_read(eos_port_file file, void *buffer,
                                  uint32_t byte_count, uint32_t *completed) {
    uint64 count = 0;
    os_status status = os_efs_file_read(eos_martos_file_decode(file),
                                        (uint8 *)buffer, (uint64)byte_count,
                                        &count, OS_WAIT_FOREVER);
    *completed = (uint32)count;
    return (int32_t)status;
}

static int32_t eos_port_file_write(eos_port_file file, const void *buffer,
                                   uint32_t byte_count, uint32_t *completed) {
    uint64 count = 0;
    os_status status = os_efs_file_write(eos_martos_file_decode(file),
                                         (const uint8 *)buffer,
                                         (uint64)byte_count, &count,
                                         OS_WAIT_FOREVER);
    *completed = (uint32)count;
    return (int32_t)status;
}

static int32_t eos_port_file_seek(eos_port_file file, int64_t offset,
                                  int32_t origin) {
    if (origin < EOS_RUST_SEEK_SET || origin > EOS_RUST_SEEK_END) {
        return OS_STS_INVALID_PARAM3;
    }
    return (int32_t)os_efs_file_seek(eos_martos_file_decode(file),
                                     (int64)offset, (os_efs_seek)origin,
                                     OS_WAIT_FOREVER);
}

static int32_t eos_port_file_tell(eos_port_file file, int64_t *offset) {
    int64 native = 0;
    int32_t status = (int32_t)os_efs_file_tell(eos_martos_file_decode(file),
                                               &native, OS_WAIT_FOREVER);
    *offset = (int64_t)native;
    return status;
}

static int32_t eos_port_file_flush(eos_port_file file) {
    return (int32_t)os_efs_file_flush(eos_martos_file_decode(file),
                                      OS_WAIT_FOREVER);
}

static void eos_martos_fill_stat(const os_efs_entry_stats *native,
                                 eos_port_stat *metadata) {
    metadata->entry_id = (uint64_t)native->entryId;
    metadata->byte_count = (uint64_t)native->byteCnt;
    metadata->utc_seconds = (int64_t)native->utc;
    metadata->type = native->type == OS_EFS_DIRECTORY
                         ? EOS_PORT_FILE_TYPE_DIRECTORY
                         : EOS_PORT_FILE_TYPE_REGULAR;
    metadata->read_only = native->permission == OS_EFS_RD_ONLY
                              ? UINT32_C(1)
                              : UINT32_C(0);
}

static int32_t eos_port_path_stat(const char *path, eos_port_stat *metadata) {
    os_efs_entry_stats native;
    int32_t status = (int32_t)os_efs_entry_get_stats(
        path, &native, OS_WAIT_FOREVER);
    if (status == OS_STS_OK) eos_martos_fill_stat(&native, metadata);
    return status;
}

static int32_t eos_port_file_stat(eos_port_file file, eos_port_stat *metadata) {
    char path[OS_EFS_MAX_PATH_LEN];
    int32_t status = (int32_t)os_efs_file_get_absolute_path(
        eos_martos_file_decode(file), path, (uint32)sizeof(path),
        OS_WAIT_FOREVER);
    return status == OS_STS_OK ? eos_port_path_stat(path, metadata) : status;
}

static int32_t eos_port_file_close(eos_port_file file) {
    return (int32_t)os_efs_file_close(eos_martos_file_decode(file),
                                      OS_WAIT_FOREVER);
}

static int32_t eos_port_path_mkdir(const char *path) {
    return (int32_t)os_efs_directory_init(path, OS_WAIT_FOREVER);
}

static eos_port_result eos_port_path_unlink(const char *path) {
    return eos_martos_fs_path_unlink(path);
}

static eos_port_result eos_port_path_rmdir(const char *path) {
    return eos_martos_fs_path_rmdir(path);
}

static eos_port_result eos_port_path_rename(const char *old_path,
                                             const char *new_path) {
    return eos_martos_fs_path_rename(old_path, new_path);
}

static int32_t eos_port_directory_count(const char *path, uint32_t *count) {
    uint32 native = 0;
    int32_t status = (int32_t)os_efs_directory_get_listing_count(
        path, false, &native, OS_WAIT_FOREVER);
    *count = (uint32_t)native;
    return status;
}

static int32_t eos_port_directory_list(const char *path,
                                       eos_port_dir_entry *entries,
                                       uint32_t capacity,
                                       uint32_t *count) {
    os_efs_entry_stats *native = NULL;
    uint32_t index;
    int32_t status;
    if (capacity == 0) {
        *count = 0;
        return OS_STS_OK;
    }
    status = (int32_t)os_mem_alloc(OS_MEM_NORMAL_CACHEABLE_DATA,
                                   (uint32)(capacity * sizeof(*native)),
                                   (void **)&native);
    if (status != OS_STS_OK) return status;
    status = (int32_t)os_efs_directory_get_listing(
        path, false, native, (uint32)capacity, OS_WAIT_FOREVER);
    if (status == OS_STS_OK) {
        for (index = 0; index < capacity; ++index) {
            size_t length = 0;
            while (length < OS_NAME_LEN && native[index].name[length] != '\0') {
                ++length;
            }
            if (length >= sizeof(entries[index].name)) {
                status = OS_STS_INVALID_PARAM1;
                break;
            }
            entries[index].entry_id = (uint64_t)native[index].entryId;
            entries[index].type = native[index].type == OS_EFS_DIRECTORY
                                      ? EOS_PORT_FILE_TYPE_DIRECTORY
                                      : EOS_PORT_FILE_TYPE_REGULAR;
            entries[index].name_length = (uint32_t)length;
            (void)memcpy(entries[index].name, native[index].name, length);
            entries[index].name[length] = '\0';
        }
    }
    if (os_mem_free(native) != OS_STS_OK) eos_rust_abort();
    if (status == OS_STS_OK) *count = capacity;
    return status;
}

static int32_t eos_port_console_read(uintptr_t stream, void *buffer,
                                     uint32_t byte_count,
                                     uint32_t *completed) {
    uint32_t index;
    char *bytes = (char *)buffer;
    if (stream != UINT32_C(0)) return OS_STS_INVALID_PARAM1;
    for (index = 0; index < byte_count; ++index) {
        if (os_stdio_read_byte(&bytes[index], OS_WAIT_FOREVER) != UINT32_C(1)) {
            *completed = index;
            return OS_STS_TIMEOUT_EXPIRED;
        }
    }
    *completed = byte_count;
    return OS_STS_OK;
}

static int32_t eos_port_console_write(uintptr_t stream, const void *buffer,
                                      uint32_t byte_count,
                                      uint32_t *completed) {
    uint32_t index;
    const char *bytes = (const char *)buffer;
    if (stream < UINT32_C(1) || stream > UINT32_C(2)) {
        return OS_STS_INVALID_PARAM1;
    }
    for (index = 0; index < byte_count; ++index) {
        uint32_t written = stream == UINT32_C(1)
                               ? os_stdio_write_byte(bytes[index], OS_WAIT_FOREVER)
                               : os_stdio_write_error_byte(bytes[index],
                                                          OS_WAIT_FOREVER);
        if (written != UINT32_C(1)) {
            *completed = index;
            return OS_STS_TIMEOUT_EXPIRED;
        }
    }
    *completed = byte_count;
    return OS_STS_OK;
}

static int32_t eos_port_hostname(char *name, uint32_t capacity) {
    return (int32_t)os_system_env_get_string(OS_TARGET_NAME_ENV_VAR,
                                              name, (uint32)capacity);
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

static eos_port_socket_create_result eos_port_socket_create(int32_t domain,
                                                            int32_t type,
                                                            int32_t protocol) {
    uint32 native_domain = domain == EOS_RUST_AF_INET
                               ? OS_NET_AF_INET : OS_NET_AF_INET6;
    uint32 native_type = type == EOS_RUST_SOCK_STREAM
                             ? OS_NET_SOCK_STREAM : OS_NET_SOCK_DGRAM;
    uint32 native_protocol = protocol == EOS_RUST_IPPROTO_IP
                                 ? OS_NET_SOCK_DEPENDENT_PROTO
                                 : protocol == EOS_RUST_IPPROTO_TCP
                                       ? OS_NET_IPPROTO_TCP
                                       : OS_NET_IPPROTO_UDP;
    return eos_martos_pointer_result(
        os_net_socket_create(native_domain, native_type, native_protocol), 0);
}

static int32_t eos_port_socket_close(eos_port_socket socket) {
    return eos_martos_network_error(
        os_net_socket_close((os_net_socket)(uintptr_t)socket),
        EOS_MARTOS_NETWORK_OTHER);
}

static int32_t eos_port_socket_bind(eos_port_socket socket,
                                    const eos_port_socket_address *address) {
    os_net_sockaddr native;
    int32_t error = eos_martos_address_to_native(address, &native);
    if (error != 0) return error;
    return eos_martos_network_error(
        os_net_bind((os_net_socket)(uintptr_t)socket, &native,
                    (uint32)sizeof(native)), EOS_MARTOS_NETWORK_OTHER);
}

static int32_t eos_port_socket_connect(eos_port_socket socket,
                                       const eos_port_socket_address *address) {
    os_net_sockaddr native;
    int32_t error = eos_martos_address_to_native(address, &native);
    if (error != 0) return error;
    return eos_martos_network_error(
        os_net_connect((os_net_socket)(uintptr_t)socket, &native,
                       (uint32)sizeof(native)), EOS_MARTOS_NETWORK_CONNECT);
}

static int32_t eos_port_socket_listen(eos_port_socket socket,
                                      int32_t backlog) {
    return eos_martos_network_error(
        os_net_listen((os_net_socket)(uintptr_t)socket, backlog),
        EOS_MARTOS_NETWORK_OTHER);
}

static eos_port_socket_create_result eos_port_socket_accept(
    eos_port_socket socket,
    eos_port_socket_address *address) {
    os_net_sockaddr native;
    uint32 length = (uint32)sizeof(native);
    eos_port_socket_create_result result;
    int32_t error;
    (void)memset(&native, 0, sizeof(native));
    native.sin_len = (uint8)sizeof(native);
    result = eos_martos_pointer_result(
        os_net_accept((os_net_socket)(uintptr_t)socket, &native, &length), 1);
    if (result.socket == EOS_PORT_SOCKET_INVALID) return result;
    if (length != (uint32)sizeof(native)) error = EOS_ERRNO_IO;
    else error = eos_martos_address_from_native(&native, address);
    if (error != 0) {
        if (os_net_socket_close((os_net_socket)(uintptr_t)result.socket) != 0) {
            eos_rust_abort();
        }
        result.socket = EOS_PORT_SOCKET_INVALID;
        result.error_number = error;
    }
    return result;
}

static eos_port_socket_io_result eos_port_socket_send(
    eos_port_socket socket, const void *buffer, uint32_t byte_count,
    int32_t flags) {
    return eos_martos_network_io_result(
        os_net_send((os_net_socket)(uintptr_t)socket, buffer,
                    (uint32)byte_count, eos_martos_message_flags(flags)),
        EOS_MARTOS_NETWORK_SEND);
}

static eos_port_socket_io_result eos_port_socket_receive(
    eos_port_socket socket, void *buffer, uint32_t byte_count,
    int32_t flags) {
    return eos_martos_network_io_result(
        os_net_recv((os_net_socket)(uintptr_t)socket, buffer,
                    (uint32)byte_count, eos_martos_message_flags(flags)),
        EOS_MARTOS_NETWORK_RECEIVE);
}

static eos_port_socket_io_result eos_port_socket_send_to(
    eos_port_socket socket, const void *buffer, uint32_t byte_count,
    int32_t flags, const eos_port_socket_address *destination) {
    os_net_sockaddr native;
    int32_t error = eos_martos_address_to_native(destination, &native);
    if (error != 0) return (eos_port_socket_io_result){-1, error};
    return eos_martos_network_io_result(
        os_net_sendto((os_net_socket)(uintptr_t)socket, buffer,
                      (uint32)byte_count, eos_martos_message_flags(flags),
                      &native, (uint32)sizeof(native)),
        EOS_MARTOS_NETWORK_SEND);
}

static eos_port_socket_io_result eos_port_socket_receive_from(
    eos_port_socket socket, void *buffer, uint32_t byte_count,
    int32_t flags, eos_port_socket_address *source) {
    os_net_sockaddr native;
    uint32 length = (uint32)sizeof(native);
    int32_t native_result;
    eos_port_socket_io_result result;
    (void)memset(&native, 0, sizeof(native));
    native.sin_len = (uint8)sizeof(native);
    native_result = os_net_recvfrom(
        (os_net_socket)(uintptr_t)socket, buffer, (uint32)byte_count,
        eos_martos_message_flags(flags), source == NULL ? NULL : &native,
        source == NULL ? NULL : &length);
    result = eos_martos_network_io_result(native_result,
                                          EOS_MARTOS_NETWORK_RECEIVE);
    if (result.count >= 0 && source != NULL) {
        int32_t error;
        if (native_result == OS_NET_ECLOSED) {
            (void)memset(source, 0, sizeof(*source));
            error = 0;
        } else {
            error = length == (uint32)sizeof(native)
                        ? eos_martos_address_from_native(&native, source)
                        : EOS_ERRNO_IO;
        }
        if (error != 0) return (eos_port_socket_io_result){-1, error};
    }
    return result;
}

static int32_t eos_port_socket_shutdown(eos_port_socket socket, int32_t how) {
    return eos_martos_network_error(
        os_net_shutdown((os_net_socket)(uintptr_t)socket, how),
        EOS_MARTOS_NETWORK_OTHER);
}

static int32_t eos_port_socket_local_address(
    eos_port_socket socket, eos_port_socket_address *address) {
    return eos_martos_local_address(socket, address);
}

static int32_t eos_port_socket_remote_address(
    eos_port_socket socket, eos_port_socket_address *address) {
    return eos_martos_remote_address(socket, address);
}

static int32_t eos_port_socket_set_timeout(eos_port_socket socket,
                                           uint32_t receive,
                                           uint32_t timeout_ticks) {
    uint32 native_timeout = (uint32)timeout_ticks;
    return eos_martos_network_error(
        os_net_setsockopt((os_net_socket)(uintptr_t)socket,
                          OS_NET_SOL_SOCKET,
                          eos_martos_timeout_option(receive),
                          &native_timeout, (uint32)sizeof(native_timeout)),
        EOS_MARTOS_NETWORK_OTHER);
}

static int32_t eos_port_socket_set_nonblocking(eos_port_socket socket,
                                               uint32_t enabled) {
    (void)socket;
    (void)enabled;
    return 0;
}

static int32_t eos_port_socket_set_integer_option(eos_port_socket socket,
                                                  int32_t option_name,
                                                  int32_t value) {
    int32_t native_option = eos_martos_integer_option(option_name);
    if (native_option < 0) return EOS_ERRNO_NO_PROTOCOL_OPTION;
    return eos_martos_network_error(
        os_net_setsockopt((os_net_socket)(uintptr_t)socket,
                          OS_NET_SOL_SOCKET, native_option,
                          &value, (uint32)sizeof(value)),
        EOS_MARTOS_NETWORK_OTHER);
}

static int32_t eos_port_socket_connection_error(eos_port_socket socket,
                                                int32_t *error_number) {
    os_net_socket native = (os_net_socket)(uintptr_t)socket;
    if (os_net_is_socket_connected(native) != 0) {
        *error_number = 0;
    } else if (os_net_get_tcp_state(native) == OS_NET_TCP_CLOSED) {
        *error_number = EOS_ERRNO_CONNECTION_RESET;
    } else {
        *error_number = EOS_ERRNO_IN_PROGRESS;
    }
    return 0;
}

static int32_t eos_port_socket_poll(const eos_port_socket *sockets,
                                    const uint32_t *requested,
                                    uint32_t *observed,
                                    uint32_t socket_count,
                                    uint32_t timeout_ticks) {
    return eos_martos_socketset_poll(sockets, requested, observed,
                                     socket_count, timeout_ticks);
}
