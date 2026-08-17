#ifndef EOS_PORT_MARTOS_NETWORK_CONTRACT_H
#define EOS_PORT_MARTOS_NETWORK_CONTRACT_H

#include <stdint.h>
#include <string.h>

typedef enum eos_martos_network_operation {
    EOS_MARTOS_NETWORK_OTHER = 0,
    EOS_MARTOS_NETWORK_CONNECT = 1,
    EOS_MARTOS_NETWORK_RECEIVE = 2,
    EOS_MARTOS_NETWORK_SEND = 3,
} eos_martos_network_operation;

static int32_t eos_martos_network_error(
    int32_t native_result,
    eos_martos_network_operation operation) {
    if (native_result >= 0) return 0;
    if (native_result == OS_NET_EWOULDBLOCK) {
        return operation == EOS_MARTOS_NETWORK_CONNECT
                   ? EOS_ERRNO_IN_PROGRESS : EOS_ERRNO_WOULD_BLOCK;
    }
    if (native_result == OS_NET_EINVAL) return EOS_ERRNO_INVALID;
    if (native_result == OS_NET_EADDRNOTAVAIL) {
        return EOS_ERRNO_ADDRESS_NOT_AVAILABLE;
    }
    if (native_result == OS_NET_EADDRINUSE) return EOS_ERRNO_ADDRESS_IN_USE;
    if (native_result == OS_NET_ENOBUFS) return EOS_ERRNO_NO_BUFFERS;
    if (native_result == OS_NET_ENOPROTOOPT) {
        return EOS_ERRNO_NO_PROTOCOL_OPTION;
    }
    if (native_result == OS_NET_ECLOSED) {
        return operation == EOS_MARTOS_NETWORK_RECEIVE
                   ? 0 : EOS_ERRNO_CONNECTION_RESET;
    }
    return EOS_ERRNO_IO;
}

static eos_port_socket_io_result eos_martos_network_io_result(
    int32_t native_result,
    eos_martos_network_operation operation) {
    eos_port_socket_io_result result;
    if (native_result >= 0) {
        result.count = native_result;
        result.error_number = 0;
    } else if (native_result == OS_NET_ECLOSED &&
               operation == EOS_MARTOS_NETWORK_RECEIVE) {
        result.count = 0;
        result.error_number = 0;
    } else {
        result.count = -1;
        result.error_number = eos_martos_network_error(native_result,
                                                       operation);
    }
    return result;
}

static int32_t eos_martos_address_to_native(
    const eos_port_socket_address *source,
    os_net_sockaddr *destination) {
    (void)memset(destination, 0, sizeof(*destination));
    if (source == NULL) return EOS_ERRNO_FAULT;
    if (source->family != EOS_RUST_AF_INET &&
        source->family != EOS_RUST_AF_INET6) {
        return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
    }
    if (source->family == EOS_RUST_AF_INET6 && source->scope_id != 0) {
        return EOS_ERRNO_NOT_SUPPORTED;
    }
    destination->sin_len = (uint8_t)sizeof(*destination);
    destination->sin_family = (uint8_t)source->family;
    destination->sin_port = source->port;
    destination->sin_flowinfo = source->flowinfo;
    if (source->family == EOS_RUST_AF_INET) {
        (void)memcpy(&destination->sin_address.ipv4, source->address, 4);
    } else {
        (void)memcpy(destination->sin_address.ipv6.bytes, source->address, 16);
    }
    return 0;
}

static int32_t eos_martos_address_from_native(
    const os_net_sockaddr *source,
    eos_port_socket_address *destination) {
    (void)memset(destination, 0, sizeof(*destination));
    if (source == NULL) return EOS_ERRNO_FAULT;
    if (source->sin_len != (uint8_t)sizeof(*source)) return EOS_ERRNO_IO;
    if (source->sin_family != OS_NET_AF_INET &&
        source->sin_family != OS_NET_AF_INET6) {
        return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
    }
    destination->family = (uint16_t)source->sin_family;
    destination->port = source->sin_port;
    destination->flowinfo = source->sin_flowinfo;
    destination->scope_id = 0;
    if (source->sin_family == OS_NET_AF_INET) {
        (void)memcpy(destination->address, &source->sin_address.ipv4, 4);
    } else {
        (void)memcpy(destination->address, source->sin_address.ipv6.bytes, 16);
    }
    return 0;
}

static int32_t eos_martos_message_flags(int32_t compatibility_flags) {
    int32_t native_flags = 0;
    if ((compatibility_flags & EOS_RUST_MSG_OOB) != 0) {
        native_flags |= OS_NET_MSG_OOB;
    }
    if ((compatibility_flags & EOS_RUST_MSG_PEEK) != 0) {
        native_flags |= OS_NET_MSG_PEEK;
    }
    if ((compatibility_flags & EOS_RUST_MSG_DONTROUTE) != 0) {
        native_flags |= OS_NET_MSG_DONTROUTE;
    }
    if ((compatibility_flags & EOS_RUST_MSG_DONTWAIT) != 0) {
        native_flags |= OS_NET_MSG_DONTWAIT;
    }
    return native_flags;
}

static int32_t eos_martos_timeout_option(uint32_t receive) {
    return receive != 0 ? OS_NET_SO_RCVTIMEO : OS_NET_SO_SNDTIMEO;
}

static int32_t eos_martos_integer_option(int32_t compatibility_option) {
    if (compatibility_option == EOS_RUST_SO_SNDBUF) return OS_NET_SO_SNDBUF;
    if (compatibility_option == EOS_RUST_SO_RCVBUF) return OS_NET_SO_RCVBUF;
    return -1;
}

static eos_port_socket_create_result eos_martos_pointer_result(
    os_net_socket native,
    uint32_t accepting) {
    eos_port_socket_create_result result;
    result.socket = EOS_PORT_SOCKET_INVALID;
    if (native == OS_NET_NO_SOCKET) {
        result.error_number = accepting != 0 ? EOS_ERRNO_WOULD_BLOCK
                                             : EOS_ERRNO_NO_BUFFERS;
    } else if (native == OS_NET_INVALID_SOCKET) {
        result.error_number = EOS_ERRNO_IO;
    } else {
        result.socket = (eos_port_socket)(uintptr_t)native;
        result.error_number = 0;
    }
    return result;
}

static int32_t eos_martos_local_address(
    eos_port_socket socket,
    eos_port_socket_address *address) {
    os_net_sockaddr native;
    uint32_t length;
    (void)memset(&native, 0, sizeof(native));
    native.sin_len = (uint8_t)sizeof(native);
    length = os_net_get_local_address((os_net_socket)(uintptr_t)socket,
                                      &native);
    if (length != (uint32_t)sizeof(native)) return EOS_ERRNO_IO;
    return eos_martos_address_from_native(&native, address);
}

static int32_t eos_martos_remote_address(
    eos_port_socket socket,
    eos_port_socket_address *address) {
    os_net_sockaddr native;
    int32_t native_result;
    (void)memset(&native, 0, sizeof(native));
    native.sin_len = (uint8_t)sizeof(native);
    native_result = os_net_get_remote_address(
        (os_net_socket)(uintptr_t)socket, &native);
    if (native_result < 0) {
        return eos_martos_network_error(native_result,
                                        EOS_MARTOS_NETWORK_OTHER);
    }
    return eos_martos_address_from_native(&native, address);
}

static int32_t eos_martos_socketset_poll(
    const eos_port_socket *sockets,
    const uint32_t *requested,
    uint32_t *observed,
    uint32_t socket_count,
    uint32_t timeout_ticks) {
    os_net_socketset set;
    uint32_t index;
    int32_t native_result;
    set = os_net_socketset_create();
    if (set == NULL || set == (os_net_socketset)OS_NET_INVALID_SOCKET) {
        return EOS_ERRNO_NO_BUFFERS;
    }
    for (index = 0; index < socket_count; ++index) {
        os_net_select_event_bits bits = OS_NET_SELECT_EXCEPT |
                                       OS_NET_SELECT_INTR;
        if ((requested[index] & EOS_PORT_SOCKET_EVENT_READ) != 0) {
            bits |= OS_NET_SELECT_READ;
        }
        if ((requested[index] & EOS_PORT_SOCKET_EVENT_WRITE) != 0) {
            bits |= OS_NET_SELECT_WRITE;
        }
        native_result = os_net_fd_set((os_net_socket)(uintptr_t)sockets[index],
                                      set, bits);
        if (native_result < 0) {
            int32_t error = eos_martos_network_error(
                native_result, EOS_MARTOS_NETWORK_OTHER);
            if (os_net_socketset_delete(set) != 0) eos_rust_abort();
            return error;
        }
    }
    native_result = os_net_select(set, timeout_ticks);
    if (native_result < 0) {
        int32_t error = eos_martos_network_error(
            native_result, EOS_MARTOS_NETWORK_OTHER);
        if (os_net_socketset_delete(set) != 0) eos_rust_abort();
        return error;
    }
    for (index = 0; index < socket_count; ++index) {
        os_net_socket native = (os_net_socket)(uintptr_t)sockets[index];
        os_net_select_event_bits bits = os_net_fd_isset(native, set);
        observed[index] = 0;
        if ((bits & OS_NET_SELECT_READ) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_READ;
        }
        if ((bits & OS_NET_SELECT_WRITE) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_WRITE;
        }
        if ((bits & (OS_NET_SELECT_EXCEPT | OS_NET_SELECT_INTR)) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_ERROR;
            if ((requested[index] & EOS_PORT_SOCKET_EVENT_PRIORITY) != 0) {
                observed[index] |= EOS_PORT_SOCKET_EVENT_PRIORITY;
            }
        }
        if ((requested[index] &
             EOS_PORT_SOCKET_EVENT_HANGUP_ELIGIBLE) != 0 &&
            os_net_socket_get_protocol(native) == OS_NET_IPPROTO_TCP &&
            os_net_get_tcp_state(native) == OS_NET_TCP_CLOSED) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_HANGUP;
        }
    }
    if (os_net_socketset_delete(set) != 0) eos_rust_abort();
    return 0;
}

#endif
