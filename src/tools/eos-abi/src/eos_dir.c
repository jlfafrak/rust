#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(eos_rust_dirent) == 80, "eos_rust_dirent ABI size");
_Static_assert(_Alignof(eos_rust_dirent) == 8,
               "eos_rust_dirent ABI alignment");
_Static_assert(offsetof(eos_rust_dirent, d_name) == 16,
               "eos_rust_dirent name offset");

typedef struct eos_directory_snapshot {
    eos_port_sync sync;
    eos_port_dir_entry *entries;
    uint32_t count;
    uint32_t index;
} eos_directory_snapshot;

static void eos_directory_destroy(eos_fd_native native) {
    eos_directory_snapshot *directory =
        (eos_directory_snapshot *)native.pointer;
    if (directory == NULL) return;
    eos_fs_destroy_sync_or_abort(directory->sync);
    eos_fs_free_or_abort(directory->entries);
    eos_fs_free_or_abort(directory);
}

eos_rust_fd_t eos_rust_opendir(const char *path) {
    char resolved[EOS_RUST_PATH_MAX];
    eos_port_stat metadata;
    eos_directory_snapshot *directory = NULL;
    eos_fd_native native;
    uint32_t count = 0;
    uint32_t listed = 0;
    int32_t status;
    if (eos_fs_normalize(path, resolved) != 0) return -1;
    status = eos_port_path_stat(resolved, &metadata);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "directory.stat");
    }
    if (metadata.type != EOS_PORT_FILE_TYPE_DIRECTORY) {
        return eos_fd_fail_errno(EOS_ERRNO_NOT_DIRECTORY);
    }
    status = eos_port_memory_alloc((uint32_t)sizeof(*directory),
                                   (void **)&directory);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "directory.allocate");
    }
    (void)memset(directory, 0, sizeof(*directory));
    status = eos_port_sync_create(&directory->sync);
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_free_or_abort(directory);
        return eos_fd_fail_status(status, "directory.sync.create");
    }
    status = eos_port_directory_count(resolved, &count);
    if (status != EOS_PORT_STATUS_OK) {
        native.pointer = directory;
        eos_directory_destroy(native);
        return eos_fd_fail_status(status, "directory.count");
    }
    if (count != 0) {
        uint64_t bytes = (uint64_t)count * (uint64_t)sizeof(*directory->entries);
        if (bytes > UINT32_MAX) {
            native.pointer = directory;
            eos_directory_destroy(native);
            return eos_fd_fail_errno(EOS_ERRNO_OVERFLOW);
        }
        status = eos_port_memory_alloc((uint32_t)bytes,
                                       (void **)&directory->entries);
        if (status != EOS_PORT_STATUS_OK) {
            native.pointer = directory;
            eos_directory_destroy(native);
            return eos_fd_fail_status(status, "directory.entries.allocate");
        }
        status = eos_port_directory_list(resolved, directory->entries, count,
                                         &listed);
        if (status != EOS_PORT_STATUS_OK || listed > count) {
            native.pointer = directory;
            eos_directory_destroy(native);
            return status == EOS_PORT_STATUS_OK
                       ? eos_fd_fail_errno(EOS_ERRNO_IO)
                       : eos_fd_fail_status(status, "directory.list");
        }
    }
    directory->count = listed;
    native.pointer = directory;
    status = eos_fd_allocate(EOS_FD_KIND_DIRECTORY, native,
                             eos_directory_destroy, UINT32_C(0));
    if (status < 0) eos_directory_destroy(native);
    return status;
}

int32_t eos_rust_readdir(eos_rust_fd_t descriptor, eos_rust_dirent *entry) {
    eos_fd_reference reference;
    eos_directory_snapshot *directory;
    eos_port_dir_entry *source;
    int32_t result;
    if (entry == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_DIRECTORY, &reference) != 0) {
        return -1;
    }
    directory = (eos_directory_snapshot *)reference.native.pointer;
    if (eos_fs_sync_lock(directory->sync, "directory.read.lock") != 0) {
        eos_fd_release_or_abort(&reference);
        return -1;
    }
    if (directory->index >= directory->count) {
        result = 0;
    } else {
        source = &directory->entries[directory->index++];
        (void)memset(entry, 0, sizeof(*entry));
        entry->d_ino = source->entry_id;
        entry->d_type = source->type == EOS_PORT_FILE_TYPE_DIRECTORY
                            ? EOS_RUST_DT_DIR
                            : source->type == EOS_PORT_FILE_TYPE_REGULAR
                                  ? EOS_RUST_DT_REG
                                  : EOS_RUST_DT_UNKNOWN;
        entry->d_name_length = source->name_length;
        (void)memcpy(entry->d_name, source->name,
                     (size_t)source->name_length + 1U);
        result = 1;
    }
    eos_fs_sync_unlock(directory->sync);
    eos_fd_release_or_abort(&reference);
    return result;
}

int32_t eos_rust_closedir(eos_rust_fd_t descriptor) {
    eos_fd_reference reference;
    int32_t result;
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_DIRECTORY, &reference) != 0) {
        return -1;
    }
#ifdef EOS_RUST_HOST_TEST
    eos_host_test_closedir_validation_point();
#endif
    result = eos_fd_close_reference(&reference, EOS_FD_KIND_DIRECTORY);
    if (result != 0 && reference.active != UINT32_C(0)) {
        int32_t saved_errno = *eos_port_errno_location();
        eos_fd_release_or_abort(&reference);
        *eos_port_errno_location() = saved_errno;
    }
    return result;
}
