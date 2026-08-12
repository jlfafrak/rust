#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

static _Thread_local int32_t eos_host_errno;
static pthread_mutex_t eos_host_locks[EOS_PORT_LOCK_COUNT] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER};
static pthread_mutex_t eos_host_console_guard = PTHREAD_MUTEX_INITIALIZER;
static char eos_host_console_in[4096];
static uint32_t eos_host_console_in_length;
static uint32_t eos_host_console_in_offset;
static char eos_host_console_out[2][4096];
static uint32_t eos_host_console_out_length[2];
static char eos_host_hostname[64] = "eos-host";

#ifdef EOS_RUST_HOST_TEST
static int32_t eos_host_next_alloc_status;
static int32_t eos_host_next_aligned_status;
static int32_t eos_host_next_realloc_status;
static int32_t eos_host_next_lock_status;
static uint32_t eos_host_lock_successes_before_failure;
static int32_t eos_host_delayed_lock_status;
static int32_t eos_host_next_unlock_status;
static int32_t eos_host_next_environment_set_status;
static int32_t eos_host_next_environment_unset_status;
static int eos_host_native_environment_present;
static char eos_host_native_environment_name[64];
static char eos_host_native_environment_value[EOS_PORT_ENV_VALUE_CAPACITY];
static int32_t eos_host_console_failure[3];
static _Atomic uint32_t eos_host_console_open_count[3];
static uint32_t eos_host_console_partial_bytes[3];
static int32_t eos_host_console_partial_status[3];
static uint32_t eos_host_file_partial_read_bytes;
static int32_t eos_host_file_partial_read_status;
static uint32_t eos_host_file_partial_write_bytes;
static int32_t eos_host_file_partial_write_status;
static int32_t eos_host_next_file_close_status;
static int32_t eos_host_next_sync_wait_status;
static uint32_t eos_host_seek_successes_before_failure;
static int32_t eos_host_delayed_seek_status;

void eos_host_test_reset(void) {
    eos_host_next_alloc_status = 0;
    eos_host_next_aligned_status = 0;
    eos_host_next_realloc_status = 0;
    eos_host_next_lock_status = 0;
    eos_host_lock_successes_before_failure = UINT32_MAX;
    eos_host_delayed_lock_status = 0;
    eos_host_next_unlock_status = 0;
    eos_host_next_environment_set_status = 0;
    eos_host_next_environment_unset_status = 0;
    eos_host_native_environment_present = 0;
    for (uint32_t stream = 0; stream < UINT32_C(3); ++stream) {
        eos_host_console_failure[stream] = 0;
        atomic_store_explicit(&eos_host_console_open_count[stream],
                              UINT32_C(0), memory_order_relaxed);
        eos_host_console_partial_bytes[stream] = UINT32_MAX;
        eos_host_console_partial_status[stream] = 0;
    }
    (void)pthread_mutex_lock(&eos_host_console_guard);
    eos_host_console_in_length = 0;
    eos_host_console_in_offset = 0;
    eos_host_console_out_length[0] = 0;
    eos_host_console_out_length[1] = 0;
    eos_host_file_partial_read_bytes = UINT32_MAX;
    eos_host_file_partial_read_status = 0;
    eos_host_file_partial_write_bytes = UINT32_MAX;
    eos_host_file_partial_write_status = 0;
    eos_host_next_file_close_status = 0;
    eos_host_next_sync_wait_status = 0;
    eos_host_seek_successes_before_failure = UINT32_MAX;
    eos_host_delayed_seek_status = 0;
    (void)strcpy(eos_host_hostname, "eos-host");
    (void)pthread_mutex_unlock(&eos_host_console_guard);
}
void eos_host_test_fail_next_alloc(int32_t status) { eos_host_next_alloc_status = status; }
void eos_host_test_fail_next_aligned_alloc(int32_t status) { eos_host_next_aligned_status = status; }
void eos_host_test_fail_next_realloc(int32_t status) { eos_host_next_realloc_status = status; }
void eos_host_test_fail_next_lock(int32_t status) { eos_host_next_lock_status = status; }
void eos_host_test_fail_lock_after(uint32_t successful_locks, int32_t status) {
    eos_host_lock_successes_before_failure = successful_locks;
    eos_host_delayed_lock_status = status;
}
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
void eos_host_test_file_partial_read(uint32_t bytes, int32_t status) {
    eos_host_file_partial_read_bytes = bytes;
    eos_host_file_partial_read_status = status;
}
void eos_host_test_file_partial_write(uint32_t bytes, int32_t status) {
    eos_host_file_partial_write_bytes = bytes;
    eos_host_file_partial_write_status = status;
}
void eos_host_test_fail_next_file_close(int32_t status) {
    eos_host_next_file_close_status = status;
}
void eos_host_test_fail_next_sync_wait(int32_t status) {
    eos_host_next_sync_wait_status = status;
}
void eos_host_test_fail_seek_after(uint32_t successful_seeks, int32_t status) {
    eos_host_seek_successes_before_failure = successful_seeks;
    eos_host_delayed_seek_status = status;
}
void eos_host_test_console_input(const char *text) {
    size_t length = strlen(text);
    if (length > sizeof(eos_host_console_in)) length = sizeof(eos_host_console_in);
    (void)pthread_mutex_lock(&eos_host_console_guard);
    (void)memcpy(eos_host_console_in, text, length);
    eos_host_console_in_length = (uint32_t)length;
    eos_host_console_in_offset = 0;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
}
uint32_t eos_host_test_console_output(uint32_t stream, char *buffer,
                                      uint32_t capacity) {
    uint32_t index;
    uint32_t count;
    if (stream < UINT32_C(1) || stream > UINT32_C(2) || buffer == NULL) return 0;
    index = stream - UINT32_C(1);
    (void)pthread_mutex_lock(&eos_host_console_guard);
    count = eos_host_console_out_length[index] < capacity
                ? eos_host_console_out_length[index]
                : capacity;
    (void)memcpy(buffer, eos_host_console_out[index], count);
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    return count;
}
void eos_host_test_console_partial(uint32_t stream, uint32_t bytes,
                                   int32_t status) {
    if (stream < UINT32_C(3)) {
        eos_host_console_partial_bytes[stream] = bytes;
        eos_host_console_partial_status[stream] = status;
    }
}
void eos_host_test_hostname(const char *name) {
    (void)pthread_mutex_lock(&eos_host_console_guard);
    (void)strncpy(eos_host_hostname, name, sizeof(eos_host_hostname) - 1U);
    eos_host_hostname[sizeof(eos_host_hostname) - 1U] = '\0';
    (void)pthread_mutex_unlock(&eos_host_console_guard);
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
    if (eos_host_delayed_lock_status != 0) {
        if (eos_host_lock_successes_before_failure == UINT32_C(0)) {
            int32_t status = eos_host_delayed_lock_status;
            eos_host_delayed_lock_status = 0;
            eos_host_lock_successes_before_failure = UINT32_MAX;
            return status;
        }
        --eos_host_lock_successes_before_failure;
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

typedef struct eos_host_sync {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} eos_host_sync;

static int32_t eos_port_sync_create(eos_port_sync *sync) {
    eos_host_sync *created;
    if (sync == NULL) return 1;
    created = (eos_host_sync *)malloc(sizeof(*created));
    if (created == NULL) return 15;
    if (pthread_mutex_init(&created->mutex, NULL) != 0) {
        free(created);
        return 17;
    }
    if (pthread_cond_init(&created->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&created->mutex);
        free(created);
        return 17;
    }
    *sync = (eos_port_sync)(uintptr_t)created;
    return 0;
}

static int32_t eos_port_sync_lock(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    return value != NULL && pthread_mutex_lock(&value->mutex) == 0 ? 0 : 17;
}

static int32_t eos_port_sync_unlock(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    return value != NULL && pthread_mutex_unlock(&value->mutex) == 0 ? 0 : 20;
}

static int32_t eos_port_sync_wait(eos_port_sync sync, uint32_t events) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    (void)events;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_sync_wait_status != 0) {
        int32_t status = eos_host_next_sync_wait_status;
        eos_host_next_sync_wait_status = 0;
        return status;
    }
#endif
    return value != NULL &&
                   pthread_cond_wait(&value->condition, &value->mutex) == 0
               ? 0
               : 17;
}

static int32_t eos_port_sync_broadcast(eos_port_sync sync, uint32_t events) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    (void)events;
    return value != NULL && pthread_cond_broadcast(&value->condition) == 0
               ? 0
               : 17;
}

static int32_t eos_port_sync_destroy(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    if (value == NULL) return 1;
    if (pthread_cond_destroy(&value->condition) != 0) return 17;
    if (pthread_mutex_destroy(&value->mutex) != 0) return 17;
    free(value);
    return 0;
}

static int32_t eos_host_status_from_errno(int error_number) {
    switch (error_number) {
    case ENOENT: return 12;
    case EEXIST: return 13;
    case EACCES: return 16;
    case EBUSY: return 17;
    case EROFS: return 18;
    case ENOMEM: return 15;
    default: return 25;
    }
}

static int eos_host_file_descriptor(eos_port_file file) {
    return (int)file.words[0] - 1;
}

static int32_t eos_port_file_open(const char *path, uint32_t flags,
                                  eos_port_file *file) {
    int native_flags = 0;
    int descriptor;
    switch (flags & EOS_RUST_O_ACCMODE) {
    case EOS_RUST_O_RDONLY: native_flags |= O_RDONLY; break;
    case EOS_RUST_O_WRONLY: native_flags |= O_WRONLY; break;
    case EOS_RUST_O_RDWR: native_flags |= O_RDWR; break;
    default: return 1;
    }
    if ((flags & EOS_RUST_O_CREAT) != 0) native_flags |= O_CREAT;
    if ((flags & EOS_RUST_O_EXCL) != 0) native_flags |= O_EXCL;
    if ((flags & EOS_RUST_O_TRUNC) != 0) native_flags |= O_TRUNC;
    descriptor = open(path, native_flags, (mode_t)0666);
    if (descriptor < 0) return eos_host_status_from_errno(errno);
    file->words[0] = (uintptr_t)(descriptor + 1);
    file->words[1] = 0;
    return 0;
}

static int32_t eos_port_file_read(eos_port_file file, void *buffer,
                                  uint32_t byte_count, uint32_t *completed) {
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    ssize_t result;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_file_partial_read_bytes != UINT32_MAX) {
        if (requested > eos_host_file_partial_read_bytes) {
            requested = eos_host_file_partial_read_bytes;
        }
        trailing = eos_host_file_partial_read_status;
        eos_host_file_partial_read_bytes = UINT32_MAX;
        eos_host_file_partial_read_status = 0;
    }
#endif
    result = read(eos_host_file_descriptor(file), buffer, (size_t)requested);
    if (result < 0) {
        *completed = 0;
        return eos_host_status_from_errno(errno);
    }
    *completed = (uint32_t)result;
    return trailing;
}

static int32_t eos_port_file_write(eos_port_file file, const void *buffer,
                                   uint32_t byte_count, uint32_t *completed) {
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    ssize_t result;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_file_partial_write_bytes != UINT32_MAX) {
        if (requested > eos_host_file_partial_write_bytes) {
            requested = eos_host_file_partial_write_bytes;
        }
        trailing = eos_host_file_partial_write_status;
        eos_host_file_partial_write_bytes = UINT32_MAX;
        eos_host_file_partial_write_status = 0;
    }
#endif
    result = write(eos_host_file_descriptor(file), buffer, (size_t)requested);
    if (result < 0) {
        *completed = 0;
        return eos_host_status_from_errno(errno);
    }
    *completed = (uint32_t)result;
    return trailing;
}

static int32_t eos_port_file_seek(eos_port_file file, int64_t offset,
                                  int32_t origin) {
    int native_origin;
    switch (origin) {
    case EOS_RUST_SEEK_SET: native_origin = SEEK_SET; break;
    case EOS_RUST_SEEK_CUR: native_origin = SEEK_CUR; break;
    case EOS_RUST_SEEK_END: native_origin = SEEK_END; break;
    default: return 1;
    }
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_delayed_seek_status != 0) {
        if (eos_host_seek_successes_before_failure == UINT32_C(0)) {
            int32_t status = eos_host_delayed_seek_status;
            eos_host_delayed_seek_status = 0;
            eos_host_seek_successes_before_failure = UINT32_MAX;
            return status;
        }
        --eos_host_seek_successes_before_failure;
    }
#endif
    return lseek(eos_host_file_descriptor(file), (off_t)offset, native_origin) < 0
               ? eos_host_status_from_errno(errno)
               : 0;
}

static int32_t eos_port_file_tell(eos_port_file file, int64_t *offset) {
    off_t result = lseek(eos_host_file_descriptor(file), (off_t)0, SEEK_CUR);
    if (result < 0) return eos_host_status_from_errno(errno);
    *offset = (int64_t)result;
    return 0;
}

static int32_t eos_port_file_flush(eos_port_file file) {
    return fsync(eos_host_file_descriptor(file)) == 0
               ? 0
               : eos_host_status_from_errno(errno);
}

static void eos_host_fill_stat(const struct stat *native, eos_port_stat *out) {
    out->entry_id = (uint64_t)native->st_ino;
    out->byte_count = native->st_size < 0 ? 0 : (uint64_t)native->st_size;
    out->utc_seconds = (int64_t)native->st_mtime;
    out->type = S_ISDIR(native->st_mode) ? EOS_PORT_FILE_TYPE_DIRECTORY
                                        : EOS_PORT_FILE_TYPE_REGULAR;
    out->read_only = (native->st_mode & S_IWUSR) == 0 ? UINT32_C(1)
                                                      : UINT32_C(0);
}

static int32_t eos_port_file_stat(eos_port_file file, eos_port_stat *metadata) {
    struct stat native;
    if (fstat(eos_host_file_descriptor(file), &native) != 0) {
        return eos_host_status_from_errno(errno);
    }
    eos_host_fill_stat(&native, metadata);
    return 0;
}

static int32_t eos_port_file_close(eos_port_file file) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_file_close_status != 0) {
        int32_t status = eos_host_next_file_close_status;
        eos_host_next_file_close_status = 0;
        return status;
    }
#endif
    return close(eos_host_file_descriptor(file)) == 0
               ? 0
               : eos_host_status_from_errno(errno);
}

static int32_t eos_port_path_stat(const char *path, eos_port_stat *metadata) {
    struct stat native;
    if (stat(path, &native) != 0) return eos_host_status_from_errno(errno);
    eos_host_fill_stat(&native, metadata);
    return 0;
}

static int32_t eos_port_path_mkdir(const char *path) {
    return mkdir(path, (mode_t)0777) == 0 ? 0 : eos_host_status_from_errno(errno);
}

static int32_t eos_port_path_remove(const char *path) {
    return remove(path) == 0 ? 0 : eos_host_status_from_errno(errno);
}

static int32_t eos_port_path_rename(const char *old_path,
                                    const char *new_path) {
    return rename(old_path, new_path) == 0 ? 0 : eos_host_status_from_errno(errno);
}

static int32_t eos_port_directory_count(const char *path, uint32_t *count) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    uint32_t value = 0;
    if (directory == NULL) return eos_host_status_from_errno(errno);
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (value == UINT32_MAX) {
                (void)closedir(directory);
                return 15;
            }
            ++value;
        }
    }
    if (closedir(directory) != 0) return 25;
    *count = value;
    return 0;
}

static int32_t eos_port_directory_list(const char *path,
                                       eos_port_dir_entry *entries,
                                       uint32_t capacity,
                                       uint32_t *count) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    uint32_t used = 0;
    if (directory == NULL) return eos_host_status_from_errno(errno);
    while ((entry = readdir(directory)) != NULL) {
        size_t length;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (used >= capacity) {
            (void)closedir(directory);
            return 17;
        }
        length = strlen(entry->d_name);
        if (length > EOS_RUST_NAME_MAX) {
            (void)closedir(directory);
            return 1;
        }
        entries[used].entry_id = (uint64_t)entry->d_ino;
        {
            char child[EOS_RUST_PATH_MAX];
            struct stat native;
            size_t path_length = strlen(path);
            if (path_length + length + 2U > sizeof(child)) {
                (void)closedir(directory);
                return 1;
            }
            (void)memcpy(child, path, path_length);
            if (path_length != 1U || path[0] != '/') child[path_length++] = '/';
            (void)memcpy(child + path_length, entry->d_name, length + 1U);
            if (stat(child, &native) != 0) {
                (void)closedir(directory);
                return eos_host_status_from_errno(errno);
            }
            entries[used].type = S_ISDIR(native.st_mode)
                                     ? EOS_PORT_FILE_TYPE_DIRECTORY
                                     : EOS_PORT_FILE_TYPE_REGULAR;
        }
        entries[used].name_length = (uint32_t)length;
        (void)memcpy(entries[used].name, entry->d_name, length + 1U);
        ++used;
    }
    if (closedir(directory) != 0) return 25;
    *count = used;
    return 0;
}

static int32_t eos_port_console_read(uintptr_t stream, void *buffer,
                                     uint32_t byte_count,
                                     uint32_t *completed) {
    uint32_t available;
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    if (stream != UINT32_C(0)) return 1;
    (void)pthread_mutex_lock(&eos_host_console_guard);
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_console_partial_bytes[0] != UINT32_MAX) {
        if (requested > eos_host_console_partial_bytes[0]) {
            requested = eos_host_console_partial_bytes[0];
        }
        trailing = eos_host_console_partial_status[0];
        eos_host_console_partial_bytes[0] = UINT32_MAX;
        eos_host_console_partial_status[0] = 0;
    }
#endif
    available = eos_host_console_in_length - eos_host_console_in_offset;
    if (requested > available) requested = available;
    (void)memcpy(buffer, eos_host_console_in + eos_host_console_in_offset,
                 requested);
    eos_host_console_in_offset += requested;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    *completed = requested;
    return trailing;
}

static int32_t eos_port_console_write(uintptr_t stream, const void *buffer,
                                      uint32_t byte_count,
                                      uint32_t *completed) {
    uint32_t requested = byte_count;
    uint32_t index;
    uint32_t available;
    int32_t trailing = 0;
    if (stream < UINT32_C(1) || stream > UINT32_C(2)) return 1;
    index = (uint32_t)stream - UINT32_C(1);
    (void)pthread_mutex_lock(&eos_host_console_guard);
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_console_partial_bytes[(uint32_t)stream] != UINT32_MAX) {
        if (requested > eos_host_console_partial_bytes[(uint32_t)stream]) {
            requested = eos_host_console_partial_bytes[(uint32_t)stream];
        }
        trailing = eos_host_console_partial_status[(uint32_t)stream];
        eos_host_console_partial_bytes[(uint32_t)stream] = UINT32_MAX;
        eos_host_console_partial_status[(uint32_t)stream] = 0;
    }
#endif
    available = (uint32_t)sizeof(eos_host_console_out[index]) -
                eos_host_console_out_length[index];
    if (requested > available) requested = available;
    (void)memcpy(eos_host_console_out[index] + eos_host_console_out_length[index],
                 buffer, requested);
    eos_host_console_out_length[index] += requested;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    *completed = requested;
    return trailing;
}

static int32_t eos_port_hostname(char *name, uint32_t capacity) {
    size_t length;
    (void)pthread_mutex_lock(&eos_host_console_guard);
    length = strlen(eos_host_hostname);
    if (length + 1U > capacity) {
        (void)pthread_mutex_unlock(&eos_host_console_guard);
        return 1;
    }
    (void)memcpy(name, eos_host_hostname, length + 1U);
    (void)pthread_mutex_unlock(&eos_host_console_guard);
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
