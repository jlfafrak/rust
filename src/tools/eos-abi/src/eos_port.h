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
#define EOS_PORT_LOCK_COUNT UINT32_C(5)

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

typedef uintptr_t eos_port_sync;

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
static int32_t eos_port_file_open(const char *path, uint32_t flags,
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
static int32_t eos_port_path_remove(const char *path);
static int32_t eos_port_path_rename(const char *old_path,
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

#endif
