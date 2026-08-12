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
        status = os_efs_file_init(path, mode, native, OS_WAIT_FOREVER);
    }
    result.status = (int32_t)status;
    return result;
}

static inline eos_port_result eos_martos_fs_result_from_errno(
    int error_number) {
    eos_port_result result;
    result.error_number = error_number;
    switch (error_number) {
    case ENOENT: result.status = OS_STS_OBJECT_NOT_FOUND; break;
    case EEXIST: result.status = OS_STS_OBJECT_EXISTS; break;
    case EACCES: result.status = OS_STS_INSUFFICIENT_ACL; break;
    case EROFS: result.status = OS_STS_OBJECT_IS_READ_ONLY; break;
    case ENOTEMPTY: result.status = OS_STS_OBJECT_IN_USE; break;
    case EBUSY: result.status = OS_STS_OBJECT_IN_USE; break;
    case ENOMEM: result.status = OS_STS_ALLOC_ERROR; break;
    case EINVAL: result.status = OS_STS_INVALID_PARAM1; break;
    case EISDIR: result.status = OS_STS_INVALID_OBJECT_TYPE; break;
    default:
        result.status = OS_STS_DEVICE_ERROR;
        result.error_number = EOS_ERRNO_IO;
        break;
    }
    return result;
}

static inline eos_port_result eos_martos_fs_path_unlink(const char *path) {
    eos_port_result result = {OS_STS_OK, 0};
    return unlink(path) == 0
               ? result
               : eos_martos_fs_result_from_errno(
                     EOS_MARTOS_LIBC_ERRNO_VALUE);
}

static inline eos_port_result eos_martos_fs_path_rmdir(const char *path) {
    eos_port_result result = {OS_STS_OK, 0};
    return rmdir(path) == 0
               ? result
               : eos_martos_fs_result_from_errno(
                     EOS_MARTOS_LIBC_ERRNO_VALUE);
}

static inline eos_port_result eos_martos_fs_path_rename(
    const char *old_path,
    const char *new_path) {
    eos_port_result result = {OS_STS_OK, 0};
    return rename(old_path, new_path) == 0
               ? result
               : eos_martos_fs_result_from_errno(
                     EOS_MARTOS_LIBC_ERRNO_VALUE);
}

#endif
