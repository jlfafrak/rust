#include "eos_rust_abi.h"
#include "eos_error.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t uint8;
typedef uint32_t uint32;
typedef void *os_net_socket;
typedef void *os_net_socketset;
typedef uint32_t os_net_select_event_bits;
typedef int32_t os_net_tcp_state;

#define OS_NET_NO_SOCKET NULL
#define OS_NET_INVALID_SOCKET ((os_net_socket)(uintptr_t)UINT32_MAX)
#define OS_NET_SOCKET_ERROR (-1)
#define OS_NET_EWOULDBLOCK (-11)
#define OS_NET_EINVAL (-22)
#define OS_NET_EADDRNOTAVAIL (-125)
#define OS_NET_EADDRINUSE (-112)
#define OS_NET_ENOBUFS (-105)
#define OS_NET_ENOPROTOOPT (-109)
#define OS_NET_ECLOSED (-128)
#define OS_NET_AF_INET 2
#define OS_NET_AF_INET6 10
#define OS_NET_IPPROTO_TCP 6
#define OS_NET_MSG_OOB 2
#define OS_NET_MSG_PEEK 4
#define OS_NET_MSG_DONTROUTE 8
#define OS_NET_MSG_DONTWAIT 16
#define OS_NET_SO_RCVTIMEO 0
#define OS_NET_SO_SNDTIMEO 1
#define OS_NET_SO_SNDBUF 4
#define OS_NET_SO_RCVBUF 5
#define OS_NET_SOL_SOCKET 0
#define OS_NET_SELECT_READ UINT32_C(0x01)
#define OS_NET_SELECT_WRITE UINT32_C(0x02)
#define OS_NET_SELECT_EXCEPT UINT32_C(0x04)
#define OS_NET_SELECT_INTR UINT32_C(0x08)
#define OS_NET_TCP_CLOSED 0
#define OS_NET_TCP_ESTABLISHED 5

typedef struct os_net_ipv6_address { uint8 bytes[16]; } os_net_ipv6_address;
typedef union os_net_ip_address {
    uint32 ipv4;
    os_net_ipv6_address ipv6;
} os_net_ip_address;
typedef struct os_net_sockaddr {
    uint8 sin_len;
    uint8 sin_family;
    uint16_t sin_port;
    uint32 sin_flowinfo;
    os_net_ip_address sin_address;
} os_net_sockaddr;

typedef uintptr_t eos_port_socket;
#define EOS_PORT_SOCKET_INVALID ((eos_port_socket)UINTPTR_MAX)
#define EOS_PORT_SOCKET_EVENT_READ UINT32_C(0x01)
#define EOS_PORT_SOCKET_EVENT_WRITE UINT32_C(0x02)
#define EOS_PORT_SOCKET_EVENT_ERROR UINT32_C(0x04)
#define EOS_PORT_SOCKET_EVENT_HANGUP UINT32_C(0x08)
#define EOS_PORT_SOCKET_EVENT_PRIORITY UINT32_C(0x10)
#define EOS_PORT_SOCKET_EVENT_HANGUP_ELIGIBLE UINT32_C(0x20)
typedef struct eos_port_socket_address {
    uint16_t family;
    uint16_t port;
    uint32_t flowinfo;
    uint8_t address[16];
    uint32_t scope_id;
} eos_port_socket_address;
typedef struct eos_port_socket_create_result {
    eos_port_socket socket;
    int32_t error_number;
} eos_port_socket_create_result;
typedef struct eos_port_socket_io_result {
    int32_t count;
    int32_t error_number;
} eos_port_socket_io_result;

static uint32_t fake_create_count;
static uint32_t fake_set_count;
static uint32_t fake_select_count;
static uint32_t fake_query_count;
static uint32_t fake_delete_count;
static os_net_select_event_bits fake_set_bits[4];
static uint32_t fake_local_length = sizeof(os_net_sockaddr);
static int32_t fake_remote_result;
static uint32_t fake_create_mode;
static uint32_t fake_set_fail_index = UINT32_MAX;
static int32_t fake_set_result;
static int32_t fake_select_result = 2;
static int32_t fake_delete_result;
static jmp_buf fake_abort_jump;
static uint32_t fake_abort_expected;
static uint32_t fake_setopt_count;
static os_net_socket fake_setopt_socket;
static int32_t fake_setopt_level;
static int32_t fake_setopt_name;
static uint32_t fake_setopt_value;
static uint32_t fake_setopt_length;
static int32_t fake_setopt_result;

static void fake_address(os_net_sockaddr *address, uint8 family) {
    (void)memset(address, 0, sizeof(*address));
    address->sin_len = (uint8)sizeof(*address);
    address->sin_family = family;
    address->sin_port = UINT16_C(0x3412);
    address->sin_flowinfo = UINT32_C(0x10203040);
    address->sin_address.ipv4 = UINT32_C(0x0100007f);
}

static os_net_socketset os_net_socketset_create(void) {
    ++fake_create_count;
    if (fake_create_mode == 1) return NULL;
    if (fake_create_mode == 2) {
        return (os_net_socketset)OS_NET_INVALID_SOCKET;
    }
    return (os_net_socketset)(uintptr_t)UINT32_C(0x1234);
}
static int32_t os_net_socketset_delete(os_net_socketset set) {
    (void)set;
    ++fake_delete_count;
    return fake_delete_result;
}
static int32_t os_net_fd_set(os_net_socket socket, os_net_socketset set,
                             os_net_select_event_bits bits) {
    (void)socket;
    (void)set;
    fake_set_bits[fake_set_count++] = bits;
    if (fake_set_count - 1 == fake_set_fail_index) return fake_set_result;
    return 0;
}
static int32_t os_net_select(os_net_socketset set, uint32 timeout) {
    (void)set;
    (void)timeout;
    ++fake_select_count;
    return fake_select_result;
}
static os_net_select_event_bits os_net_fd_isset(os_net_socket socket,
                                                os_net_socketset set) {
    uintptr_t value = (uintptr_t)socket;
    (void)set;
    ++fake_query_count;
    if (value == 1) return OS_NET_SELECT_READ;
    if (value == 2) return OS_NET_SELECT_WRITE | OS_NET_SELECT_EXCEPT;
    return 0;
}
static uint32 os_net_socket_get_protocol(os_net_socket socket) {
    (void)socket;
    return OS_NET_IPPROTO_TCP;
}
static os_net_tcp_state os_net_get_tcp_state(os_net_socket socket) {
    return (uintptr_t)socket == 3 ? OS_NET_TCP_CLOSED
                                  : OS_NET_TCP_ESTABLISHED;
}
static uint32 os_net_get_local_address(os_net_socket socket,
                                       os_net_sockaddr *address) {
    (void)socket;
    fake_address(address, OS_NET_AF_INET);
    return fake_local_length;
}
static int32_t os_net_get_remote_address(os_net_socket socket,
                                         os_net_sockaddr *address) {
    (void)socket;
    fake_address(address, OS_NET_AF_INET);
    return fake_remote_result;
}
int32_t os_net_setsockopt(os_net_socket socket, int32_t level,
                          int32_t option_name, const void *option_value,
                          uint32 option_length) {
    ++fake_setopt_count;
    fake_setopt_socket = socket;
    fake_setopt_level = level;
    fake_setopt_name = option_name;
    fake_setopt_length = option_length;
    fake_setopt_value = 0;
    if (option_value != NULL && option_length == sizeof(fake_setopt_value)) {
        (void)memcpy(&fake_setopt_value, option_value,
                     sizeof(fake_setopt_value));
    }
    return fake_setopt_result;
}

void eos_rust_abort(void) {
    if (fake_abort_expected != 0) longjmp(fake_abort_jump, 1);
    abort();
}

#include "eos_port_martos_network_contract.h"

static int expect(int condition) { return condition ? 0 : 1; }

static void reset_socketset_fixture(void) {
    fake_create_count = 0;
    fake_set_count = 0;
    fake_select_count = 0;
    fake_query_count = 0;
    fake_delete_count = 0;
    (void)memset(fake_set_bits, 0, sizeof(fake_set_bits));
    fake_create_mode = 0;
    fake_set_fail_index = UINT32_MAX;
    fake_set_result = 0;
    fake_select_result = 2;
    fake_delete_result = 0;
    fake_abort_expected = 0;
}

int main(void) {
    eos_port_socket_address normalized;
    eos_port_socket_address roundtrip;
    eos_port_socket_create_result pointer_result;
    eos_port_socket_io_result io_result;
    os_net_sockaddr native;
    eos_port_socket sockets[3] = {1, 2, 3};
    uint32_t requested[3] = {EOS_PORT_SOCKET_EVENT_READ,
                             EOS_PORT_SOCKET_EVENT_WRITE |
                                 EOS_PORT_SOCKET_EVENT_PRIORITY,
                             0};
    uint32_t observed[3] = {0, 0, 0};
    (void)memset(&normalized, 0, sizeof(normalized));
    normalized.family = EOS_RUST_AF_INET;
    normalized.port = UINT16_C(0x3412);
    normalized.flowinfo = UINT32_C(0x10203040);
    normalized.address[0] = 127;
    normalized.address[3] = 1;
    if (expect(eos_martos_address_to_native(&normalized, &native) == 0 &&
               native.sin_len == sizeof(native) &&
               native.sin_family == OS_NET_AF_INET &&
               native.sin_port == UINT16_C(0x3412) &&
               native.sin_address.ipv4 == UINT32_C(0x0100007f))) return 1;
    if (expect(eos_martos_address_from_native(&native, &roundtrip) == 0 &&
               memcmp(&normalized, &roundtrip, sizeof(normalized)) == 0)) return 2;
    (void)memset(&normalized, 0, sizeof(normalized));
    normalized.family = EOS_RUST_AF_INET6;
    normalized.port = UINT16_C(0x7856);
    normalized.flowinfo = UINT32_C(0x50607080);
    for (uint32_t index = 0; index < 16; ++index) {
        normalized.address[index] = (uint8_t)(index + 1);
    }
    if (expect(eos_martos_address_to_native(&normalized, &native) == 0 &&
               native.sin_family == OS_NET_AF_INET6 &&
               native.sin_port == normalized.port &&
               native.sin_flowinfo == normalized.flowinfo &&
               memcmp(native.sin_address.ipv6.bytes,
                      normalized.address, 16) == 0 &&
               eos_martos_address_from_native(&native, &roundtrip) == 0 &&
               memcmp(&normalized, &roundtrip, sizeof(normalized)) == 0)) return 3;
    normalized.scope_id = 4;
    if (expect(eos_martos_address_to_native(&normalized, &native) ==
               EOS_ERRNO_NOT_SUPPORTED)) return 4;
    if (expect(eos_martos_network_error(OS_NET_EWOULDBLOCK,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_WOULD_BLOCK &&
               eos_martos_network_error(OS_NET_EWOULDBLOCK,
                                        EOS_MARTOS_NETWORK_CONNECT) ==
                   EOS_ERRNO_IN_PROGRESS &&
               eos_martos_network_error(OS_NET_EINVAL,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_INVALID &&
               eos_martos_network_error(OS_NET_EADDRNOTAVAIL,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_ADDRESS_NOT_AVAILABLE &&
               eos_martos_network_error(OS_NET_EADDRINUSE,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_ADDRESS_IN_USE &&
               eos_martos_network_error(OS_NET_ENOBUFS,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_NO_BUFFERS &&
               eos_martos_network_error(OS_NET_ENOPROTOOPT,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_NO_PROTOCOL_OPTION &&
               eos_martos_network_error(OS_NET_ECLOSED,
                                        EOS_MARTOS_NETWORK_SEND) ==
                   EOS_ERRNO_CONNECTION_RESET &&
               eos_martos_network_error(OS_NET_SOCKET_ERROR,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_IO &&
               eos_martos_network_error(-777,
                                        EOS_MARTOS_NETWORK_OTHER) ==
                   EOS_ERRNO_IO)) return 5;
    io_result = eos_martos_network_io_result(OS_NET_ECLOSED,
                                             EOS_MARTOS_NETWORK_RECEIVE);
    if (expect(io_result.count == 0 && io_result.error_number == 0)) return 6;
    pointer_result = eos_martos_pointer_result(OS_NET_NO_SOCKET, 1);
    if (expect(pointer_result.socket == EOS_PORT_SOCKET_INVALID &&
               pointer_result.error_number == EOS_ERRNO_WOULD_BLOCK)) return 7;
    pointer_result = eos_martos_pointer_result(OS_NET_INVALID_SOCKET, 0);
    if (expect(pointer_result.socket == EOS_PORT_SOCKET_INVALID &&
               pointer_result.error_number == EOS_ERRNO_IO)) return 8;
    pointer_result = eos_martos_pointer_result((void *)(uintptr_t)9, 0);
    if (expect(pointer_result.socket == 9 && pointer_result.error_number == 0)) return 9;
    if (expect(eos_martos_message_flags(EOS_RUST_MSG_PEEK |
                                        EOS_RUST_MSG_DONTWAIT) ==
                   (OS_NET_MSG_PEEK | OS_NET_MSG_DONTWAIT) &&
               eos_martos_timeout_option(1) == OS_NET_SO_RCVTIMEO &&
               eos_martos_timeout_option(0) == OS_NET_SO_SNDTIMEO &&
               eos_martos_integer_option(EOS_RUST_SO_SNDBUF) ==
                   OS_NET_SO_SNDBUF &&
               eos_martos_integer_option(EOS_RUST_SO_REUSEADDR) == -1)) return 10;
    fake_setopt_result = 0;
    if (expect(eos_martos_socket_set_timeout(9, 1, UINT32_C(37)) == 0 &&
               fake_setopt_count == 1 && fake_setopt_socket == (void *)9 &&
               fake_setopt_level == OS_NET_SOL_SOCKET &&
               fake_setopt_name == OS_NET_SO_RCVTIMEO &&
               fake_setopt_value == UINT32_C(37) &&
               fake_setopt_length == sizeof(uint32))) return 23;
    fake_setopt_result = OS_NET_ENOPROTOOPT;
    if (expect(eos_martos_socket_set_timeout(10, 0, UINT32_C(41)) ==
                   EOS_ERRNO_NO_PROTOCOL_OPTION &&
               fake_setopt_count == 2 && fake_setopt_socket == (void *)10 &&
               fake_setopt_level == OS_NET_SOL_SOCKET &&
               fake_setopt_name == OS_NET_SO_SNDTIMEO &&
               fake_setopt_value == UINT32_C(41) &&
               fake_setopt_length == sizeof(uint32))) return 24;
    if (expect(eos_martos_local_address(1, &roundtrip) == 0 &&
               roundtrip.family == EOS_RUST_AF_INET &&
               roundtrip.port == UINT16_C(0x3412))) return 11;
    fake_local_length = 0;
    if (expect(eos_martos_local_address(1, &roundtrip) == EOS_ERRNO_IO)) return 12;
    fake_remote_result = OS_NET_EADDRNOTAVAIL;
    if (expect(eos_martos_remote_address(1, &roundtrip) ==
               EOS_ERRNO_ADDRESS_NOT_AVAILABLE)) return 13;
    fake_remote_result = 0;
    reset_socketset_fixture();
    if (expect(eos_martos_socketset_poll(sockets, requested, observed, 3, 7) == 0 &&
               fake_create_count == 1 && fake_set_count == 3 &&
               fake_select_count == 1 && fake_query_count == 3 &&
               fake_delete_count == 1 &&
               (fake_set_bits[0] & OS_NET_SELECT_READ) != 0 &&
               (fake_set_bits[0] & OS_NET_SELECT_EXCEPT) != 0 &&
               observed[0] == EOS_PORT_SOCKET_EVENT_READ &&
               observed[1] == (EOS_PORT_SOCKET_EVENT_WRITE |
                               EOS_PORT_SOCKET_EVENT_ERROR |
                               EOS_PORT_SOCKET_EVENT_PRIORITY) &&
               observed[2] == 0)) return 14;
    requested[2] = EOS_PORT_SOCKET_EVENT_HANGUP_ELIGIBLE;
    observed[2] = 0;
    if (expect(eos_martos_socketset_poll(&sockets[2], &requested[2],
                                        &observed[2], 1, 0) == 0 &&
               observed[2] == EOS_PORT_SOCKET_EVENT_HANGUP)) return 15;
    reset_socketset_fixture();
    if (expect(eos_martos_socketset_poll(NULL, NULL, NULL, 0, 0) == 0 &&
               fake_create_count == 1 && fake_set_count == 0 &&
               fake_select_count == 1 && fake_query_count == 0 &&
               fake_delete_count == 1)) return 16;
    reset_socketset_fixture();
    fake_create_mode = 1;
    if (expect(eos_martos_socketset_poll(sockets, requested, observed, 1, 0) ==
                   EOS_ERRNO_NO_BUFFERS && fake_delete_count == 0)) return 17;
    reset_socketset_fixture();
    fake_create_mode = 2;
    if (expect(eos_martos_socketset_poll(sockets, requested, observed, 1, 0) ==
                   EOS_ERRNO_IO && fake_delete_count == 0)) return 18;
    reset_socketset_fixture();
    fake_set_fail_index = 1;
    fake_set_result = OS_NET_EADDRINUSE;
    if (expect(eos_martos_socketset_poll(sockets, requested, observed, 3, 0) ==
                   EOS_ERRNO_ADDRESS_IN_USE && fake_set_count == 2 &&
                   fake_select_count == 0 && fake_query_count == 0 &&
                   fake_delete_count == 1)) return 19;
    reset_socketset_fixture();
    fake_select_result = OS_NET_EWOULDBLOCK;
    if (expect(eos_martos_socketset_poll(sockets, requested, observed, 1, 0) ==
                   EOS_ERRNO_WOULD_BLOCK && fake_set_count == 1 &&
                   fake_select_count == 1 && fake_query_count == 0 &&
                   fake_delete_count == 1)) return 20;
    reset_socketset_fixture();
    fake_delete_result = OS_NET_SOCKET_ERROR;
    fake_abort_expected = 1;
    if (setjmp(fake_abort_jump) == 0) {
        (void)eos_martos_socketset_poll(sockets, requested, observed, 1, 0);
        fake_abort_expected = 0;
        return 21;
    }
    fake_abort_expected = 0;
    if (expect(fake_create_count == 1 && fake_set_count == 1 &&
               fake_select_count == 1 && fake_query_count == 1 &&
               fake_delete_count == 1)) return 22;
    return 0;
}
