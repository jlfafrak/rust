#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_pause_socket_poll(int enabled);
uint32_t eos_host_test_socket_poll_entered(void);
void eos_host_test_resume_socket_poll(void);
void eos_host_test_force_socket_poll_events(uint32_t compatibility_events);
}

static_assert(sizeof(eos_rust_pollfd) == 8);
static_assert(alignof(eos_rust_pollfd) == 4);
static_assert(offsetof(eos_rust_pollfd, events) == 4);
static_assert(offsetof(eos_rust_pollfd, revents) == 6);

static int expect(bool condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    std::fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

static uint16_t network_u16(uint16_t value) {
    return static_cast<uint16_t>((value << 8U) | (value >> 8U));
}

static void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    const char *arguments[] = {"poll-test", nullptr};
    eos_rust_runtime_init(1, arguments);
}

static eos_rust_sockaddr_in bind_udp(eos_rust_fd_t descriptor) {
    eos_rust_sockaddr_in address{};
    address.sin_family = EOS_RUST_AF_INET;
    address.sin_addr.s_addr = UINT32_C(0x0100007f);
    if (eos_rust_bind(descriptor,
                      reinterpret_cast<const eos_rust_sockaddr *>(&address),
                      sizeof(address)) != 0) {
        std::fprintf(stderr, "UDP poll bind failed errno=%d\n",
                     *eos_rust_errno_location());
        std::exit(EXIT_FAILURE);
    }
    eos_rust_socklen_t length = sizeof(address);
    if (eos_rust_getsockname(descriptor,
                            reinterpret_cast<eos_rust_sockaddr *>(&address),
                            &length) != 0 ||
        address.sin_port == network_u16(0)) {
        std::fprintf(stderr, "UDP poll getsockname failed\n");
        std::exit(EXIT_FAILURE);
    }
    return address;
}

int main() {
    reset_fixture();
    eos_rust_pollfd invalid_entries[4] = {
        {-1, EOS_RUST_POLLIN, static_cast<int16_t>(-1)},
        {63, EOS_RUST_POLLOUT, static_cast<int16_t>(-1)},
        {-7, EOS_RUST_POLLERR, static_cast<int16_t>(-1)},
        {0, EOS_RUST_POLLIN, static_cast<int16_t>(-1)},
    };
    *eos_rust_errno_location() = 77;
    const int32_t invalid_ready = eos_rust_poll(invalid_entries, 4, 0);
    if (expect(invalid_ready == 2 && invalid_entries[0].revents == 0 &&
                   invalid_entries[1].revents == EOS_RUST_POLLNVAL &&
                   invalid_entries[2].revents == 0 &&
                   invalid_entries[3].revents == EOS_RUST_POLLNVAL,
               "poll must ignore negative descriptors and mark stale/non-socket descriptors")) {
        return EXIT_FAILURE;
    }
    if (expect(*eos_rust_errno_location() == 77,
               "successful POLLNVAL reporting must preserve errno")) {
        return EXIT_FAILURE;
    }
    eos_rust_pollfd immediate_invalid{63, EOS_RUST_POLLIN, 0};
    *eos_rust_errno_location() = 78;
    const auto invalid_start = std::chrono::steady_clock::now();
    const int32_t immediate_ready = eos_rust_poll(&immediate_invalid, 1, 1000);
    const auto invalid_elapsed = std::chrono::steady_clock::now() - invalid_start;
    if (expect(immediate_ready == 1 &&
                   immediate_invalid.revents == EOS_RUST_POLLNVAL &&
                   *eos_rust_errno_location() == 78 &&
                   invalid_elapsed < std::chrono::milliseconds(500),
               "POLLNVAL must return immediately without honoring the timeout")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_poll(nullptr, 0, 0) == 0,
               "zero-entry poll must accept a null array")) return EXIT_FAILURE;
    if (expect(eos_rust_poll(nullptr, 1, 0) == -1 &&
                   *eos_rust_errno_location() == 14,
               "nonzero poll count must reject a null array")) return EXIT_FAILURE;
    std::vector<eos_rust_pollfd> too_many(65);
    if (expect(eos_rust_poll(too_many.data(), 65, 0) == -1 &&
                   *eos_rust_errno_location() == 22,
               "poll must reject more than descriptor-table capacity before entry access")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_poll(nullptr, 0, -2) == -1 &&
                   *eos_rust_errno_location() == 22,
               "poll timeout below -1 must be invalid")) return EXIT_FAILURE;

    reset_fixture();
    eos_rust_fd_t receiver_one = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, EOS_RUST_IPPROTO_UDP);
    eos_rust_fd_t sender = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, EOS_RUST_IPPROTO_UDP);
    if (expect(receiver_one >= 3 && sender >= 3,
               "poll UDP sockets must be created")) return EXIT_FAILURE;
    eos_host_test_force_socket_poll_events(
        EOS_RUST_POLLOUT | EOS_RUST_POLLPRI |
        EOS_RUST_POLLERR | EOS_RUST_POLLHUP);
    eos_rust_pollfd masked_entry{receiver_one, EOS_RUST_POLLIN, 0};
    if (expect(eos_rust_poll(&masked_entry, 1, 0) == 1 &&
                   masked_entry.revents ==
                       (EOS_RUST_POLLERR | EOS_RUST_POLLHUP),
               "poll must mask normal readiness by each entry's request while preserving error and hangup")) {
        return EXIT_FAILURE;
    }
    eos_host_test_force_socket_poll_events(EOS_RUST_POLLIN |
                                           EOS_RUST_POLLOUT);
    eos_rust_pollfd split_entries[2] = {
        {receiver_one, EOS_RUST_POLLIN, 0},
        {receiver_one, EOS_RUST_POLLOUT, 0},
    };
    if (expect(eos_rust_poll(split_entries, 2, 0) == 2 &&
                   split_entries[0].revents == EOS_RUST_POLLIN &&
                   split_entries[1].revents == EOS_RUST_POLLOUT,
               "duplicate poll entries must retain separate masks and ready counts")) {
        return EXIT_FAILURE;
    }
    eos_host_test_force_socket_poll_events(0);
    eos_rust_sockaddr_in address_one = bind_udp(receiver_one);
    static constexpr char payload[] = "poll";
    if (expect(eos_rust_sendto(
                   sender, payload, sizeof(payload), 0,
                   reinterpret_cast<const eos_rust_sockaddr *>(&address_one),
                   sizeof(address_one)) == static_cast<int32_t>(sizeof(payload)),
               "poll readiness datagram send failed")) return EXIT_FAILURE;
    eos_rust_fd_t receiver_two = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, EOS_RUST_IPPROTO_UDP);
    if (expect(receiver_two >= 3, "second poll UDP socket must be created")) {
        return EXIT_FAILURE;
    }
    eos_rust_pollfd entries[3] = {
        {receiver_one, EOS_RUST_POLLIN, 0},
        {receiver_one, EOS_RUST_POLLIN, 0},
        {receiver_two, EOS_RUST_POLLIN, 0},
    };
    int32_t duplicate_ready = eos_rust_poll(entries, 3, 1000);
    if (expect(duplicate_ready == 2 &&
                   entries[0].revents == EOS_RUST_POLLIN &&
                   entries[1].revents == EOS_RUST_POLLIN &&
                   entries[2].revents == 0,
               "poll must count duplicate ready entries independently")) return EXIT_FAILURE;
    eos_rust_pollfd writable_entry{sender, EOS_RUST_POLLOUT, 0};
    if (expect(eos_rust_poll(&writable_entry, 1, 0) == 1 &&
                   writable_entry.revents == EOS_RUST_POLLOUT,
               "zero-time poll must report writable UDP socket")) return EXIT_FAILURE;
    char received[sizeof(payload)]{};
    if (expect(eos_rust_recv(receiver_one, &received, sizeof(payload), 0) ==
                       static_cast<int32_t>(sizeof(payload)) &&
                   received[0] == payload[0],
               "poll readiness datagram receive failed")) return EXIT_FAILURE;
    eos_rust_pollfd timeout_entry{receiver_two, EOS_RUST_POLLIN, -1};
    if (expect(eos_rust_poll(&timeout_entry, 1, 5) == 0 &&
                   timeout_entry.revents == 0,
               "finite poll timeout must return no stale readiness")) return EXIT_FAILURE;
    if (expect(eos_rust_sendto(
                   sender, payload, sizeof(payload), 0,
                   reinterpret_cast<const eos_rust_sockaddr *>(&address_one),
                   sizeof(address_one)) == static_cast<int32_t>(sizeof(payload)),
               "infinite poll readiness datagram send failed")) return EXIT_FAILURE;
    eos_rust_pollfd infinite_entry{receiver_one, EOS_RUST_POLLIN, 0};
    if (expect(eos_rust_poll(&infinite_entry, 1, -1) == 1 &&
                   infinite_entry.revents == EOS_RUST_POLLIN &&
                   eos_rust_recv(receiver_one, received, sizeof(payload), 0) ==
                       static_cast<int32_t>(sizeof(payload)),
               "infinite poll must return once a socket is ready")) return EXIT_FAILURE;

    eos_host_test_pause_socket_poll(1);
    eos_rust_pollfd raced{receiver_two, EOS_RUST_POLLIN, 0};
    std::atomic<int32_t> raced_result{-2};
    std::thread polling([&] {
        raced_result.store(eos_rust_poll(&raced, 1, 0),
                           std::memory_order_release);
    });
    while (eos_host_test_socket_poll_entered() == 0) std::this_thread::yield();
    const eos_rust_fd_t recycled_number = receiver_two;
    if (expect(eos_rust_close(receiver_two) == 0,
               "poll close/reuse fixture close failed")) return EXIT_FAILURE;
    eos_rust_fd_t replacement = eos_rust_socket(
        EOS_RUST_AF_INET, EOS_RUST_SOCK_DGRAM, EOS_RUST_IPPROTO_UDP);
    if (expect(replacement == recycled_number,
               "poll close/reuse fixture must recycle the descriptor number")) {
        return EXIT_FAILURE;
    }
    eos_host_test_resume_socket_poll();
    polling.join();
    if (expect(raced_result.load(std::memory_order_acquire) == 0 &&
                   raced.revents == 0,
               "poll result extraction must remain attached to leased old socket")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_close(replacement) == 0 &&
                   eos_rust_close(receiver_one) == 0 &&
                   eos_rust_close(sender) == 0,
               "poll socket cleanup failed")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
