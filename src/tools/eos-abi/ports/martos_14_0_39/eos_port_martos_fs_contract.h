#ifndef EOS_PORT_MARTOS_FS_CONTRACT_H
#define EOS_PORT_MARTOS_FS_CONTRACT_H

/*
 * Exact MARTOS filesystem policy isolated for compilation against both the
 * pinned SDK declarations and deterministic contract fakes.
 */
static inline eos_port_result eos_martos_fs_file_open_native(
    const char *path,
    uint32_t flags,
    os_efs_file_id *native) {
    os_efs_access_mode mode;
    os_status status;
    eos_port_result result = {OS_STS_OK, 0};
    const int create = (flags & EOS_RUST_O_CREAT) != 0;
    const int exclusive = (flags & EOS_RUST_O_EXCL) != 0;
    const int truncate = (flags & EOS_RUST_O_TRUNC) != 0;
    switch (flags & EOS_RUST_O_ACCMODE) {
    case EOS_RUST_O_RDONLY: mode = OS_EFS_READ; break;
    case EOS_RUST_O_WRONLY: mode = OS_EFS_WRITE; break;
    case EOS_RUST_O_RDWR: mode = OS_EFS_READ_WRITE; break;
    default:
        result.status = OS_STS_INVALID_PARAM2;
        return result;
    }
    if (create && exclusive) {
        os_efs_entry_type type = OS_EFS_NONE;
        status = os_efs_entry_exists(path, &type, OS_WAIT_FOREVER);
        if (status != OS_STS_OK) {
            result.status = (int32_t)status;
            return result;
        }
        if (type != OS_EFS_NONE) {
            result.status = OS_STS_OBJECT_EXISTS;
            return result;
        }
        /* The SDK has no atomic create-exclusive operation. */
        result.status = OS_STS_NOT_CALLABLE_FROM_ISR;
        result.error_number = EOS_ERRNO_NOT_SUPPORTED;
        return result;
    }
    if (truncate && create) {
        result.status = (int32_t)os_efs_file_init(
            path, mode, native, OS_WAIT_FOREVER);
        return result;
    }
    if (truncate) {
        status = os_efs_file_open(path, mode, native, OS_WAIT_FOREVER);
        if (status != OS_STS_OK) {
            result.status = (int32_t)status;
            return result;
        }
        status = os_efs_file_close(*native, OS_WAIT_FOREVER);
        if (status != OS_STS_OK) {
            result.status = (int32_t)status;
            return result;
        }
        /* The SDK has no truncate-existing operation that cannot create. */
        result.status = OS_STS_NOT_CALLABLE_FROM_ISR;
        result.error_number = EOS_ERRNO_NOT_SUPPORTED;
        return result;
    }
    status = os_efs_file_open(path, mode, native, OS_WAIT_FOREVER);
    if (status == OS_STS_OBJECT_NOT_FOUND && create) {
        /* A later init could overwrite a file created after this failed open. */
        result.status = OS_STS_NOT_CALLABLE_FROM_ISR;
        result.error_number = EOS_ERRNO_NOT_SUPPORTED;
        return result;
    }
    result.status = (int32_t)status;
    return result;
}

static inline eos_port_result eos_martos_fs_known_error(
    int32_t status,
    int32_t error_number) {
    eos_port_result result = {status, error_number};
    return result;
}

static inline eos_port_result eos_martos_fs_validate_parent_components(
    const char *path) {
    char component[EOS_RUST_PATH_MAX];
    size_t index;
    eos_port_result result = {OS_STS_OK, 0};

    for (index = 1; path[index] != '\0'; ++index) {
        os_efs_entry_type type = OS_EFS_NONE;
        os_status status;
        if (path[index] != '/') continue;
        if (index >= sizeof(component)) {
            return eos_martos_fs_known_error(OS_STS_INVALID_PARAM1,
                                             EOS_ERRNO_NAME_TOO_LONG);
        }
        (void)memcpy(component, path, index);
        component[index] = '\0';
        status = os_efs_entry_exists(component, &type, OS_WAIT_FOREVER);
        if (status != OS_STS_OK) {
            result.status = (int32_t)status;
            return result;
        }
        if (type == OS_EFS_NONE) {
            return eos_martos_fs_known_error(OS_STS_OBJECT_NOT_FOUND,
                                             EOS_ERRNO_NO_ENTRY);
        }
        if (type != OS_EFS_DIRECTORY) {
            return eos_martos_fs_known_error(OS_STS_INVALID_OBJECT_TYPE,
                                             EOS_ERRNO_NOT_DIRECTORY);
        }
    }
    return result;
}

static inline eos_port_result eos_martos_fs_path_type(
    const char *path,
    os_efs_entry_type *type) {
    eos_port_result result = {OS_STS_OK, 0};
    os_status status;

    result = eos_martos_fs_validate_parent_components(path);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    status = os_efs_entry_exists(path, type, OS_WAIT_FOREVER);
    if (status != OS_STS_OK) {
        result.status = (int32_t)status;
        return result;
    }
    if (*type == OS_EFS_NONE) {
        return eos_martos_fs_known_error(OS_STS_OBJECT_NOT_FOUND,
                                         EOS_ERRNO_NO_ENTRY);
    }
    return result;
}

static inline eos_port_result eos_martos_fs_path_unlink(const char *path) {
    eos_port_result result;
    os_efs_entry_type type = OS_EFS_NONE;

    result = eos_martos_fs_path_type(path, &type);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    if (type == OS_EFS_DIRECTORY) {
        return eos_martos_fs_known_error(OS_STS_INVALID_OBJECT_TYPE,
                                         EOS_ERRNO_IS_DIRECTORY);
    }
    if (unlink(path) == 0) return result;
    /* A preflight race or an opaque libc failure cannot be classified safely. */
    return eos_martos_fs_known_error(OS_STS_DEVICE_ERROR, EOS_ERRNO_IO);
}

static inline eos_port_result eos_martos_fs_path_rmdir(const char *path) {
    eos_port_result result;
    os_efs_entry_type type = OS_EFS_NONE;
    uint32 entry_count = 0;
    os_status status;

    result = eos_martos_fs_path_type(path, &type);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    if (type != OS_EFS_DIRECTORY) {
        return eos_martos_fs_known_error(OS_STS_INVALID_OBJECT_TYPE,
                                         EOS_ERRNO_NOT_DIRECTORY);
    }
    status = os_efs_directory_get_listing_count(
        path, false, &entry_count, OS_WAIT_FOREVER);
    if (status != OS_STS_OK) {
        result.status = (int32_t)status;
        return result;
    }
    if (entry_count != 0) {
        return eos_martos_fs_known_error(OS_STS_OBJECT_IN_USE,
                                         EOS_ERRNO_NOT_EMPTY);
    }
    if (rmdir(path) == 0) return result;
    return eos_martos_fs_known_error(OS_STS_DEVICE_ERROR, EOS_ERRNO_IO);
}

static inline eos_port_result eos_martos_fs_path_rename(
    const char *old_path,
    const char *new_path) {
    eos_port_result result;
    os_efs_entry_type source_type = OS_EFS_NONE;

    result = eos_martos_fs_path_type(old_path, &source_type);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    result = eos_martos_fs_validate_parent_components(new_path);
    if (result.status != OS_STS_OK || result.error_number != 0) return result;
    if (rename(old_path, new_path) == 0) return result;
    return eos_martos_fs_known_error(OS_STS_DEVICE_ERROR, EOS_ERRNO_IO);
}

#endif
