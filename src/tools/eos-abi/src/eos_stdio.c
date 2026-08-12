#include <string.h>

static int32_t eos_stdio_release(eos_fd_reference *reference,
                                 int32_t result) {
    eos_fd_release_or_abort(reference);
    return result;
}

int32_t eos_rust_close(eos_rust_fd_t descriptor) {
    return eos_fd_close(descriptor);
}

int32_t eos_rust_read(eos_rust_fd_t descriptor, void *buffer,
                      uint32_t byte_count) {
    eos_fd_reference reference;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_NONE, &reference) != 0) {
        return -1;
    }
    switch (reference.kind) {
    case EOS_FD_KIND_FILE:
        result = eos_fs_file_read((eos_fs_file *)reference.native.pointer,
                                  buffer, byte_count, 0, 0);
        break;
    case EOS_FD_KIND_PIPE_READER:
        result = byte_count == 0
                     ? 0
                     : eos_pipe_read(
                           (eos_pipe_endpoint *)reference.native.pointer,
                           buffer, byte_count);
        break;
    case EOS_FD_KIND_CONSOLE: {
        uint32_t completed = 0;
        if (reference.native.word != UINT32_C(0)) {
            result = eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        } else {
            int32_t status = eos_port_console_read(reference.native.word,
                                                   buffer, byte_count,
                                                   &completed);
            result = eos_fs_io_result(status, completed, "console.read", 1);
        }
        break;
    }
    case EOS_FD_KIND_NULL:
        result = eos_fs_null_read(
            (eos_null_file *)reference.native.pointer);
        break;
    default:
        result = eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        break;
    }
    return eos_stdio_release(&reference, result);
}

int32_t eos_rust_write(eos_rust_fd_t descriptor, const void *buffer,
                       uint32_t byte_count) {
    eos_fd_reference reference;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_NONE, &reference) != 0) {
        return -1;
    }
    switch (reference.kind) {
    case EOS_FD_KIND_FILE:
        result = eos_fs_file_write((eos_fs_file *)reference.native.pointer,
                                   buffer, byte_count, 0, 0);
        break;
    case EOS_FD_KIND_PIPE_WRITER:
        result = byte_count == 0
                     ? 0
                     : eos_pipe_write(
                           (eos_pipe_endpoint *)reference.native.pointer,
                           buffer, byte_count);
        break;
    case EOS_FD_KIND_CONSOLE: {
        uint32_t completed = 0;
        if (reference.native.word == UINT32_C(0)) {
            result = eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        } else {
            int32_t status = eos_port_console_write(reference.native.word,
                                                    buffer, byte_count,
                                                    &completed);
            result = eos_fs_io_result(status, completed, "console.write", 0);
        }
        break;
    }
    case EOS_FD_KIND_NULL:
        result = eos_fs_null_write(
            (eos_null_file *)reference.native.pointer, byte_count);
        break;
    default:
        result = eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        break;
    }
    return eos_stdio_release(&reference, result);
}

int32_t eos_rust_pread(eos_rust_fd_t descriptor, void *buffer,
                       uint32_t byte_count, int64_t offset) {
    eos_fd_reference reference;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    if (offset < 0) return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_FILE, &reference) != 0) {
        return -1;
    }
    result = eos_fs_file_read((eos_fs_file *)reference.native.pointer,
                              buffer, byte_count, offset, 1);
    return eos_stdio_release(&reference, result);
}

int32_t eos_rust_pwrite(eos_rust_fd_t descriptor, const void *buffer,
                        uint32_t byte_count, int64_t offset) {
    eos_fd_reference reference;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    if (offset < 0) return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_FILE, &reference) != 0) {
        return -1;
    }
    result = eos_fs_file_write((eos_fs_file *)reference.native.pointer,
                               buffer, byte_count, offset, 1);
    return eos_stdio_release(&reference, result);
}

int64_t eos_rust_lseek(eos_rust_fd_t descriptor, int64_t offset,
                       int32_t origin) {
    eos_fd_reference reference;
    int64_t result;
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_NONE, &reference) != 0) return -1;
    if (reference.kind != EOS_FD_KIND_FILE) {
        eos_fd_release_or_abort(&reference);
        return (int64_t)eos_fd_fail_errno(EOS_ERRNO_ILLEGAL_SEEK);
    }
    result = eos_fs_file_seek((eos_fs_file *)reference.native.pointer,
                              offset, origin);
    eos_fd_release_or_abort(&reference);
    return result;
}

int32_t eos_rust_fsync(eos_rust_fd_t descriptor) {
    eos_fd_reference reference;
    int32_t result;
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_FILE, &reference) != 0) {
        return -1;
    }
    result = eos_fs_file_flush((eos_fs_file *)reference.native.pointer);
    return eos_stdio_release(&reference, result);
}

int32_t eos_rust_fstat(eos_rust_fd_t descriptor, eos_rust_stat *metadata) {
    eos_fd_reference reference;
    int32_t result;
    if (metadata == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_FILE, &reference) != 0) {
        return -1;
    }
    result = eos_fs_file_metadata((eos_fs_file *)reference.native.pointer,
                                  metadata);
    return eos_stdio_release(&reference, result);
}

eos_rust_fd_t eos_rust_dup(eos_rust_fd_t descriptor) {
    return eos_fd_dup(descriptor);
}

eos_rust_fd_t eos_rust_dup2(eos_rust_fd_t old_descriptor,
                             eos_rust_fd_t new_descriptor) {
    return eos_fd_dup2(old_descriptor, new_descriptor);
}

static int32_t eos_stdio_status_flags(eos_fd_reference *reference,
                                      int32_t command, int32_t argument) {
    uint32_t allowed;
    uint32_t *flags;
    eos_port_sync sync;
    switch (reference->kind) {
    case EOS_FD_KIND_FILE:
        flags = &((eos_fs_file *)reference->native.pointer)->status_flags;
        sync = ((eos_fs_file *)reference->native.pointer)->sync;
        allowed = EOS_RUST_O_APPEND | EOS_RUST_O_NONBLOCK;
        break;
    case EOS_FD_KIND_NULL:
        flags = &((eos_null_file *)reference->native.pointer)->status_flags;
        sync = ((eos_null_file *)reference->native.pointer)->sync;
        allowed = EOS_RUST_O_APPEND | EOS_RUST_O_NONBLOCK;
        break;
    case EOS_FD_KIND_PIPE_READER:
    case EOS_FD_KIND_PIPE_WRITER:
        flags = &((eos_pipe_endpoint *)reference->native.pointer)->status_flags;
        sync = ((eos_pipe_endpoint *)reference->native.pointer)->pipe->sync;
        allowed = EOS_RUST_O_NONBLOCK;
        break;
    case EOS_FD_KIND_CONSOLE:
        if (command == EOS_RUST_F_GETFL) {
            return reference->native.word == UINT32_C(0)
                       ? (int32_t)EOS_RUST_O_RDONLY
                       : (int32_t)EOS_RUST_O_WRONLY;
        }
        return argument == 0 ? 0 : eos_fd_fail_errno(EOS_ERRNO_INVALID);
    default:
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (((uint32_t)argument & ~allowed) != 0) {
        if (command == EOS_RUST_F_SETFL) {
            return eos_fd_fail_errno(EOS_ERRNO_INVALID);
        }
    }
    if (eos_port_sync_lock(sync) != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_errno(EOS_ERRNO_BUSY);
    }
    if (command == EOS_RUST_F_GETFL) {
        int32_t result = (int32_t)*flags;
        if (eos_port_sync_unlock(sync) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return result;
    }
    *flags = (*flags & EOS_RUST_O_ACCMODE) | ((uint32_t)argument & allowed);
    if (eos_port_sync_unlock(sync) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    return 0;
}

int32_t eos_rust_fcntl(eos_rust_fd_t descriptor, int32_t command,
                       int32_t argument) {
    uint32_t descriptor_flags = 0;
    eos_fd_reference reference;
    int32_t result;
    switch (command) {
    case EOS_RUST_F_GETFD:
        if (eos_fd_get_flags(descriptor, &descriptor_flags) != 0) return -1;
        return (descriptor_flags & EOS_FD_FLAG_CLOEXEC) != 0
                   ? EOS_RUST_FD_CLOEXEC
                   : 0;
    case EOS_RUST_F_SETFD:
        if ((argument & ~EOS_RUST_FD_CLOEXEC) != 0) {
            return eos_fd_fail_errno(EOS_ERRNO_INVALID);
        }
        return eos_fd_set_cloexec(descriptor,
                                  (argument & EOS_RUST_FD_CLOEXEC) != 0);
    case EOS_RUST_F_DUPFD:
    case EOS_RUST_F_DUPFD_CLOEXEC:
        if (argument < 0 || (uint32_t)argument >= EOS_FD_TABLE_CAPACITY) {
            return eos_fd_fail_errno(EOS_ERRNO_INVALID);
        }
        return eos_fd_dup_min(descriptor, argument,
                              command == EOS_RUST_F_DUPFD_CLOEXEC
                                  ? EOS_FD_FLAG_CLOEXEC
                                  : UINT32_C(0));
    case EOS_RUST_F_GETFL:
    case EOS_RUST_F_SETFL:
        if (eos_fd_acquire(descriptor, EOS_FD_KIND_NONE, &reference) != 0) {
            return -1;
        }
        result = eos_stdio_status_flags(&reference, command, argument);
        return eos_stdio_release(&reference, result);
    default:
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
}

int32_t eos_rust_isatty(eos_rust_fd_t descriptor) {
    eos_fd_reference reference;
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_NONE, &reference) != 0) return 0;
    if (reference.kind == EOS_FD_KIND_CONSOLE) {
        eos_fd_release_or_abort(&reference);
        return 1;
    }
    eos_fd_release_or_abort(&reference);
    (void)eos_fd_fail_errno(EOS_ERRNO_NOT_TTY);
    return 0;
}

int32_t eos_rust_gethostname(char *name, uint32_t capacity) {
    char snapshot[EOS_RUST_PATH_MAX];
    int32_t status;
    if (name == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    if (capacity == 0) return eos_fd_fail_errno(EOS_ERRNO_NAME_TOO_LONG);
    status = eos_port_hostname(snapshot, (uint32_t)sizeof(snapshot));
    if (status != EOS_PORT_STATUS_OK) {
        name[0] = '\0';
        return eos_fd_fail_status(status, "hostname");
    }
    return eos_fs_copy_truncated(snapshot, name, capacity,
                                 EOS_ERRNO_NAME_TOO_LONG);
}
