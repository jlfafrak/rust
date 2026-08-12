#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(eos_rust_stat) == 128, "eos_rust_stat ABI size");
_Static_assert(_Alignof(eos_rust_stat) == 8, "eos_rust_stat ABI alignment");
_Static_assert(offsetof(eos_rust_stat, st_size) == 32,
               "eos_rust_stat st_size offset");
_Static_assert(offsetof(eos_rust_stat, st_mtime_sec) == 56,
               "eos_rust_stat st_mtime offset");
_Static_assert(offsetof(eos_rust_stat, reserved) == 100,
               "eos_rust_stat reserved offset");

#define EOS_FS_MODE_DIRECTORY UINT32_C(0040000)
#define EOS_FS_MODE_REGULAR UINT32_C(0100000)
#define EOS_FS_MODE_READ UINT32_C(0444)
#define EOS_FS_MODE_WRITE UINT32_C(0222)

typedef struct eos_fs_file {
    eos_port_file native;
    eos_port_sync sync;
    uint32_t status_flags;
    uint32_t poisoned;
} eos_fs_file;

typedef struct eos_null_file {
    eos_port_sync sync;
    uint32_t status_flags;
} eos_null_file;

static char eos_fs_cwd[EOS_RUST_PATH_MAX] = "/";

static void eos_fs_free_or_abort(void *memory) {
    if (memory != NULL && eos_port_memory_free(memory) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static void eos_fs_destroy_sync_or_abort(eos_port_sync sync) {
    if (eos_port_sync_destroy(sync) != EOS_PORT_STATUS_OK) eos_rust_abort();
}

static int32_t eos_fs_lock_cwd(void) {
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_CWD);
    return status == EOS_PORT_STATUS_OK ? 0
                                       : eos_fd_fail_status(status, "cwd.lock");
}

static void eos_fs_unlock_cwd(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_CWD) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static int32_t eos_fs_copy_truncated(const char *source, char *destination,
                                     uint32_t capacity, int32_t error_number) {
    size_t length;
    size_t copy;
    if (destination == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    if (capacity == 0) return eos_fd_fail_errno(error_number);
    length = strlen(source);
    copy = length < (size_t)capacity - 1U ? length : (size_t)capacity - 1U;
    (void)memcpy(destination, source, copy);
    destination[copy] = '\0';
    return length + 1U <= (size_t)capacity ? 0
                                           : eos_fd_fail_errno(error_number);
}

static int32_t eos_fs_normalize(const char *path, char resolved[EOS_RUST_PATH_MAX]) {
    char combined[EOS_RUST_PATH_MAX * 2U];
    size_t path_length;
    size_t length;
    size_t source;
    size_t output = 1U;
    if (path == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    path_length = 0;
    while (path_length < EOS_RUST_PATH_MAX && path[path_length] != '\0') ++path_length;
    if (path_length == EOS_RUST_PATH_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
    }
    if (path_length == 0) return eos_fd_fail_errno(EOS_ERRNO_NO_ENTRY);
    if (path[0] == '/') {
        (void)memcpy(combined, path, path_length + 1U);
    } else {
        size_t cwd_length;
        if (eos_fs_lock_cwd() != 0) return -1;
        cwd_length = strlen(eos_fs_cwd);
        if (cwd_length + 1U + path_length >= sizeof(combined)) {
            eos_fs_unlock_cwd();
            return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
        }
        (void)memcpy(combined, eos_fs_cwd, cwd_length);
        if (cwd_length != 1U) combined[cwd_length++] = '/';
        (void)memcpy(combined + cwd_length, path, path_length + 1U);
        eos_fs_unlock_cwd();
    }
    length = strlen(combined);
    resolved[0] = '/';
    source = combined[0] == '/' ? 1U : 0U;
    while (source <= length) {
        size_t begin;
        size_t component_length;
        while (source < length && combined[source] == '/') ++source;
        begin = source;
        while (source < length && combined[source] != '/') ++source;
        component_length = source - begin;
        if (component_length == 0U) break;
        if (component_length == 1U && combined[begin] == '.') continue;
        if (component_length == 2U && combined[begin] == '.' &&
            combined[begin + 1U] == '.') {
            if (output > 1U) {
                --output;
                while (output > 1U && resolved[output - 1U] != '/') --output;
                if (output > 1U) --output;
            }
            continue;
        }
        if (component_length > EOS_RUST_NAME_MAX) {
            return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
        }
        if (output > 1U) {
            if (output + 1U >= EOS_RUST_PATH_MAX) {
                return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
            }
            resolved[output++] = '/';
        }
        if (output + component_length >= EOS_RUST_PATH_MAX) {
            return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
        }
        (void)memcpy(resolved + output, combined + begin, component_length);
        output += component_length;
    }
    resolved[output] = '\0';
    return 0;
}

static int32_t eos_fs_sync_lock(eos_port_sync sync, const char *operation) {
    int32_t status = eos_port_sync_lock(sync);
    return status == EOS_PORT_STATUS_OK ? 0
                                       : eos_fd_fail_status(status, operation);
}

static void eos_fs_sync_unlock(eos_port_sync sync) {
    if (eos_port_sync_unlock(sync) != EOS_PORT_STATUS_OK) eos_rust_abort();
}

static int32_t eos_fs_io_result(int32_t status, uint32_t completed,
                                const char *operation, int is_read) {
    eos_error_result error;
    if (completed != 0) return (int32_t)completed;
    error = eos_error_from_port_status_impl(status, operation);
    if (error.kind == EOS_ERROR_NONE ||
        (is_read && error.kind == EOS_ERROR_END_OF_OBJECT)) {
        return 0;
    }
    return eos_fd_fail_errno(error.error_number);
}

static void eos_fs_fill_stat(const eos_port_stat *source,
                             eos_rust_stat *destination) {
    uint32_t permissions = EOS_FS_MODE_READ;
    (void)memset(destination, 0, sizeof(*destination));
    if (source->read_only == UINT32_C(0)) permissions |= EOS_FS_MODE_WRITE;
    destination->st_ino = source->entry_id;
    destination->st_mode = permissions |
        (source->type == EOS_PORT_FILE_TYPE_DIRECTORY
             ? EOS_FS_MODE_DIRECTORY
             : EOS_FS_MODE_REGULAR);
    destination->st_nlink = UINT32_C(1);
    destination->st_size = source->byte_count;
    destination->st_atime_sec = source->utc_seconds;
    destination->st_mtime_sec = source->utc_seconds;
    destination->st_ctime_sec = source->utc_seconds;
    destination->st_blocks =
        (source->byte_count + UINT64_C(511)) / UINT64_C(512);
    destination->st_blksize = UINT32_C(512);
}

static void eos_fs_file_destroy(eos_fd_native native) {
    eos_fs_file *file = (eos_fs_file *)native.pointer;
    if (file == NULL) return;
    if (eos_port_file_close(file->native) != EOS_PORT_STATUS_OK) eos_rust_abort();
    eos_fs_destroy_sync_or_abort(file->sync);
    eos_fs_free_or_abort(file);
}

static void eos_fs_null_destroy(eos_fd_native native) {
    eos_null_file *file = (eos_null_file *)native.pointer;
    if (file == NULL) return;
    eos_fs_destroy_sync_or_abort(file->sync);
    eos_fs_free_or_abort(file);
}

static int32_t eos_fs_create_null(uint32_t flags) {
    eos_null_file *file = NULL;
    eos_fd_native native;
    int32_t status = eos_port_memory_alloc((uint32_t)sizeof(*file),
                                           (void **)&file);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "null.allocate");
    }
    status = eos_port_sync_create(&file->sync);
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_free_or_abort(file);
        return eos_fd_fail_status(status, "null.sync.create");
    }
    file->status_flags = flags &
        (EOS_RUST_O_ACCMODE | EOS_RUST_O_NONBLOCK | EOS_RUST_O_APPEND);
    native.pointer = file;
    status = eos_fd_allocate(EOS_FD_KIND_NULL, native, eos_fs_null_destroy,
                             (flags & EOS_RUST_O_CLOEXEC) != 0
                                 ? EOS_FD_FLAG_CLOEXEC
                                 : UINT32_C(0));
    if (status < 0) eos_fs_null_destroy(native);
    return status;
}

eos_rust_fd_t eos_rust_open(const char *path, uint32_t flags, uint32_t mode) {
    static const uint32_t allowed = EOS_RUST_O_ACCMODE | EOS_RUST_O_CREAT |
        EOS_RUST_O_EXCL | EOS_RUST_O_TRUNC | EOS_RUST_O_APPEND |
        EOS_RUST_O_NONBLOCK | EOS_RUST_O_CLOEXEC;
    char resolved[EOS_RUST_PATH_MAX];
    eos_fs_file *file = NULL;
    eos_fd_native native;
    eos_port_result open_result;
    int32_t status;
    (void)mode;
    if ((flags & ~allowed) != 0 || (flags & EOS_RUST_O_ACCMODE) == 3 ||
        ((flags & EOS_RUST_O_EXCL) != 0 &&
         (flags & EOS_RUST_O_CREAT) == 0) ||
        ((flags & EOS_RUST_O_TRUNC) != 0 &&
         (flags & EOS_RUST_O_ACCMODE) == EOS_RUST_O_RDONLY)) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    if (strcmp(resolved, "/dev/null") == 0) return eos_fs_create_null(flags);
    status = eos_port_memory_alloc((uint32_t)sizeof(*file), (void **)&file);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "file.allocate");
    }
    (void)memset(file, 0, sizeof(*file));
    status = eos_port_sync_create(&file->sync);
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_free_or_abort(file);
        return eos_fd_fail_status(status, "file.sync.create");
    }
    open_result = eos_port_file_open(resolved, flags, &file->native);
    status = open_result.status;
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_destroy_sync_or_abort(file->sync);
        eos_fs_free_or_abort(file);
        if (open_result.error_number != 0) {
            return eos_fd_fail_errno(open_result.error_number);
        }
        return eos_fd_fail_status(status, "file.open");
    }
    if (open_result.error_number != 0) {
        eos_fs_destroy_sync_or_abort(file->sync);
        eos_fs_free_or_abort(file);
        return eos_fd_fail_errno(open_result.error_number);
    }
    file->status_flags = flags &
        (EOS_RUST_O_ACCMODE | EOS_RUST_O_APPEND | EOS_RUST_O_NONBLOCK);
    native.pointer = file;
    status = eos_fd_allocate(EOS_FD_KIND_FILE, native, eos_fs_file_destroy,
                             (flags & EOS_RUST_O_CLOEXEC) != 0
                                 ? EOS_FD_FLAG_CLOEXEC
                                 : UINT32_C(0));
    if (status < 0) eos_fs_file_destroy(native);
    return status;
}

static int32_t eos_fs_file_read(eos_fs_file *file, void *buffer,
                                uint32_t byte_count, int64_t offset,
                                int positional) {
    uint32_t completed = 0;
    int64_t original = 0;
    int32_t status;
    if (eos_fs_sync_lock(file->sync, "file.read.lock") != 0) return -1;
    if ((file->status_flags & EOS_RUST_O_ACCMODE) == EOS_RUST_O_WRONLY) {
        eos_fs_sync_unlock(file->sync);
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (file->poisoned != UINT32_C(0)) {
        eos_fs_sync_unlock(file->sync);
        return eos_fd_fail_errno(EOS_ERRNO_IO);
    }
    if (byte_count == 0) {
        eos_fs_sync_unlock(file->sync);
        return 0;
    }
    if (positional) {
        status = eos_port_file_tell(file->native, &original);
        if (status == EOS_PORT_STATUS_OK) {
            status = offset < 0 ? EOS_PORT_STATUS_INVALID_PARAM4
                                : eos_port_file_seek(file->native, offset,
                                                     EOS_RUST_SEEK_SET);
        }
        if (status != EOS_PORT_STATUS_OK) {
            eos_fs_sync_unlock(file->sync);
            return eos_fd_fail_status(status, "file.pread.seek");
        }
    }
    status = eos_port_file_read(file->native, buffer, byte_count, &completed);
    if (positional &&
        eos_port_file_seek(file->native, original, EOS_RUST_SEEK_SET) !=
            EOS_PORT_STATUS_OK) {
        file->poisoned = UINT32_C(1);
        eos_fs_sync_unlock(file->sync);
        eos_rust_abort();
    }
    eos_fs_sync_unlock(file->sync);
    return eos_fs_io_result(status, completed,
                            positional ? "file.pread" : "file.read", 1);
}

static int32_t eos_fs_file_write(eos_fs_file *file, const void *buffer,
                                 uint32_t byte_count, int64_t offset,
                                 int positional) {
    uint32_t completed = 0;
    int64_t original = 0;
    int32_t status;
    if (eos_fs_sync_lock(file->sync, "file.write.lock") != 0) return -1;
    if ((file->status_flags & EOS_RUST_O_ACCMODE) == EOS_RUST_O_RDONLY) {
        eos_fs_sync_unlock(file->sync);
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (file->poisoned != UINT32_C(0)) {
        eos_fs_sync_unlock(file->sync);
        return eos_fd_fail_errno(EOS_ERRNO_IO);
    }
    if (byte_count == 0) {
        eos_fs_sync_unlock(file->sync);
        return 0;
    }
    if (positional) {
        status = eos_port_file_tell(file->native, &original);
        if (status == EOS_PORT_STATUS_OK) {
            status = offset < 0 ? EOS_PORT_STATUS_INVALID_PARAM4
                                : eos_port_file_seek(file->native, offset,
                                                     EOS_RUST_SEEK_SET);
        }
        if (status != EOS_PORT_STATUS_OK) {
            eos_fs_sync_unlock(file->sync);
            return eos_fd_fail_status(status, "file.pwrite.seek");
        }
    } else if ((file->status_flags & EOS_RUST_O_APPEND) != 0) {
        status = eos_port_file_seek(file->native, 0, EOS_RUST_SEEK_END);
        if (status != EOS_PORT_STATUS_OK) {
            eos_fs_sync_unlock(file->sync);
            return eos_fd_fail_status(status, "file.append.seek");
        }
    }
    status = eos_port_file_write(file->native, buffer, byte_count, &completed);
    if (positional &&
        eos_port_file_seek(file->native, original, EOS_RUST_SEEK_SET) !=
            EOS_PORT_STATUS_OK) {
        file->poisoned = UINT32_C(1);
        eos_fs_sync_unlock(file->sync);
        eos_rust_abort();
    }
    eos_fs_sync_unlock(file->sync);
    return eos_fs_io_result(status, completed,
                            positional ? "file.pwrite" : "file.write", 0);
}

static int32_t eos_fs_null_read(eos_null_file *file) {
    int32_t result;
    if (eos_fs_sync_lock(file->sync, "null.read.lock") != 0) return -1;
    result = (file->status_flags & EOS_RUST_O_ACCMODE) == EOS_RUST_O_WRONLY
                 ? eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR)
                 : 0;
    eos_fs_sync_unlock(file->sync);
    return result;
}

static int32_t eos_fs_null_write(eos_null_file *file,
                                 uint32_t byte_count) {
    int32_t result;
    if (eos_fs_sync_lock(file->sync, "null.write.lock") != 0) return -1;
    result = (file->status_flags & EOS_RUST_O_ACCMODE) == EOS_RUST_O_RDONLY
                 ? eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR)
                 : (int32_t)byte_count;
    eos_fs_sync_unlock(file->sync);
    return result;
}

static int64_t eos_fs_file_seek(eos_fs_file *file, int64_t offset,
                                int32_t origin) {
    int64_t result = 0;
    int32_t status;
    if (origin < EOS_RUST_SEEK_SET || origin > EOS_RUST_SEEK_END) {
        return (int64_t)eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (eos_fs_sync_lock(file->sync, "file.seek.lock") != 0) return -1;
    status = eos_port_file_seek(file->native, offset, origin);
    if (status == EOS_PORT_STATUS_OK) {
        status = eos_port_file_tell(file->native, &result);
    }
    eos_fs_sync_unlock(file->sync);
    return status == EOS_PORT_STATUS_OK
               ? result
               : (int64_t)eos_fd_fail_status(status, "file.seek");
}

static int32_t eos_fs_file_flush(eos_fs_file *file) {
    int32_t status;
    if (eos_fs_sync_lock(file->sync, "file.flush.lock") != 0) return -1;
    status = eos_port_file_flush(file->native);
    eos_fs_sync_unlock(file->sync);
    return status == EOS_PORT_STATUS_OK ? 0
                                       : eos_fd_fail_status(status, "file.flush");
}

static int32_t eos_fs_file_metadata(eos_fs_file *file,
                                    eos_rust_stat *metadata) {
    eos_port_stat native;
    int32_t status;
    if (eos_fs_sync_lock(file->sync, "file.stat.lock") != 0) return -1;
    status = eos_port_file_stat(file->native, &native);
    eos_fs_sync_unlock(file->sync);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "file.stat");
    }
    eos_fs_fill_stat(&native, metadata);
    return 0;
}

int32_t eos_rust_stat_path(const char *path, eos_rust_stat *metadata) {
    char resolved[EOS_RUST_PATH_MAX];
    eos_port_stat native;
    int32_t status;
    if (metadata == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    status = eos_port_path_stat(resolved, &native);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "path.stat");
    }
    eos_fs_fill_stat(&native, metadata);
    return 0;
}

int32_t eos_rust_lstat(const char *path, eos_rust_stat *metadata) {
    return eos_rust_stat_path(path, metadata);
}

int32_t eos_rust_mkdir(const char *path, uint32_t mode) {
    char resolved[EOS_RUST_PATH_MAX];
    int32_t status;
    (void)mode;
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    status = eos_port_path_mkdir(resolved);
    return status == EOS_PORT_STATUS_OK ? 0
                                       : eos_fd_fail_status(status, "path.mkdir");
}

int32_t eos_rust_unlink(const char *path) {
    char resolved[EOS_RUST_PATH_MAX];
    eos_port_result result;
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    result = eos_port_path_unlink(resolved);
    if (result.error_number != 0) {
        return eos_fd_fail_errno(result.error_number);
    }
    return result.status == EOS_PORT_STATUS_OK
               ? 0
               : eos_fd_fail_status(result.status, "path.unlink");
}

int32_t eos_rust_rmdir(const char *path) {
    char resolved[EOS_RUST_PATH_MAX];
    eos_port_stat metadata;
    eos_port_result result;
    int32_t status;
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    status = eos_port_path_stat(resolved, &metadata);
    if (status != EOS_PORT_STATUS_OK) return eos_fd_fail_status(status, "rmdir.stat");
    if (metadata.type != EOS_PORT_FILE_TYPE_DIRECTORY) {
        return eos_fd_fail_errno(EOS_ERRNO_NOT_DIRECTORY);
    }
    result = eos_port_path_rmdir(resolved);
    if (result.error_number != 0) {
        return eos_fd_fail_errno(result.error_number);
    }
    return result.status == EOS_PORT_STATUS_OK
               ? 0
               : eos_fd_fail_status(result.status, "path.rmdir");
}

int32_t eos_rust_rename(const char *old_path, const char *new_path) {
    char old_resolved[EOS_RUST_PATH_MAX];
    char new_resolved[EOS_RUST_PATH_MAX];
    eos_port_result result;
    if (eos_fs_normalize(old_path, old_resolved) != 0 ||
        eos_fs_normalize(new_path, new_resolved) != 0) return -1;
    result = eos_port_path_rename(old_resolved, new_resolved);
    if (result.error_number != 0) {
        return eos_fd_fail_errno(result.error_number);
    }
    return result.status == EOS_PORT_STATUS_OK
               ? 0
               : eos_fd_fail_status(result.status, "path.rename");
}

int32_t eos_rust_realpath(const char *path, char *resolved,
                          uint32_t capacity) {
    char absolute[EOS_RUST_PATH_MAX];
    eos_port_stat metadata;
    int32_t status;
    if (eos_fs_normalize(path, absolute) != 0) return -1;
    status = eos_port_path_stat(absolute, &metadata);
    if (status != EOS_PORT_STATUS_OK) return eos_fd_fail_status(status, "realpath.stat");
    return eos_fs_copy_truncated(absolute, resolved, capacity, EOS_ERRNO_RANGE);
}

int32_t eos_rust_getcwd(char *buffer, uint32_t capacity) {
    char snapshot[EOS_RUST_PATH_MAX];
    if (eos_fs_lock_cwd() != 0) return -1;
    (void)strcpy(snapshot, eos_fs_cwd);
    eos_fs_unlock_cwd();
    return eos_fs_copy_truncated(snapshot, buffer, capacity, EOS_ERRNO_RANGE);
}

int32_t eos_rust_chdir(const char *path) {
    char resolved[EOS_RUST_PATH_MAX];
    eos_port_stat metadata;
    int32_t status;
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    status = eos_port_path_stat(resolved, &metadata);
    if (status != EOS_PORT_STATUS_OK) return eos_fd_fail_status(status, "chdir.stat");
    if (metadata.type != EOS_PORT_FILE_TYPE_DIRECTORY) {
        return eos_fd_fail_errno(EOS_ERRNO_NOT_DIRECTORY);
    }
    if (eos_fs_lock_cwd() != 0) return -1;
    (void)strcpy(eos_fs_cwd, resolved);
    eos_fs_unlock_cwd();
    return 0;
}

int32_t eos_rust_symlink(const char *target, const char *link_path) {
    (void)target; (void)link_path;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_link(const char *old_path, const char *new_path) {
    (void)old_path; (void)new_path;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_chown(const char *path, uint32_t owner, uint32_t group) {
    (void)path; (void)owner; (void)group;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_lchown(const char *path, uint32_t owner, uint32_t group) {
    (void)path; (void)owner; (void)group;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_fchown(eos_rust_fd_t descriptor, uint32_t owner,
                        uint32_t group) {
    (void)descriptor; (void)owner; (void)group;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_chmod(const char *path, uint32_t mode) {
    (void)path; (void)mode;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}
int32_t eos_rust_fchmod(eos_rust_fd_t descriptor, uint32_t mode) {
    (void)descriptor; (void)mode;
    return eos_fd_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
}

#ifdef EOS_RUST_HOST_TEST
void eos_fs_test_reset(void) {
    if (eos_fs_lock_cwd() != 0) eos_rust_abort();
    (void)strcpy(eos_fs_cwd, "/");
    eos_fs_unlock_cwd();
}
#endif
