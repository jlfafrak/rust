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
#define EOS_PORT_LOCK_RUNTIME UINT32_C(0)
#define EOS_PORT_LOCK_HASH_SEED UINT32_C(1)
#define EOS_PORT_LOCK_FD_TABLE UINT32_C(2)
#define EOS_PORT_LOCK_RUNTIME_INIT UINT32_C(3)
#define EOS_PORT_LOCK_CWD UINT32_C(4)
#define EOS_PORT_LOCK_THREAD_REGISTRY UINT32_C(5)
#define EOS_PORT_LOCK_TLS_KEYS UINT32_C(6)
#define EOS_PORT_LOCK_SYNC_REGISTRY UINT32_C(7)
#define EOS_PORT_LOCK_TIME UINT32_C(8)
#define EOS_PORT_LOCK_COUNT UINT32_C(9)

#define EOS_PORT_NO_WAIT UINT32_C(0)
#define EOS_PORT_WAIT_FOREVER UINT32_MAX
#define EOS_PORT_MAX_FINITE_WAIT (UINT32_MAX - UINT32_C(1))

#define EOS_PORT_FILE_TYPE_NONE UINT32_C(0)
#define EOS_PORT_FILE_TYPE_REGULAR UINT32_C(1)
#define EOS_PORT_FILE_TYPE_DIRECTORY UINT32_C(2)

typedef struct eos_port_file {
    uintptr_t words[2];
} eos_port_file;

typedef struct eos_port_stat {
    uint64_t entry_id;
    uint64_t byte_count;
    int64_t utc_seconds;
    uint32_t type;
    uint32_t read_only;
} eos_port_stat;

typedef struct eos_port_dir_entry {
    uint64_t entry_id;
    uint32_t type;
    uint32_t name_length;
    char name[64];
} eos_port_dir_entry;

typedef struct eos_port_result {
    int32_t status;
    int32_t error_number;
} eos_port_result;

typedef uintptr_t eos_port_sync;
typedef uintptr_t eos_port_mutex;
typedef uintptr_t eos_port_semaphore;
typedef uintptr_t eos_port_socket;

#define EOS_PORT_SOCKET_INVALID ((eos_port_socket)UINTPTR_MAX)
#define EOS_PORT_SOCKET_EVENT_READ UINT32_C(0x01)
#define EOS_PORT_SOCKET_EVENT_WRITE UINT32_C(0x02)
#define EOS_PORT_SOCKET_EVENT_ERROR UINT32_C(0x04)
#define EOS_PORT_SOCKET_EVENT_HANGUP UINT32_C(0x08)
#define EOS_PORT_SOCKET_EVENT_PRIORITY UINT32_C(0x10)
#define EOS_PORT_SOCKET_EVENT_HANGUP_ELIGIBLE UINT32_C(0x20)
#define EOS_PORT_SOCKET_POLL_CAPACITY UINT32_C(64)

typedef struct eos_port_socket_address {
    uint16_t family;
    uint16_t port;
    uint32_t flowinfo;
    uint8_t address[16];
    uint32_t scope_id;
} eos_port_socket_address;

typedef struct eos_port_socket_create_result {
    eos_port_socket socket;
    int32_t error_number;
} eos_port_socket_create_result;

typedef struct eos_port_socket_io_result {
    int32_t count;
    int32_t error_number;
} eos_port_socket_io_result;
typedef void (*eos_port_thread_start)(void *argument);

/* Implemented with internal linkage by the one C source selected by CMake. */
static int32_t eos_port_thread_tls_get(uint32_t slot, uintptr_t *value);
static int32_t eos_port_thread_tls_set(uint32_t slot, uintptr_t value);
static int32_t eos_port_thread_create(const char *name,
                                      eos_port_thread_start start,
                                      void *argument,
                                      uint32_t stack_size);
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
static int32_t eos_port_lock_acquire(uint32_t lock_id);
static int32_t eos_port_lock_release(uint32_t lock_id);
static int32_t eos_port_console_establish(uint32_t stream,
                                          uintptr_t *native_console);
static void eos_port_console_release(uintptr_t native_console);
static void eos_port_direct_diagnostic(const char *message);
static int32_t eos_port_sync_create(eos_port_sync *sync);
static int32_t eos_port_sync_lock(eos_port_sync sync);
static int32_t eos_port_sync_unlock(eos_port_sync sync);
static int32_t eos_port_sync_wait(eos_port_sync sync, uint32_t events);
static int32_t eos_port_sync_broadcast(eos_port_sync sync, uint32_t events);
static int32_t eos_port_sync_destroy(eos_port_sync sync);
static int32_t eos_port_mutex_create(uint32_t recursive,
                                     eos_port_mutex *mutex);
static int32_t eos_port_mutex_lock(eos_port_mutex mutex,
                                   uint32_t timeout_ticks);
static int32_t eos_port_mutex_unlock(eos_port_mutex mutex);
static int32_t eos_port_mutex_destroy(eos_port_mutex mutex);
static int32_t eos_port_semaphore_create(uint32_t maximum_count,
                                         uint32_t initial_count,
                                         eos_port_semaphore *semaphore);
static int32_t eos_port_semaphore_take(eos_port_semaphore semaphore,
                                       uint32_t timeout_ticks);
static int32_t eos_port_semaphore_give(eos_port_semaphore semaphore);
static int32_t eos_port_semaphore_destroy(eos_port_semaphore semaphore);
static int32_t eos_port_monotonic_usec(uint64_t *usec);
static int32_t eos_port_realtime_usec(uint64_t *usec);
static uint32_t eos_port_tick_rate_hz(void);
static int32_t eos_port_delay_ticks(uint32_t ticks);
static int32_t eos_port_delay_usec(uint32_t usec);
static eos_port_result eos_port_file_open(const char *path, uint32_t flags,
                                          eos_port_file *file);
static int32_t eos_port_file_read(eos_port_file file, void *buffer,
                                  uint32_t byte_count, uint32_t *completed);
static int32_t eos_port_file_write(eos_port_file file, const void *buffer,
                                   uint32_t byte_count, uint32_t *completed);
static int32_t eos_port_file_seek(eos_port_file file, int64_t offset,
                                  int32_t origin);
static int32_t eos_port_file_tell(eos_port_file file, int64_t *offset);
static int32_t eos_port_file_flush(eos_port_file file);
static int32_t eos_port_file_stat(eos_port_file file, eos_port_stat *metadata);
static int32_t eos_port_file_close(eos_port_file file);
static int32_t eos_port_path_stat(const char *path, eos_port_stat *metadata);
static int32_t eos_port_path_mkdir(const char *path);
static eos_port_result eos_port_path_unlink(const char *path);
static eos_port_result eos_port_path_rmdir(const char *path);
static eos_port_result eos_port_path_rename(const char *old_path,
                                            const char *new_path);
static int32_t eos_port_directory_count(const char *path, uint32_t *count);
static int32_t eos_port_directory_list(const char *path,
                                       eos_port_dir_entry *entries,
                                       uint32_t capacity,
                                       uint32_t *count);
static int32_t eos_port_console_read(uintptr_t stream, void *buffer,
                                     uint32_t byte_count,
                                     uint32_t *completed);
static int32_t eos_port_console_write(uintptr_t stream, const void *buffer,
                                      uint32_t byte_count,
                                      uint32_t *completed);
static int32_t eos_port_hostname(char *name, uint32_t capacity);
static eos_port_socket_create_result eos_port_socket_create(int32_t domain,
                                                            int32_t type,
                                                            int32_t protocol);
static int32_t eos_port_socket_close(eos_port_socket socket);
static int32_t eos_port_socket_bind(eos_port_socket socket,
                                    const eos_port_socket_address *address);
static int32_t eos_port_socket_connect(eos_port_socket socket,
                                       const eos_port_socket_address *address);
static int32_t eos_port_socket_listen(eos_port_socket socket,
                                      int32_t backlog);
static eos_port_socket_create_result eos_port_socket_accept(
    eos_port_socket socket,
    eos_port_socket_address *address);
static eos_port_socket_io_result eos_port_socket_send(
    eos_port_socket socket,
    const void *buffer,
    uint32_t byte_count,
    int32_t flags);
static eos_port_socket_io_result eos_port_socket_receive(
    eos_port_socket socket,
    void *buffer,
    uint32_t byte_count,
    int32_t flags);
static eos_port_socket_io_result eos_port_socket_send_to(
    eos_port_socket socket,
    const void *buffer,
    uint32_t byte_count,
    int32_t flags,
    const eos_port_socket_address *destination);
static eos_port_socket_io_result eos_port_socket_receive_from(
    eos_port_socket socket,
    void *buffer,
    uint32_t byte_count,
    int32_t flags,
    eos_port_socket_address *source);
static int32_t eos_port_socket_shutdown(eos_port_socket socket, int32_t how);
static int32_t eos_port_socket_local_address(
    eos_port_socket socket,
    eos_port_socket_address *address);
static int32_t eos_port_socket_remote_address(
    eos_port_socket socket,
    eos_port_socket_address *address);
static int32_t eos_port_socket_set_timeout(eos_port_socket socket,
                                           uint32_t receive,
                                           uint32_t timeout_ticks);
static int32_t eos_port_socket_set_nonblocking(eos_port_socket socket,
                                               uint32_t enabled);
static int32_t eos_port_socket_set_integer_option(eos_port_socket socket,
                                                  int32_t option_name,
                                                  int32_t value);
static int32_t eos_port_socket_connection_error(eos_port_socket socket,
                                                int32_t *error_number);
static int32_t eos_port_socket_poll(const eos_port_socket *sockets,
                                    const uint32_t *requested,
                                    uint32_t *observed,
                                    uint32_t socket_count,
                                    uint32_t timeout_ticks);

#endif
