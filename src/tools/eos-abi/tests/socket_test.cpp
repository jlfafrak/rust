#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_socket_create(int32_t error_number);
void eos_host_test_fail_next_socket_connect(int32_t error_number);
uint32_t eos_host_test_socket_close_count(void);
void eos_host_test_socket_partial_send(uint32_t bytes, int32_t error_number);
void eos_host_test_socket_partial_receive(uint32_t bytes,
                                          int32_t error_number);
void eos_host_test_fail_socket_timeout_after(uint32_t successful_updates,
                                             int32_t error_number);
uint32_t eos_host_test_socket_timeout_log_count(void);
uint32_t eos_host_test_socket_timeout_log_receive(uint32_t index);
uint32_t eos_host_test_socket_timeout_log_ticks(uint32_t index);
void eos_host_test_fail_alloc_after(uint32_t successful_allocations,
                                    int32_t status);
int32_t eos_socket_test_ipv6_output(uint32_t scope_id,
                                    eos_rust_sockaddr_in6 *destination);
}

static_assert(sizeof(eos_rust_sockaddr) == 16);
static_assert(alignof(eos_rust_sockaddr) == 2);
static_assert(sizeof(eos_rust_sockaddr_in) == 16);
static_assert(offsetof(eos_rust_sockaddr_in, sin_addr) == 4);
static_assert(sizeof(eos_rust_sockaddr_in6) == 28);
static_assert(offsetof(eos_rust_sockaddr_in6, sin6_addr) == 8);
static_assert(offsetof(eos_rust_sockaddr_in6, sin6_scope_id) == 24);
static_assert(sizeof(eos_rust_sockaddr_storage) == 32);
static_assert(alignof(eos_rust_sockaddr_storage) == 4);
static_assert(sizeof(eos_rust_timeval) == 16);
static_assert(alignof(eos_rust_timeval) == 8);

static int expect(bool condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    std::fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

static uint16_t network_u16(uint16_t value) {
    return static_cast<uint16_t>((value << 8U) | (value >> 8U));
}

static void reset_fixture(const char *name) {
    eos_fd_test_reset();
    eos_host_test_reset();
    const char *arguments[] = {name, nullptr};
    eos_rust_runtime_init(1, arguments);
}

static eos_rust_sockaddr_in loopback_address() {
    eos_rust_sockaddr_in address{};
    address.sin_family = EOS_RUST_AF_INET;
    address.sin_addr.s_addr = UINT32_C(0x0100007f);
    return address;
}

static int test_validation_before_native_access() {
    reset_fixture("socket-validation");
    eos_rust_sockaddr_in6 scoped{};
    if (expect(eos_socket_test_ipv6_output(UINT32_C(73), &scoped) == 0 &&
                   scoped.sin6_family == EOS_RUST_AF_INET6 &&
                   scoped.sin6_scope_id == UINT32_C(73),
               "normalized IPv6 output must preserve the scope identifier")) {
        return 1;
    }
    eos_host_test_fail_next_socket_create(49);
    if (expect(eos_rust_socket(99, EOS_RUST_SOCK_STREAM,
                               EOS_RUST_IPPROTO_TCP) == -1 &&
                   *eos_rust_errno_location() == 47,
               "invalid family must fail before native creation")) return 1;
    if (expect(eos_rust_socket(EOS_RUST_AF_INET, EOS_RUST_SOCK_STREAM,
                               EOS_RUST_IPPROTO_TCP) == -1 &&
                   *eos_rust_errno_location() == 49,
               "validation must leave the native-create fault untouched")) return 1;
    if (expect(eos_rust_socket(EOS_RUST_AF_INET, 99, 0) == -1 &&
                   *eos_rust_errno_location() == 41,
               "invalid type must report EPROTOTYPE")) return 1;
    if (expect(eos_rust_socket(EOS_RUST_AF_INET, EOS_RUST_SOCK_STREAM,
                               EOS_RUST_IPPROTO_UDP) == -1 &&
                   *eos_rust_errno_location() == 43,
               "inconsistent protocol must be rejected")) return 1;
    if (expect(eos_rust_send(-1, nullptr, UINT32_MAX, 0) == -1 &&
                   *eos_rust_errno_location() == 22,
               "oversized send must fail before descriptor and buffer access")) return 1;
    return 0;
}

static int test_tcp_options_dup_and_partial_progress() {
    reset_fixture("socket-tcp");
    eos_rust_sockaddr_in loopback = loopback_address();
    eos_rust_sockaddr_in listener_address{};
    eos_rust_socklen_t listener_length = sizeof(listener_address);

    const eos_rust_fd_t listener = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_STREAM, EOS_RUST_IPPROTO_TCP);
    if (expect(listener >= 3, "TCP socket creation must publish a descriptor")) return 1;
    if (expect(eos_rust_bind(listener,
                             reinterpret_cast<const eos_rust_sockaddr *>(&loopback),
                             sizeof(loopback)) == 0,
               "TCP loopback bind must succeed")) return 1;
    if (expect(eos_rust_listen(listener, 4) == 0,
               "TCP listen must succeed")) return 1;
    if (expect(eos_rust_getsockname(
                   listener,
                   reinterpret_cast<eos_rust_sockaddr *>(&listener_address),
                   &listener_length) == 0 &&
                   listener_length == sizeof(listener_address) &&
                   listener_address.sin_family == EOS_RUST_AF_INET &&
                   listener_address.sin_port != network_u16(0),
               "getsockname must return the bound IPv4 address")) return 1;
    eos_rust_sockaddr short_address{};
    eos_rust_socklen_t short_length = 4;
    if (expect(eos_rust_getsockname(listener, &short_address, &short_length) == 0 &&
                   short_length == sizeof(eos_rust_sockaddr_in) &&
                   short_address.sa_family == EOS_RUST_AF_INET,
               "address output must report actual length and bounded initialized bytes")) return 1;

    const eos_rust_fd_t client = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_STREAM, EOS_RUST_IPPROTO_TCP);
    if (expect(client >= 3 &&
                   eos_rust_connect(client,
                                    reinterpret_cast<const eos_rust_sockaddr *>(
                                        &listener_address),
                                    sizeof(listener_address)) == 0,
               "TCP loopback connect must succeed")) return 1;

    eos_rust_sockaddr_in peer{};
    eos_rust_socklen_t peer_length = sizeof(peer);
    const eos_rust_fd_t accepted = eos_rust_accept(
        listener, reinterpret_cast<eos_rust_sockaddr *>(&peer), &peer_length);
    if (expect(accepted >= 3 && peer_length == sizeof(peer),
               "accept must publish a descriptor and peer address")) return 1;
    eos_rust_sockaddr_in queried_peer{};
    eos_rust_socklen_t queried_peer_length = sizeof(queried_peer);
    if (expect(eos_rust_getpeername(
                   client, reinterpret_cast<eos_rust_sockaddr *>(&queried_peer),
                   &queried_peer_length) == 0 &&
                   queried_peer.sin_port == listener_address.sin_port,
               "getpeername must return the connected peer")) return 1;

    static constexpr char payload[] = "network-contract";
    char received[sizeof(payload)]{};
    if (expect(eos_rust_send(client, payload, sizeof(payload), 0) ==
                   static_cast<int32_t>(sizeof(payload)) &&
                   eos_rust_recv(accepted, received, sizeof(received), 0) ==
                   static_cast<int32_t>(sizeof(received)) &&
                   std::memcmp(received, payload, sizeof(payload)) == 0,
               "TCP send/recv must preserve payload bytes")) return 1;
    if (expect(eos_rust_write(accepted, payload, sizeof(payload)) ==
                   static_cast<int32_t>(sizeof(payload)) &&
                   eos_rust_read(client, received, sizeof(received)) ==
                   static_cast<int32_t>(sizeof(received)),
               "descriptor read/write must dispatch sockets")) return 1;

    eos_host_test_socket_partial_send(3, 54);
    if (expect(eos_rust_send(client, payload, sizeof(payload), 0) == 3 &&
                   eos_rust_recv(accepted, received, 3, 0) == 3,
               "partial send progress must outrank a trailing native error")) return 1;
    if (expect(eos_rust_send(client, payload, 5, 0) == 5,
               "partial receive fixture send failed")) return 1;
    eos_host_test_socket_partial_receive(2, 54);
    if (expect(eos_rust_recv(accepted, received, 5, 0) == 2,
               "partial receive progress must outrank a trailing native error")) return 1;
    if (expect(eos_rust_recv(accepted, received, 3, 0) == 3,
               "partial receive fixture must leave remaining bytes readable")) return 1;

    eos_rust_timeval sub_tick{0, 1};
    if (expect(eos_rust_setsockopt(client, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_RCVTIMEO, &sub_tick,
                                   sizeof(sub_tick)) == 0 &&
                   eos_host_test_socket_timeout_log_ticks(
                       eos_host_test_socket_timeout_log_count() - 1) == 1,
               "positive sub-tick receive timeout must round upward")) return 1;
    eos_rust_timeval observed_timeout{};
    eos_rust_socklen_t observed_timeout_length = sizeof(observed_timeout);
    if (expect(eos_rust_getsockopt(client, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_RCVTIMEO, &observed_timeout,
                                   &observed_timeout_length) == 0 &&
                   observed_timeout.tv_sec == 0 &&
                   observed_timeout.tv_usec == 1,
               "getsockopt must return compatibility-owned timeout state")) return 1;
    eos_rust_timeval invalid_timeout{0, 1000000};
    if (expect(eos_rust_setsockopt(client, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_RCVTIMEO, &invalid_timeout,
                                   sizeof(invalid_timeout)) == -1 &&
                   *eos_rust_errno_location() == 22,
               "invalid timeval must be rejected")) return 1;
    eos_rust_timeval overflow_timeout{INT64_MAX, 0};
    if (expect(eos_rust_setsockopt(client, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_RCVTIMEO, &overflow_timeout,
                                   sizeof(overflow_timeout)) == -1 &&
                   *eos_rust_errno_location() == 84,
               "unrepresentable finite timeout must report EOVERFLOW")) return 1;

    uint32_t before_rollback = eos_host_test_socket_timeout_log_count();
    eos_host_test_fail_socket_timeout_after(1, 55);
    if (expect(eos_rust_fcntl(client, EOS_RUST_F_SETFL,
                              EOS_RUST_O_NONBLOCK) == -1 &&
                   *eos_rust_errno_location() == 55 &&
                   (eos_rust_fcntl(client, EOS_RUST_F_GETFL, 0) &
                    EOS_RUST_O_NONBLOCK) == 0 &&
                   eos_host_test_socket_timeout_log_count() ==
                       before_rollback + 3 &&
                   eos_host_test_socket_timeout_log_receive(before_rollback) == 1 &&
                   eos_host_test_socket_timeout_log_receive(before_rollback + 1) == 0 &&
                   eos_host_test_socket_timeout_log_receive(before_rollback + 2) == 1 &&
                   eos_host_test_socket_timeout_log_ticks(before_rollback + 2) == 1,
               "failed second nonblocking update must rollback first and not publish state")) return 1;
    if (expect(eos_rust_fcntl(client, EOS_RUST_F_SETFL,
                              EOS_RUST_O_NONBLOCK) == 0,
               "nonblocking enable must succeed after rollback")) return 1;
    eos_rust_fd_t duplicate = eos_rust_dup(client);
    if (expect(duplicate >= 3 &&
                   (eos_rust_fcntl(duplicate, EOS_RUST_F_GETFL, 0) &
                    EOS_RUST_O_NONBLOCK) != 0,
               "duplicates must share nonblocking file-description state")) return 1;
    char byte = 0;
    if (expect(eos_rust_recv(client, &byte, 1, 0) == -1 &&
                   *eos_rust_errno_location() == 35,
               "empty nonblocking receive must report EWOULDBLOCK")) return 1;
    uint32_t before_deferred_timeout = eos_host_test_socket_timeout_log_count();
    eos_rust_timeval retained_timeout{2, 0};
    if (expect(eos_rust_setsockopt(duplicate, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_RCVTIMEO, &retained_timeout,
                                   sizeof(retained_timeout)) == 0 &&
                   eos_host_test_socket_timeout_log_count() ==
                       before_deferred_timeout,
               "timeout changes in nonblocking mode must retain state without losing no-wait")) return 1;
    if (expect(eos_rust_fcntl(duplicate, EOS_RUST_F_SETFL, 0) == 0 &&
                   (eos_rust_fcntl(client, EOS_RUST_F_GETFL, 0) &
                    EOS_RUST_O_NONBLOCK) == 0 &&
                   eos_host_test_socket_timeout_log_ticks(
                       eos_host_test_socket_timeout_log_count() - 2) == 2000 &&
                   eos_host_test_socket_timeout_log_ticks(
                       eos_host_test_socket_timeout_log_count() - 1) == UINT32_MAX,
               "disabling nonblocking must restore retained receive and infinite send timeouts")) return 1;
    if (expect(eos_rust_fcntl(duplicate, EOS_RUST_F_SETFD,
                              EOS_RUST_FD_CLOEXEC) == 0 &&
                   eos_rust_fcntl(duplicate, EOS_RUST_F_GETFD, 0) ==
                       EOS_RUST_FD_CLOEXEC &&
                   eos_rust_fcntl(client, EOS_RUST_F_GETFD, 0) == 0,
               "CLOEXEC must remain descriptor-local across dup")) return 1;
    int32_t unsupported = 1;
    if (expect(eos_rust_setsockopt(client, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_REUSEADDR, &unsupported,
                                   sizeof(unsupported)) == -1 &&
                   *eos_rust_errno_location() == 42,
               "SO_REUSEADDR must not impersonate MARTOS reuse-listen")) return 1;

    eos_rust_fd_t error_socket = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_STREAM, EOS_RUST_IPPROTO_TCP);
    eos_host_test_fail_next_socket_connect(61);
    if (expect(error_socket >= 3 &&
                   eos_rust_connect(error_socket,
                                    reinterpret_cast<const eos_rust_sockaddr *>(
                                        &listener_address),
                                    sizeof(listener_address)) == -1 &&
                   *eos_rust_errno_location() == 61,
               "connect error must populate compatibility pending error")) return 1;
    int32_t pending = 0;
    eos_rust_socklen_t pending_length = sizeof(pending);
    if (expect(eos_rust_getsockopt(error_socket, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_ERROR, &pending,
                                   &pending_length) == 0 && pending == 61,
               "SO_ERROR must return the pending connect error")) return 1;
    pending = -1;
    pending_length = sizeof(pending);
    if (expect(eos_rust_getsockopt(error_socket, EOS_RUST_SOL_SOCKET,
                                   EOS_RUST_SO_ERROR, &pending,
                                   &pending_length) == 0 && pending == 0,
               "SO_ERROR must clear the pending error")) return 1;

    uint32_t close_before = eos_host_test_socket_close_count();
    if (expect(eos_rust_close(client) == 0 &&
                   eos_host_test_socket_close_count() == close_before &&
                   eos_rust_send(duplicate, payload, 1, 0) == 1 &&
                   eos_rust_recv(accepted, received, 1, 0) == 1,
               "closing one duplicate must retain the shared native socket")) return 1;
    if (expect(eos_rust_shutdown(duplicate, EOS_RUST_SHUT_WR) == 0 &&
                   eos_rust_recv(accepted, received, sizeof(received), 0) == 0,
               "TCP orderly write shutdown must become receive EOF")) return 1;

    if (expect(eos_rust_close(duplicate) == 0 &&
                   eos_host_test_socket_close_count() == close_before + 1 &&
                   eos_rust_close(error_socket) == 0 &&
                   eos_rust_close(accepted) == 0 &&
                   eos_rust_close(listener) == 0,
               "final duplicate and TCP descriptors must close exactly once")) return 1;
    return 0;
}

static int test_udp_peek_connect_and_zero_datagram() {
    reset_fixture("socket-udp");
    eos_rust_sockaddr_in loopback = loopback_address();
    eos_rust_sockaddr_in receiver_address{};
    eos_rust_socklen_t receiver_length = sizeof(receiver_address);
    eos_rust_fd_t receiver = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, EOS_RUST_IPPROTO_UDP);
    eos_rust_fd_t sender = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, 0);
    if (expect(receiver >= 3 && sender >= 3 &&
                   eos_rust_bind(receiver,
                                 reinterpret_cast<const eos_rust_sockaddr *>(
                                     &loopback), sizeof(loopback)) == 0 &&
                   eos_rust_getsockname(receiver,
                                        reinterpret_cast<eos_rust_sockaddr *>(
                                            &receiver_address),
                                        &receiver_length) == 0,
               "UDP bind/address setup must succeed")) return 1;
    static constexpr char datagram[] = "udp-contract";
    if (expect(eos_rust_sendto(sender, datagram, sizeof(datagram), 0,
                               reinterpret_cast<const eos_rust_sockaddr *>(
                                   &receiver_address),
                               sizeof(receiver_address)) ==
                   static_cast<int32_t>(sizeof(datagram)),
               "UDP sendto failed")) return 1;
    char received[sizeof(datagram)]{};
    eos_rust_sockaddr_in source{};
    eos_rust_socklen_t source_length = sizeof(source);
    if (expect(eos_rust_recvfrom(receiver, received, sizeof(received),
                                 EOS_RUST_MSG_PEEK,
                                 reinterpret_cast<eos_rust_sockaddr *>(&source),
                                 &source_length) ==
                       static_cast<int32_t>(sizeof(received)) &&
                   eos_rust_recvfrom(receiver, received, sizeof(received), 0,
                                     reinterpret_cast<eos_rust_sockaddr *>(&source),
                                     &source_length) ==
                       static_cast<int32_t>(sizeof(received)) &&
                   source.sin_family == EOS_RUST_AF_INET,
               "UDP peek must preserve datagram and source address")) return 1;
    if (expect(eos_rust_connect(sender,
                                reinterpret_cast<const eos_rust_sockaddr *>(
                                    &receiver_address),
                                sizeof(receiver_address)) == 0 &&
                   eos_rust_send(sender, datagram, 3, 0) == 3 &&
                   eos_rust_recv(receiver, received, 3, 0) == 3,
               "connected UDP send/receive failed")) return 1;
    if (expect(eos_rust_sendto(sender, nullptr, 0, 0,
                               reinterpret_cast<const eos_rust_sockaddr *>(
                                   &receiver_address),
                               sizeof(receiver_address)) == 0 &&
                   eos_rust_recvfrom(receiver, nullptr, 0, 0, nullptr,
                                     nullptr) == 0,
               "zero-length UDP datagram must be transmitted and consumed")) return 1;
    if (expect(eos_rust_fcntl(receiver, EOS_RUST_F_SETFL,
                              EOS_RUST_O_NONBLOCK) == 0 &&
                   eos_rust_recv(receiver, received, 1,
                                 EOS_RUST_MSG_DONTWAIT) == -1 &&
                   *eos_rust_errno_location() == 35,
               "empty nonblocking UDP receive must report EWOULDBLOCK")) return 1;
    if (expect(eos_rust_sendto(sender, datagram, 1, INT32_MIN,
                               reinterpret_cast<const eos_rust_sockaddr *>(
                                   &receiver_address),
                               sizeof(receiver_address)) == -1 &&
                   *eos_rust_errno_location() == 22,
               "unknown message flags must be rejected")) return 1;
    if (expect(eos_rust_close(sender) == 0 && eos_rust_close(receiver) == 0,
               "UDP descriptors must close")) return 1;
    return 0;
}

static int test_exhaustion_and_accept_publication_rollback() {
    reset_fixture("socket-exhaustion");
    for (uint32_t index = 3; index < eos_fd_test_capacity(); ++index) {
        if (expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, 1000 + index, 0) ==
                       static_cast<int32_t>(index),
                   "descriptor exhaustion fixture failed")) return 1;
    }
    uint32_t close_before = eos_host_test_socket_close_count();
    if (expect(eos_rust_socket(EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, 0) == -1 &&
                   *eos_rust_errno_location() == 24 &&
                   eos_host_test_socket_close_count() == close_before + 1,
               "descriptor publication failure must close native socket once")) return 1;

    reset_fixture("socket-accept-rollback");
    eos_rust_sockaddr_in loopback = loopback_address();
    eos_rust_sockaddr_in listener_address{};
    eos_rust_socklen_t length = sizeof(listener_address);
    eos_rust_fd_t listener = eos_rust_socket(EOS_RUST_AF_INET,
                                             EOS_RUST_SOCK_STREAM, 0);
    if (expect(listener >= 3 &&
                   eos_rust_bind(listener,
                                 reinterpret_cast<const eos_rust_sockaddr *>(
                                     &loopback), sizeof(loopback)) == 0 &&
                   eos_rust_listen(listener, 1) == 0 &&
                   eos_rust_getsockname(listener,
                                        reinterpret_cast<eos_rust_sockaddr *>(
                                            &listener_address), &length) == 0,
               "accept rollback listener setup failed")) return 1;
    eos_rust_fd_t client = eos_rust_socket(EOS_RUST_AF_INET,
                                           EOS_RUST_SOCK_STREAM, 0);
    if (expect(client >= 3 &&
                   eos_rust_connect(client,
                                    reinterpret_cast<const eos_rust_sockaddr *>(
                                        &listener_address),
                                    sizeof(listener_address)) == 0,
               "accept rollback client setup failed")) return 1;
    close_before = eos_host_test_socket_close_count();
    eos_host_test_fail_alloc_after(1, 15);
    if (expect(eos_rust_accept(listener, nullptr, nullptr) == -1 &&
                   *eos_rust_errno_location() == 12 &&
                   eos_host_test_socket_close_count() == close_before + 1,
               "accepted socket publication failure must close only accepted native socket")) return 1;
    if (expect(eos_rust_close(client) == 0 && eos_rust_close(listener) == 0,
               "accept rollback fixture cleanup failed")) return 1;
    return 0;
}

int main() {
    if (test_validation_before_native_access() != 0) return EXIT_FAILURE;
    if (test_tcp_options_dup_and_partial_progress() != 0) return EXIT_FAILURE;
    if (test_udp_peek_connect_and_zero_datagram() != 0) return EXIT_FAILURE;
    if (test_exhaustion_and_accept_publication_rollback() != 0) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
