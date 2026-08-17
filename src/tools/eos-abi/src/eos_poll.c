#include <stdint.h>

static uint32_t eos_poll_timeout_ticks(int32_t timeout_milliseconds) {
    uint64_t ticks;
    uint32_t rate;
    if (timeout_milliseconds < 0) return EOS_PORT_WAIT_FOREVER;
    if (timeout_milliseconds == 0) return EOS_PORT_NO_WAIT;
    rate = eos_port_tick_rate_hz();
    if (rate == 0) return EOS_PORT_MAX_FINITE_WAIT;
    ticks = ((uint64_t)(uint32_t)timeout_milliseconds * rate + UINT64_C(999)) /
            UINT64_C(1000);
    if (ticks == 0) ticks = 1;
    if (ticks > EOS_PORT_MAX_FINITE_WAIT) ticks = EOS_PORT_MAX_FINITE_WAIT;
    return (uint32_t)ticks;
}

int32_t eos_rust_poll(eos_rust_pollfd *descriptors,
                      uint32_t descriptor_count,
                      int32_t timeout_milliseconds) {
    eos_fd_reference references[EOS_FD_TABLE_CAPACITY];
    eos_port_socket sockets[EOS_FD_TABLE_CAPACITY];
    uint32_t requested[EOS_FD_TABLE_CAPACITY];
    uint32_t observed[EOS_FD_TABLE_CAPACITY];
    uint32_t entry_to_socket[EOS_FD_TABLE_CAPACITY];
    uint32_t socket_count = 0;
    uint32_t index;
    int32_t original_errno;
    int32_t error;
    int32_t ready = 0;
    if (descriptor_count > EOS_FD_TABLE_CAPACITY) {
        return eos_socket_fail(EOS_ERRNO_INVALID);
    }
    if (descriptor_count != 0 && descriptors == NULL) {
        return eos_socket_fail(EOS_ERRNO_FAULT);
    }
    if (timeout_milliseconds < -1) return eos_socket_fail(EOS_ERRNO_INVALID);
    original_errno = *eos_tls_errno_location();
    for (index = 0; index < descriptor_count; ++index) {
        int16_t events = descriptors[index].events;
        descriptors[index].revents = 0;
        entry_to_socket[index] = UINT32_MAX;
        if (descriptors[index].fd < 0) continue;
        if ((events & ~(EOS_RUST_POLLIN | EOS_RUST_POLLPRI |
                        EOS_RUST_POLLOUT | EOS_RUST_POLLERR |
                        EOS_RUST_POLLHUP | EOS_RUST_POLLNVAL)) != 0) {
            while (socket_count != 0) {
                --socket_count;
                eos_fd_release_or_abort(&references[socket_count]);
            }
            return eos_socket_fail(EOS_ERRNO_INVALID);
        }
        if (eos_fd_acquire(descriptors[index].fd, EOS_FD_KIND_SOCKET,
                           &references[socket_count]) != 0) {
            if (*eos_tls_errno_location() == EOS_ERRNO_BAD_DESCRIPTOR) {
                descriptors[index].revents = EOS_RUST_POLLNVAL;
                ++ready;
                *eos_tls_errno_location() = original_errno;
                continue;
            }
            while (socket_count != 0) {
                --socket_count;
                eos_fd_release_or_abort(&references[socket_count]);
            }
            return -1;
        }
        sockets[socket_count] =
            ((eos_socket *)references[socket_count].native.pointer)->native;
        requested[socket_count] = 0;
        if ((events & EOS_RUST_POLLIN) != 0) {
            requested[socket_count] |= EOS_PORT_SOCKET_EVENT_READ;
        }
        if ((events & EOS_RUST_POLLOUT) != 0) {
            requested[socket_count] |= EOS_PORT_SOCKET_EVENT_WRITE;
        }
        if ((events & EOS_RUST_POLLPRI) != 0) {
            requested[socket_count] |= EOS_PORT_SOCKET_EVENT_PRIORITY;
        }
        observed[socket_count] = 0;
        entry_to_socket[index] = socket_count;
        ++socket_count;
    }
    error = eos_port_socket_poll(
        sockets, requested, observed, socket_count,
        ready != 0 ? EOS_PORT_NO_WAIT
                   : eos_poll_timeout_ticks(timeout_milliseconds));
    if (error != 0) {
        while (socket_count != 0) {
            --socket_count;
            eos_fd_release_or_abort(&references[socket_count]);
        }
        return eos_socket_fail(error);
    }
    for (index = 0; index < descriptor_count; ++index) {
        uint32_t socket_index = entry_to_socket[index];
        int16_t revents = descriptors[index].revents;
        if (socket_index == UINT32_MAX) continue;
        if ((observed[socket_index] & EOS_PORT_SOCKET_EVENT_READ) != 0) {
            revents |= EOS_RUST_POLLIN;
        }
        if ((observed[socket_index] & EOS_PORT_SOCKET_EVENT_WRITE) != 0) {
            revents |= EOS_RUST_POLLOUT;
        }
        if ((observed[socket_index] & EOS_PORT_SOCKET_EVENT_ERROR) != 0) {
            revents |= EOS_RUST_POLLERR;
        }
        if ((observed[socket_index] & EOS_PORT_SOCKET_EVENT_HANGUP) != 0) {
            revents |= EOS_RUST_POLLHUP;
        }
        if ((observed[socket_index] & EOS_PORT_SOCKET_EVENT_PRIORITY) != 0) {
            revents |= EOS_RUST_POLLPRI;
        }
        descriptors[index].revents = revents;
        if (revents != 0) ++ready;
    }
    while (socket_count != 0) {
        --socket_count;
        eos_fd_release_or_abort(&references[socket_count]);
    }
    *eos_tls_errno_location() = original_errno;
    return ready;
}
