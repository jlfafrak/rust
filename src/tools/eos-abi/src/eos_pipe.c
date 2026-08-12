#include <string.h>

#define EOS_PIPE_CAPACITY UINT32_C(4096)
#define EOS_PIPE_EVENT_READABLE UINT32_C(1)
#define EOS_PIPE_EVENT_WRITABLE UINT32_C(2)

typedef struct eos_pipe_state {
    eos_port_sync sync;
    uint8_t buffer[EOS_PIPE_CAPACITY];
    uint32_t head;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
    uint32_t endpoint_objects;
} eos_pipe_state;

typedef struct eos_pipe_endpoint {
    eos_pipe_state *pipe;
    uint32_t writer;
    uint32_t status_flags;
} eos_pipe_endpoint;

static void eos_pipe_unlock(eos_pipe_state *pipe) {
    if (eos_port_sync_unlock(pipe->sync) != EOS_PORT_STATUS_OK) eos_rust_abort();
}

static int32_t eos_pipe_lock(eos_pipe_state *pipe, const char *operation) {
    int32_t status = eos_port_sync_lock(pipe->sync);
    return status == EOS_PORT_STATUS_OK ? 0
                                       : eos_fd_fail_status(status, operation);
}

static void eos_pipe_broadcast(eos_pipe_state *pipe, uint32_t events) {
    if (eos_port_sync_broadcast(pipe->sync, events) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static void eos_pipe_endpoint_destroy(eos_fd_native native) {
    eos_pipe_endpoint *endpoint = (eos_pipe_endpoint *)native.pointer;
    eos_pipe_state *pipe;
    uint32_t final;
    if (endpoint == NULL) return;
    pipe = endpoint->pipe;
    if (eos_port_sync_lock(pipe->sync) != EOS_PORT_STATUS_OK) eos_rust_abort();
    if (endpoint->writer != UINT32_C(0)) {
        if (pipe->writers == UINT32_C(0)) eos_rust_abort();
        --pipe->writers;
        if (pipe->writers == UINT32_C(0)) {
            eos_pipe_broadcast(pipe, EOS_PIPE_EVENT_READABLE);
        }
    } else {
        if (pipe->readers == UINT32_C(0)) eos_rust_abort();
        --pipe->readers;
        if (pipe->readers == UINT32_C(0)) {
            eos_pipe_broadcast(pipe, EOS_PIPE_EVENT_WRITABLE);
        }
    }
    if (pipe->endpoint_objects == UINT32_C(0)) eos_rust_abort();
    --pipe->endpoint_objects;
    final = pipe->endpoint_objects == UINT32_C(0);
    eos_pipe_unlock(pipe);
    eos_fs_free_or_abort(endpoint);
    if (final) {
        eos_fs_destroy_sync_or_abort(pipe->sync);
        eos_fs_free_or_abort(pipe);
    }
}

int32_t eos_rust_pipe(eos_rust_fd_t descriptors[2], uint32_t flags) {
    eos_pipe_state *pipe = NULL;
    eos_pipe_endpoint *reader = NULL;
    eos_pipe_endpoint *writer = NULL;
    eos_fd_native native;
    int32_t status;
    int32_t read_descriptor;
    int32_t write_descriptor;
    uint32_t descriptor_flags;
    if (descriptors == NULL) return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    descriptors[0] = -1;
    descriptors[1] = -1;
    if ((flags & ~(EOS_RUST_O_NONBLOCK | EOS_RUST_O_CLOEXEC)) != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    status = eos_port_memory_alloc((uint32_t)sizeof(*pipe), (void **)&pipe);
    if (status == EOS_PORT_STATUS_OK) {
        status = eos_port_memory_alloc((uint32_t)sizeof(*reader),
                                       (void **)&reader);
    }
    if (status == EOS_PORT_STATUS_OK) {
        status = eos_port_memory_alloc((uint32_t)sizeof(*writer),
                                       (void **)&writer);
    }
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_free_or_abort(writer);
        eos_fs_free_or_abort(reader);
        eos_fs_free_or_abort(pipe);
        return eos_fd_fail_status(status, "pipe.allocate");
    }
    (void)memset(pipe, 0, sizeof(*pipe));
    status = eos_port_sync_create(&pipe->sync);
    if (status != EOS_PORT_STATUS_OK) {
        eos_fs_free_or_abort(writer);
        eos_fs_free_or_abort(reader);
        eos_fs_free_or_abort(pipe);
        return eos_fd_fail_status(status, "pipe.sync.create");
    }
    pipe->readers = UINT32_C(1);
    pipe->writers = UINT32_C(1);
    pipe->endpoint_objects = UINT32_C(2);
    reader->pipe = pipe;
    reader->writer = UINT32_C(0);
    reader->status_flags = EOS_RUST_O_RDONLY | (flags & EOS_RUST_O_NONBLOCK);
    writer->pipe = pipe;
    writer->writer = UINT32_C(1);
    writer->status_flags = EOS_RUST_O_WRONLY | (flags & EOS_RUST_O_NONBLOCK);
    descriptor_flags = (flags & EOS_RUST_O_CLOEXEC) != 0
                           ? EOS_FD_FLAG_CLOEXEC
                           : UINT32_C(0);
    native.pointer = reader;
    read_descriptor = eos_fd_allocate(EOS_FD_KIND_PIPE_READER, native,
                                      eos_pipe_endpoint_destroy,
                                      descriptor_flags);
    if (read_descriptor < 0) {
        native.pointer = writer;
        eos_pipe_endpoint_destroy(native);
        native.pointer = reader;
        eos_pipe_endpoint_destroy(native);
        return -1;
    }
    native.pointer = writer;
    write_descriptor = eos_fd_allocate(EOS_FD_KIND_PIPE_WRITER, native,
                                       eos_pipe_endpoint_destroy,
                                       descriptor_flags);
    if (write_descriptor < 0) {
        eos_pipe_endpoint_destroy(native);
        if (eos_fd_close(read_descriptor) != 0) eos_rust_abort();
        return -1;
    }
    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;
    return 0;
}

static int32_t eos_pipe_read(eos_pipe_endpoint *endpoint, void *buffer,
                             uint32_t byte_count) {
    eos_pipe_state *pipe = endpoint->pipe;
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t completed;
    uint32_t first;
    int32_t status;
    if (endpoint->writer != UINT32_C(0)) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_pipe_lock(pipe, "pipe.read.lock") != 0) return -1;
    for (;;) {
        if (pipe->size != UINT32_C(0)) {
            completed = byte_count < pipe->size ? byte_count : pipe->size;
            first = completed;
            if (first > EOS_PIPE_CAPACITY - pipe->head) {
                first = EOS_PIPE_CAPACITY - pipe->head;
            }
            (void)memcpy(bytes, pipe->buffer + pipe->head, first);
            if (completed > first) {
                (void)memcpy(bytes + first, pipe->buffer, completed - first);
            }
            pipe->head = (pipe->head + completed) % EOS_PIPE_CAPACITY;
            pipe->size -= completed;
            eos_pipe_broadcast(pipe, EOS_PIPE_EVENT_WRITABLE);
            eos_pipe_unlock(pipe);
            return (int32_t)completed;
        }
        if (pipe->writers == UINT32_C(0)) {
            eos_pipe_unlock(pipe);
            return 0;
        }
        if ((endpoint->status_flags & EOS_RUST_O_NONBLOCK) != 0) {
            eos_pipe_unlock(pipe);
            return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
        }
        status = eos_port_sync_wait(pipe->sync, EOS_PIPE_EVENT_READABLE);
        if (status != EOS_PORT_STATUS_OK) {
            eos_pipe_unlock(pipe);
            return eos_fd_fail_status(status, "pipe.read.wait");
        }
    }
}

static int32_t eos_pipe_write(eos_pipe_endpoint *endpoint, const void *buffer,
                              uint32_t byte_count) {
    eos_pipe_state *pipe = endpoint->pipe;
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint32_t available;
    uint32_t completed;
    uint32_t tail;
    uint32_t first;
    int32_t status;
    if (endpoint->writer == UINT32_C(0)) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_pipe_lock(pipe, "pipe.write.lock") != 0) return -1;
    for (;;) {
        if (pipe->readers == UINT32_C(0)) {
            eos_pipe_unlock(pipe);
            return eos_fd_fail_errno(EOS_ERRNO_PIPE);
        }
        available = EOS_PIPE_CAPACITY - pipe->size;
        if (available != UINT32_C(0)) {
            completed = byte_count < available ? byte_count : available;
            tail = (pipe->head + pipe->size) % EOS_PIPE_CAPACITY;
            first = completed;
            if (first > EOS_PIPE_CAPACITY - tail) {
                first = EOS_PIPE_CAPACITY - tail;
            }
            (void)memcpy(pipe->buffer + tail, bytes, first);
            if (completed > first) {
                (void)memcpy(pipe->buffer, bytes + first, completed - first);
            }
            pipe->size += completed;
            eos_pipe_broadcast(pipe, EOS_PIPE_EVENT_READABLE);
            eos_pipe_unlock(pipe);
            return (int32_t)completed;
        }
        if ((endpoint->status_flags & EOS_RUST_O_NONBLOCK) != 0) {
            eos_pipe_unlock(pipe);
            return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
        }
        status = eos_port_sync_wait(pipe->sync, EOS_PIPE_EVENT_WRITABLE);
        if (status != EOS_PORT_STATUS_OK) {
            eos_pipe_unlock(pipe);
            return eos_fd_fail_status(status, "pipe.write.wait");
        }
    }
}
