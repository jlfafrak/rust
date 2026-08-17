#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct eos_socket {
    eos_port_socket native;
    int32_t domain;
    int32_t type;
    int32_t protocol;
    uint32_t status_flags;
    eos_rust_timeval receive_timeout;
    eos_rust_timeval send_timeout;
    int32_t pending_error;
    uint32_t shutdown_state;
    eos_port_mutex state_lock;
} eos_socket;

static int32_t eos_socket_fail(int32_t error_number) {
    return eos_fd_fail_errno(error_number == 0 ? EOS_ERRNO_IO : error_number);
}

static void eos_socket_lock(eos_socket *socket) {
    if (eos_port_mutex_lock(socket->state_lock, EOS_PORT_WAIT_FOREVER) !=
        EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static void eos_socket_unlock(eos_socket *socket) {
    if (eos_port_mutex_unlock(socket->state_lock) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static void eos_socket_destroy(eos_fd_native native) {
    eos_socket *socket = (eos_socket *)native.pointer;
    if (socket == NULL) eos_rust_abort();
    if (eos_port_socket_close(socket->native) != 0) eos_rust_abort();
    if (eos_port_mutex_destroy(socket->state_lock) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (eos_port_memory_free(socket) != EOS_PORT_STATUS_OK) eos_rust_abort();
}

static int32_t eos_socket_validate_creation(int32_t domain,
                                            int32_t type,
                                            int32_t protocol) {
    int32_t expected;
    if (domain != EOS_RUST_AF_INET && domain != EOS_RUST_AF_INET6) {
        return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
    }
    if (type != EOS_RUST_SOCK_STREAM && type != EOS_RUST_SOCK_DGRAM) {
        return EOS_ERRNO_PROTOCOL_TYPE;
    }
    expected = type == EOS_RUST_SOCK_STREAM ? EOS_RUST_IPPROTO_TCP
                                            : EOS_RUST_IPPROTO_UDP;
    if (protocol != EOS_RUST_IPPROTO_IP && protocol != expected) {
        return EOS_ERRNO_PROTOCOL_NOT_SUPPORTED;
    }
    return 0;
}

static int32_t eos_socket_public_to_port(
    const eos_rust_sockaddr *address,
    eos_rust_socklen_t address_length,
    eos_port_socket_address *converted) {
    uint16_t family = 0;
    if (address == NULL) return EOS_ERRNO_FAULT;
    if (address_length < (eos_rust_socklen_t)sizeof(family)) {
        return EOS_ERRNO_INVALID;
    }
    (void)memcpy(&family, address, sizeof(family));
    (void)memset(converted, 0, sizeof(*converted));
    if (family == EOS_RUST_AF_INET) {
        eos_rust_sockaddr_in value;
        if (address_length < (eos_rust_socklen_t)sizeof(value)) {
            return EOS_ERRNO_INVALID;
        }
        (void)memcpy(&value, address, sizeof(value));
        converted->family = family;
        converted->port = value.sin_port;
        (void)memcpy(converted->address, &value.sin_addr.s_addr,
                     sizeof(value.sin_addr.s_addr));
        return 0;
    }
    if (family == EOS_RUST_AF_INET6) {
        eos_rust_sockaddr_in6 value;
        if (address_length < (eos_rust_socklen_t)sizeof(value)) {
            return EOS_ERRNO_INVALID;
        }
        (void)memcpy(&value, address, sizeof(value));
        converted->family = family;
        converted->port = value.sin6_port;
        converted->flowinfo = value.sin6_flowinfo;
        converted->scope_id = value.sin6_scope_id;
        (void)memcpy(converted->address, value.sin6_addr.s6_addr,
                     sizeof(converted->address));
        return 0;
    }
    return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
}

static int32_t eos_socket_port_to_public(
    const eos_port_socket_address *address,
    eos_rust_sockaddr *destination,
    eos_rust_socklen_t *destination_length) {
    eos_rust_sockaddr_storage initialized;
    eos_rust_socklen_t required;
    eos_rust_socklen_t copied;
    if (destination_length == NULL) return EOS_ERRNO_FAULT;
    if (destination == NULL) return EOS_ERRNO_FAULT;
    (void)memset(&initialized, 0, sizeof(initialized));
    if (address->family == EOS_RUST_AF_INET) {
        eos_rust_sockaddr_in value;
        (void)memset(&value, 0, sizeof(value));
        value.sin_family = EOS_RUST_AF_INET;
        value.sin_port = address->port;
        (void)memcpy(&value.sin_addr.s_addr, address->address,
                     sizeof(value.sin_addr.s_addr));
        (void)memcpy(&initialized, &value, sizeof(value));
        required = (eos_rust_socklen_t)sizeof(value);
    } else if (address->family == EOS_RUST_AF_INET6) {
        eos_rust_sockaddr_in6 value;
        (void)memset(&value, 0, sizeof(value));
        value.sin6_family = EOS_RUST_AF_INET6;
        value.sin6_port = address->port;
        value.sin6_flowinfo = address->flowinfo;
        value.sin6_scope_id = address->scope_id;
        (void)memcpy(value.sin6_addr.s6_addr, address->address,
                     sizeof(value.sin6_addr.s6_addr));
        (void)memcpy(&initialized, &value, sizeof(value));
        required = (eos_rust_socklen_t)sizeof(value);
    } else {
        return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
    }
    copied = *destination_length < required ? *destination_length : required;
    if (copied != 0) (void)memcpy(destination, &initialized, copied);
    *destination_length = required;
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
int32_t eos_socket_test_ipv6_output(uint32_t scope_id,
                                    eos_rust_sockaddr_in6 *destination) {
    eos_port_socket_address address;
    eos_rust_socklen_t length = (eos_rust_socklen_t)sizeof(*destination);
    (void)memset(&address, 0, sizeof(address));
    address.family = EOS_RUST_AF_INET6;
    address.scope_id = scope_id;
    return eos_socket_port_to_public(&address,
                                     (eos_rust_sockaddr *)destination,
                                     &length);
}
#endif

static int32_t eos_socket_acquire(eos_rust_fd_t descriptor,
                                  eos_fd_reference *reference,
                                  eos_socket **socket) {
    if (eos_fd_acquire(descriptor, EOS_FD_KIND_SOCKET, reference) != 0) {
        return -1;
    }
    *socket = (eos_socket *)reference->native.pointer;
    if (*socket == NULL) eos_rust_abort();
    return 0;
}

static int32_t eos_socket_release(eos_fd_reference *reference,
                                  int32_t result) {
    eos_fd_release_or_abort(reference);
    return result;
}

static int32_t eos_socket_flags(int32_t flags, uint32_t nonblocking,
                                int32_t *native_flags) {
    const int32_t allowed = EOS_RUST_MSG_OOB | EOS_RUST_MSG_PEEK |
                            EOS_RUST_MSG_DONTROUTE |
                            EOS_RUST_MSG_DONTWAIT | EOS_RUST_MSG_NOSIGNAL;
    if ((flags & ~allowed) != 0) return EOS_ERRNO_INVALID;
    *native_flags = flags;
    if (nonblocking != 0) *native_flags |= EOS_RUST_MSG_DONTWAIT;
    return 0;
}

static int32_t eos_socket_transfer_result(eos_port_socket_io_result result) {
    if (result.count >= 0) return result.count;
    return eos_socket_fail(result.error_number);
}

static int32_t eos_socket_send_held(eos_socket *socket,
                                    const void *buffer,
                                    uint32_t byte_count,
                                    int32_t flags) {
    uint32_t nonblocking;
    int32_t native_flags;
    int32_t error = 0;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    eos_socket_lock(socket);
    nonblocking = socket->status_flags & EOS_RUST_O_NONBLOCK;
    eos_socket_unlock(socket);
    error = eos_socket_flags(flags, nonblocking, &native_flags);
    if (error != 0) return eos_socket_fail(error);
    return eos_socket_transfer_result(eos_port_socket_send(
        socket->native, buffer, byte_count, native_flags));
}

static int32_t eos_socket_receive_held(eos_socket *socket,
                                       void *buffer,
                                       uint32_t byte_count,
                                       int32_t flags) {
    uint32_t nonblocking;
    int32_t native_flags;
    int32_t error;
    if (byte_count > (uint32_t)INT32_MAX) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (buffer == NULL && byte_count != 0) {
        return eos_fd_fail_errno(EOS_ERRNO_FAULT);
    }
    eos_socket_lock(socket);
    nonblocking = socket->status_flags & EOS_RUST_O_NONBLOCK;
    eos_socket_unlock(socket);
    error = eos_socket_flags(flags, nonblocking, &native_flags);
    if (error != 0) return eos_socket_fail(error);
    return eos_socket_transfer_result(eos_port_socket_receive(
        socket->native, buffer, byte_count, native_flags));
}

static uint32_t eos_socket_timeout_ticks(const eos_rust_timeval *value,
                                         int32_t *error) {
    uint64_t ticks;
    uint64_t fractional;
    uint32_t rate = eos_port_tick_rate_hz();
    *error = 0;
    if (value->tv_sec < 0 || value->tv_usec < 0 ||
        value->tv_usec >= INT64_C(1000000) || rate == 0) {
        *error = EOS_ERRNO_INVALID;
        return 0;
    }
    if (value->tv_sec == 0 && value->tv_usec == 0) {
        return EOS_PORT_WAIT_FOREVER;
    }
    if ((uint64_t)value->tv_sec >
        (uint64_t)EOS_PORT_MAX_FINITE_WAIT / rate) {
        *error = EOS_ERRNO_OVERFLOW;
        return 0;
    }
    ticks = (uint64_t)value->tv_sec * rate;
    fractional = ((uint64_t)value->tv_usec * rate + UINT64_C(999999)) /
                 UINT64_C(1000000);
    if (ticks > EOS_PORT_MAX_FINITE_WAIT - fractional) {
        *error = EOS_ERRNO_OVERFLOW;
        return 0;
    }
    ticks += fractional;
    if (ticks == 0) ticks = 1;
    return (uint32_t)ticks;
}

static int32_t eos_socket_configure_nonblocking(eos_socket *socket,
                                                uint32_t enabled) {
    int32_t error = 0;
    int32_t receive_error;
    int32_t send_error;
    uint32_t old_enabled;
    uint32_t old_receive;
    uint32_t old_send;
    uint32_t new_receive;
    uint32_t new_send;
    eos_socket_lock(socket);
    old_enabled = (socket->status_flags & EOS_RUST_O_NONBLOCK) != 0;
    if (old_enabled == enabled) {
        eos_socket_unlock(socket);
        return 0;
    }
    old_receive = old_enabled ? EOS_PORT_NO_WAIT
                              : eos_socket_timeout_ticks(
                                    &socket->receive_timeout, &error);
    if (error != 0) eos_rust_abort();
    error = 0;
    old_send = old_enabled ? EOS_PORT_NO_WAIT
                           : eos_socket_timeout_ticks(&socket->send_timeout,
                                                      &error);
    if (error != 0) eos_rust_abort();
    error = 0;
    new_receive = enabled ? EOS_PORT_NO_WAIT
                          : eos_socket_timeout_ticks(&socket->receive_timeout,
                                                     &error);
    if (error != 0) {
        eos_socket_unlock(socket);
        return eos_socket_fail(error);
    }
    error = 0;
    new_send = enabled ? EOS_PORT_NO_WAIT
                       : eos_socket_timeout_ticks(&socket->send_timeout,
                                                  &error);
    if (error != 0) {
        eos_socket_unlock(socket);
        return eos_socket_fail(error);
    }
    receive_error = eos_port_socket_set_timeout(socket->native, 1,
                                                new_receive);
    if (receive_error != 0) {
        eos_socket_unlock(socket);
        return eos_socket_fail(receive_error);
    }
    send_error = eos_port_socket_set_timeout(socket->native, 0, new_send);
    if (send_error != 0) {
        if (eos_port_socket_set_timeout(socket->native, 1, old_receive) != 0) {
            eos_rust_abort();
        }
        eos_socket_unlock(socket);
        return eos_socket_fail(send_error);
    }
    error = eos_port_socket_set_nonblocking(socket->native, enabled);
    if (error != 0) {
        if (eos_port_socket_set_timeout(socket->native, 0, old_send) != 0 ||
            eos_port_socket_set_timeout(socket->native, 1, old_receive) != 0) {
            eos_rust_abort();
        }
        eos_socket_unlock(socket);
        return eos_socket_fail(error);
    }
    if (enabled != 0) socket->status_flags |= EOS_RUST_O_NONBLOCK;
    else socket->status_flags &= ~EOS_RUST_O_NONBLOCK;
    eos_socket_unlock(socket);
    return 0;
}

static int32_t eos_socket_status_flags(eos_socket *socket,
                                       int32_t command,
                                       int32_t argument) {
    if (command == EOS_RUST_F_GETFL) {
        int32_t result;
        eos_socket_lock(socket);
        result = (int32_t)socket->status_flags;
        eos_socket_unlock(socket);
        return result;
    }
    if (((uint32_t)argument & ~(EOS_RUST_O_ACCMODE |
                                EOS_RUST_O_NONBLOCK)) != 0) {
        return eos_socket_fail(EOS_ERRNO_INVALID);
    }
    return eos_socket_configure_nonblocking(
        socket, ((uint32_t)argument & EOS_RUST_O_NONBLOCK) != 0);
}

static int32_t eos_socket_publish(eos_port_socket native,
                                  int32_t domain,
                                  int32_t type,
                                  int32_t protocol,
                                  uint32_t status_flags,
                                  const eos_rust_timeval *receive_timeout,
                                  const eos_rust_timeval *send_timeout) {
    eos_socket *socket = NULL;
    eos_fd_native fd_native;
    int32_t status;
    int32_t descriptor;
    status = eos_port_memory_alloc((uint32_t)sizeof(*socket),
                                   (void **)&socket);
    if (status != EOS_PORT_STATUS_OK) {
        if (eos_port_socket_close(native) != 0) eos_rust_abort();
        return eos_fd_fail_status(status, "socket.object.allocate");
    }
    (void)memset(socket, 0, sizeof(*socket));
    socket->native = native;
    socket->domain = domain;
    socket->type = type;
    socket->protocol = protocol == 0
                           ? (type == EOS_RUST_SOCK_STREAM
                                  ? EOS_RUST_IPPROTO_TCP
                                  : EOS_RUST_IPPROTO_UDP)
                           : protocol;
    socket->status_flags = EOS_RUST_O_RDWR |
                           (status_flags & EOS_RUST_O_NONBLOCK);
    if (receive_timeout != NULL) socket->receive_timeout = *receive_timeout;
    if (send_timeout != NULL) socket->send_timeout = *send_timeout;
    status = eos_port_mutex_create(UINT32_C(0), &socket->state_lock);
    if (status != EOS_PORT_STATUS_OK) {
        if (eos_port_socket_close(native) != 0 ||
            eos_port_memory_free(socket) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
        return eos_fd_fail_status(status, "socket.lock.create");
    }
    if (receive_timeout != NULL || send_timeout != NULL ||
        (status_flags & EOS_RUST_O_NONBLOCK) != 0) {
        uint32_t receive_ticks;
        uint32_t send_ticks;
        int32_t error = 0;
        receive_ticks = (status_flags & EOS_RUST_O_NONBLOCK) != 0
                            ? EOS_PORT_NO_WAIT
                            : eos_socket_timeout_ticks(
                                  &socket->receive_timeout, &error);
        if (error == 0) {
            send_ticks = (status_flags & EOS_RUST_O_NONBLOCK) != 0
                             ? EOS_PORT_NO_WAIT
                             : eos_socket_timeout_ticks(
                                   &socket->send_timeout, &error);
        } else {
            send_ticks = EOS_PORT_WAIT_FOREVER;
        }
        if (error == 0) {
            error = eos_port_socket_set_timeout(native, 1, receive_ticks);
        }
        if (error == 0) {
            error = eos_port_socket_set_timeout(native, 0, send_ticks);
        }
        if (error == 0) {
            error = eos_port_socket_set_nonblocking(
                native, (status_flags & EOS_RUST_O_NONBLOCK) != 0);
        }
        if (error != 0) {
            if (eos_port_socket_close(native) != 0 ||
                eos_port_mutex_destroy(socket->state_lock) !=
                    EOS_PORT_STATUS_OK ||
                eos_port_memory_free(socket) != EOS_PORT_STATUS_OK) {
                eos_rust_abort();
            }
            return eos_socket_fail(error);
        }
    }
    fd_native.pointer = socket;
    descriptor = eos_fd_allocate(EOS_FD_KIND_SOCKET, fd_native,
                                 eos_socket_destroy, UINT32_C(0));
    if (descriptor < 0) {
        if (eos_port_socket_close(native) != 0 ||
            eos_port_mutex_destroy(socket->state_lock) != EOS_PORT_STATUS_OK ||
            eos_port_memory_free(socket) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
    }
    return descriptor;
}

eos_rust_fd_t eos_rust_socket(int32_t domain, int32_t type,
                              int32_t protocol) {
    eos_port_socket_create_result created;
    int32_t error = eos_socket_validate_creation(domain, type, protocol);
    if (error != 0) return eos_socket_fail(error);
    created = eos_port_socket_create(domain, type, protocol);
    if (created.socket == EOS_PORT_SOCKET_INVALID) {
        return eos_socket_fail(created.error_number);
    }
    return eos_socket_publish(created.socket, domain, type, protocol, 0,
                              NULL, NULL);
}

int32_t eos_rust_bind(eos_rust_fd_t descriptor,
                      const eos_rust_sockaddr *address,
                      eos_rust_socklen_t address_length) {
    eos_port_socket_address converted;
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error = eos_socket_public_to_port(address, address_length,
                                              &converted);
    if (error != 0) return eos_socket_fail(error);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    if (socket->domain != converted.family) error = EOS_ERRNO_INVALID;
    else error = eos_port_socket_bind(socket->native, &converted);
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

int32_t eos_rust_connect(eos_rust_fd_t descriptor,
                         const eos_rust_sockaddr *address,
                         eos_rust_socklen_t address_length) {
    eos_port_socket_address converted;
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error = eos_socket_public_to_port(address, address_length,
                                              &converted);
    if (error != 0) return eos_socket_fail(error);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    if (socket->domain != converted.family) error = EOS_ERRNO_INVALID;
    else error = eos_port_socket_connect(socket->native, &converted);
    if (error != 0) {
        eos_socket_lock(socket);
        socket->pending_error = error;
        eos_socket_unlock(socket);
    }
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

int32_t eos_rust_listen(eos_rust_fd_t descriptor, int32_t backlog) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error;
    if (backlog < 0) return eos_socket_fail(EOS_ERRNO_INVALID);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    error = socket->type == EOS_RUST_SOCK_STREAM
                ? eos_port_socket_listen(socket->native, backlog)
                : EOS_ERRNO_PROTOCOL_TYPE;
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

eos_rust_fd_t eos_rust_accept(eos_rust_fd_t descriptor,
                              eos_rust_sockaddr *address,
                              eos_rust_socklen_t *address_length) {
    eos_port_socket_address converted;
    eos_port_socket_create_result accepted;
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t result;
    int32_t error;
    uint32_t status_flags;
    eos_rust_timeval receive_timeout;
    eos_rust_timeval send_timeout;
    if ((address == NULL) != (address_length == NULL)) {
        return eos_socket_fail(EOS_ERRNO_FAULT);
    }
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    if (socket->type != EOS_RUST_SOCK_STREAM) {
        return eos_socket_release(&reference,
                                  eos_socket_fail(EOS_ERRNO_PROTOCOL_TYPE));
    }
    accepted = eos_port_socket_accept(socket->native, &converted);
    if (accepted.socket == EOS_PORT_SOCKET_INVALID) {
        return eos_socket_release(
            &reference, eos_socket_fail(accepted.error_number));
    }
    if (address != NULL) {
        error = eos_socket_port_to_public(&converted, address,
                                          address_length);
        if (error != 0) {
            if (eos_port_socket_close(accepted.socket) != 0) eos_rust_abort();
            return eos_socket_release(&reference, eos_socket_fail(error));
        }
    }
    eos_socket_lock(socket);
    status_flags = socket->status_flags;
    receive_timeout = socket->receive_timeout;
    send_timeout = socket->send_timeout;
    eos_socket_unlock(socket);
    result = eos_socket_publish(accepted.socket, socket->domain, socket->type,
                                socket->protocol, status_flags,
                                &receive_timeout, &send_timeout);
    return eos_socket_release(&reference, result);
}

int32_t eos_rust_send(eos_rust_fd_t descriptor, const void *buffer,
                      uint32_t byte_count, int32_t flags) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) return eos_socket_fail(EOS_ERRNO_INVALID);
    if (buffer == NULL && byte_count != 0) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    result = eos_socket_send_held(socket, buffer, byte_count, flags);
    return eos_socket_release(&reference, result);
}

int32_t eos_rust_recv(eos_rust_fd_t descriptor, void *buffer,
                      uint32_t byte_count, int32_t flags) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t result;
    if (byte_count > (uint32_t)INT32_MAX) return eos_socket_fail(EOS_ERRNO_INVALID);
    if (buffer == NULL && byte_count != 0) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    result = eos_socket_receive_held(socket, buffer, byte_count, flags);
    return eos_socket_release(&reference, result);
}

int32_t eos_rust_sendto(eos_rust_fd_t descriptor, const void *buffer,
                        uint32_t byte_count, int32_t flags,
                        const eos_rust_sockaddr *destination,
                        eos_rust_socklen_t destination_length) {
    eos_port_socket_address converted;
    eos_fd_reference reference;
    eos_socket *socket;
    eos_port_socket_io_result port_result;
    uint32_t nonblocking;
    int32_t native_flags;
    int32_t error;
    if (byte_count > (uint32_t)INT32_MAX) return eos_socket_fail(EOS_ERRNO_INVALID);
    if (buffer == NULL && byte_count != 0) return eos_socket_fail(EOS_ERRNO_FAULT);
    error = eos_socket_public_to_port(destination, destination_length,
                                      &converted);
    if (error != 0) return eos_socket_fail(error);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    eos_socket_lock(socket);
    nonblocking = socket->status_flags & EOS_RUST_O_NONBLOCK;
    eos_socket_unlock(socket);
    error = eos_socket_flags(flags, nonblocking, &native_flags);
    if (error == 0 && socket->domain != converted.family) {
        error = EOS_ERRNO_INVALID;
    }
    if (error != 0) {
        return eos_socket_release(&reference, eos_socket_fail(error));
    }
    port_result = eos_port_socket_send_to(socket->native, buffer, byte_count,
                                          native_flags, &converted);
    return eos_socket_release(&reference,
                              eos_socket_transfer_result(port_result));
}

int32_t eos_rust_recvfrom(eos_rust_fd_t descriptor, void *buffer,
                          uint32_t byte_count, int32_t flags,
                          eos_rust_sockaddr *source,
                          eos_rust_socklen_t *source_length) {
    eos_port_socket_address converted;
    eos_fd_reference reference;
    eos_socket *socket;
    eos_port_socket_io_result port_result;
    uint32_t nonblocking;
    int32_t native_flags;
    int32_t error;
    if (byte_count > (uint32_t)INT32_MAX) return eos_socket_fail(EOS_ERRNO_INVALID);
    if (buffer == NULL && byte_count != 0) return eos_socket_fail(EOS_ERRNO_FAULT);
    if ((source == NULL) != (source_length == NULL)) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    eos_socket_lock(socket);
    nonblocking = socket->status_flags & EOS_RUST_O_NONBLOCK;
    eos_socket_unlock(socket);
    error = eos_socket_flags(flags, nonblocking, &native_flags);
    if (error != 0) return eos_socket_release(&reference, eos_socket_fail(error));
    port_result = eos_port_socket_receive_from(socket->native, buffer,
                                               byte_count, native_flags,
                                               source == NULL ? NULL : &converted);
    if (port_result.count >= 0 && source != NULL) {
        if (converted.family == 0) {
            *source_length = 0;
            error = 0;
        } else {
            error = eos_socket_port_to_public(&converted, source,
                                              source_length);
        }
        if (error != 0) port_result = (eos_port_socket_io_result){-1, error};
    }
    return eos_socket_release(&reference,
                              eos_socket_transfer_result(port_result));
}

int32_t eos_rust_shutdown(eos_rust_fd_t descriptor, int32_t how) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error;
    if (how < EOS_RUST_SHUT_RD || how > EOS_RUST_SHUT_RDWR) {
        return eos_socket_fail(EOS_ERRNO_INVALID);
    }
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    error = eos_port_socket_shutdown(socket->native, how);
    if (error == 0) {
        eos_socket_lock(socket);
        socket->shutdown_state |= how == EOS_RUST_SHUT_RD
                                      ? UINT32_C(1)
                                      : how == EOS_RUST_SHUT_WR
                                            ? UINT32_C(2)
                                            : UINT32_C(3);
        eos_socket_unlock(socket);
    }
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

static int32_t eos_socket_address_query(eos_rust_fd_t descriptor,
                                        eos_rust_sockaddr *address,
                                        eos_rust_socklen_t *address_length,
                                        uint32_t peer) {
    eos_port_socket_address converted;
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error;
    if (address == NULL || address_length == NULL) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    error = peer ? eos_port_socket_remote_address(socket->native, &converted)
                 : eos_port_socket_local_address(socket->native, &converted);
    if (error == 0) error = eos_socket_port_to_public(&converted, address,
                                                      address_length);
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

int32_t eos_rust_getsockname(eos_rust_fd_t descriptor,
                             eos_rust_sockaddr *address,
                             eos_rust_socklen_t *address_length) {
    return eos_socket_address_query(descriptor, address, address_length, 0);
}

int32_t eos_rust_getpeername(eos_rust_fd_t descriptor,
                             eos_rust_sockaddr *address,
                             eos_rust_socklen_t *address_length) {
    return eos_socket_address_query(descriptor, address, address_length, 1);
}

int32_t eos_rust_setsockopt(eos_rust_fd_t descriptor, int32_t level,
                            int32_t option_name, const void *option_value,
                            eos_rust_socklen_t option_length) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error = 0;
    if (option_value == NULL) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (level != EOS_RUST_SOL_SOCKET && level != EOS_RUST_IPPROTO_IP &&
        level != EOS_RUST_IPPROTO_TCP && level != EOS_RUST_IPPROTO_IPV6) {
        return eos_socket_fail(EOS_ERRNO_INVALID);
    }
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    if (level == EOS_RUST_SOL_SOCKET &&
        (option_name == EOS_RUST_SO_RCVTIMEO ||
         option_name == EOS_RUST_SO_SNDTIMEO)) {
        eos_rust_timeval value;
        uint32_t ticks;
        uint32_t receive = option_name == EOS_RUST_SO_RCVTIMEO;
        if (option_length != sizeof(value)) error = EOS_ERRNO_INVALID;
        else {
            (void)memcpy(&value, option_value, sizeof(value));
            ticks = eos_socket_timeout_ticks(&value, &error);
            if (error == 0) {
                eos_socket_lock(socket);
                if ((socket->status_flags & EOS_RUST_O_NONBLOCK) == 0) {
                    error = eos_port_socket_set_timeout(socket->native,
                                                        receive, ticks);
                }
                if (error == 0) {
                    if (receive) socket->receive_timeout = value;
                    else socket->send_timeout = value;
                }
                eos_socket_unlock(socket);
            }
        }
    } else if (level == EOS_RUST_SOL_SOCKET &&
               (option_name == EOS_RUST_SO_SNDBUF ||
                option_name == EOS_RUST_SO_RCVBUF)) {
        int32_t value;
        if (option_length != sizeof(value)) error = EOS_ERRNO_INVALID;
        else {
            (void)memcpy(&value, option_value, sizeof(value));
            if (value <= 0) error = EOS_ERRNO_INVALID;
            else error = eos_port_socket_set_integer_option(socket->native,
                                                             option_name,
                                                             value);
        }
    } else {
        error = EOS_ERRNO_NO_PROTOCOL_OPTION;
    }
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}

int32_t eos_rust_getsockopt(eos_rust_fd_t descriptor, int32_t level,
                            int32_t option_name, void *option_value,
                            eos_rust_socklen_t *option_length) {
    eos_fd_reference reference;
    eos_socket *socket;
    int32_t error = 0;
    if (option_value == NULL || option_length == NULL) return eos_socket_fail(EOS_ERRNO_FAULT);
    if (level != EOS_RUST_SOL_SOCKET && level != EOS_RUST_IPPROTO_IP &&
        level != EOS_RUST_IPPROTO_TCP && level != EOS_RUST_IPPROTO_IPV6) {
        return eos_socket_fail(EOS_ERRNO_INVALID);
    }
    if (eos_socket_acquire(descriptor, &reference, &socket) != 0) return -1;
    if (level == EOS_RUST_SOL_SOCKET &&
        (option_name == EOS_RUST_SO_RCVTIMEO ||
         option_name == EOS_RUST_SO_SNDTIMEO)) {
        eos_rust_timeval value;
        if (*option_length < sizeof(value)) error = EOS_ERRNO_INVALID;
        else {
            eos_socket_lock(socket);
            value = option_name == EOS_RUST_SO_RCVTIMEO
                        ? socket->receive_timeout : socket->send_timeout;
            eos_socket_unlock(socket);
            (void)memcpy(option_value, &value, sizeof(value));
            *option_length = sizeof(value);
        }
    } else if (level == EOS_RUST_SOL_SOCKET &&
               option_name == EOS_RUST_SO_ERROR) {
        int32_t value;
        if (*option_length < sizeof(value)) error = EOS_ERRNO_INVALID;
        else {
            eos_socket_lock(socket);
            value = socket->pending_error;
            if (value == EOS_ERRNO_IN_PROGRESS || value == EOS_ERRNO_ALREADY) {
                int32_t observed = 0;
                error = eos_port_socket_connection_error(socket->native,
                                                          &observed);
                if (error == 0) value = observed;
            }
            if (error == 0) socket->pending_error = 0;
            eos_socket_unlock(socket);
            if (error == 0) {
                (void)memcpy(option_value, &value, sizeof(value));
                *option_length = sizeof(value);
            }
        }
    } else {
        error = EOS_ERRNO_NO_PROTOCOL_OPTION;
    }
    return eos_socket_release(&reference,
                              error == 0 ? 0 : eos_socket_fail(error));
}
