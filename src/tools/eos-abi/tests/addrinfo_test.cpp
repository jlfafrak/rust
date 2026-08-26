#include "eos_rust_abi.h"

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_alloc_after(uint32_t successful_allocations,
                                    int32_t status);
}

static_assert(offsetof(eos_rust_addrinfo, ai_flags) == 0);
static_assert(offsetof(eos_rust_addrinfo, ai_family) == 4);
static_assert(offsetof(eos_rust_addrinfo, ai_socktype) == 8);
static_assert(offsetof(eos_rust_addrinfo, ai_protocol) == 12);
static_assert(offsetof(eos_rust_addrinfo, ai_addrlen) == 16);
static_assert(offsetof(eos_rust_addrinfo, ai_addr) == 24);
static_assert(offsetof(eos_rust_addrinfo, ai_canonname) == 32);
static_assert(offsetof(eos_rust_addrinfo, ai_next) == 40);
static_assert(sizeof(eos_rust_addrinfo) == 48);

static int expect(bool condition, const char *message) {
    if (condition) return EXIT_SUCCESS;
    std::fprintf(stderr, "%s\n", message);
    return EXIT_FAILURE;
}

static uint16_t network_u16(uint16_t value) {
    return static_cast<uint16_t>((value << 8U) | (value >> 8U));
}

static uint32_t result_count(const eos_rust_addrinfo *result) {
    uint32_t count = 0;
    while (result != nullptr) {
        ++count;
        result = result->ai_next;
    }
    return count;
}

int main() {
    eos_host_test_reset();
    eos_rust_in_addr ipv4{};
    eos_rust_in6_addr ipv6{};
    char text[64]{};
    if (expect(eos_rust_inet_pton(EOS_RUST_AF_INET, "192.0.2.129", &ipv4) == 1 &&
                   ipv4.s_addr == UINT32_C(0x810200c0),
               "inet_pton must parse strict IPv4 into network bytes")) return EXIT_FAILURE;
    if (expect(eos_rust_inet_ntop(EOS_RUST_AF_INET, &ipv4, text,
                                  sizeof(text)) == text &&
                   std::strcmp(text, "192.0.2.129") == 0,
               "inet_ntop must format canonical IPv4")) return EXIT_FAILURE;
    if (expect(eos_rust_inet_pton(EOS_RUST_AF_INET6, "2001:db8::1", &ipv6) == 1 &&
                   eos_rust_inet_ntop(EOS_RUST_AF_INET6, &ipv6, text,
                                      sizeof(text)) == text &&
                   std::strcmp(text, "2001:db8::1") == 0,
               "IPv6 numeric conversion must support compression")) return EXIT_FAILURE;
    if (expect(eos_rust_inet_pton(EOS_RUST_AF_INET, "01.2.3.4", &ipv4) == 0 &&
                   eos_rust_inet_pton(EOS_RUST_AF_INET, "256.2.3.4", &ipv4) == 0 &&
                   eos_rust_inet_pton(EOS_RUST_AF_INET, "1.2.3", &ipv4) == 0,
               "IPv4 parser must reject leading, overflow, and short syntax")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_inet_pton(EOS_RUST_AF_INET6, "::", &ipv6) == 1 &&
                   eos_rust_inet_ntop(EOS_RUST_AF_INET6, &ipv6, text,
                                      sizeof(text)) == text &&
                   std::strcmp(text, "::") == 0 &&
                   eos_rust_inet_pton(EOS_RUST_AF_INET6,
                                      "::ffff:192.0.2.1", &ipv6) == 1 &&
                   eos_rust_inet_ntop(EOS_RUST_AF_INET6, &ipv6, text,
                                      sizeof(text)) == text &&
                   std::strcmp(text, "::ffff:c000:201") == 0,
               "IPv6 conversion must cover all-zero and embedded IPv4 forms")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_inet_pton(EOS_RUST_AF_INET6, "1::2::3", &ipv6) == 0 &&
                   eos_rust_inet_pton(EOS_RUST_AF_INET6,
                                      "1:2:3:4:5:6:7:8:9", &ipv6) == 0 &&
                   eos_rust_inet_pton(EOS_RUST_AF_INET6, "1:2:3:4:5:6:7:",
                                      &ipv6) == 0,
               "IPv6 parser must reject duplicate compression and excess/trailing groups")) {
        return EXIT_FAILURE;
    }
    char short_text[3] = {'x', 'y', 'z'};
    if (expect(eos_rust_inet_ntop(EOS_RUST_AF_INET6, &ipv6, short_text,
                                  sizeof(short_text)) == nullptr &&
                   *eos_rust_errno_location() == 28 &&
                   short_text[0] == 'x',
               "inet_ntop must reject a short buffer without partial output")) {
        return EXIT_FAILURE;
    }
    if (expect(eos_rust_inet_pton(99, "127.0.0.1", &ipv4) == -1 &&
                   *eos_rust_errno_location() == 47,
               "inet_pton must reject unsupported family with EAFNOSUPPORT")) {
        return EXIT_FAILURE;
    }

    eos_rust_addrinfo hints{};
    eos_rust_addrinfo *result = nullptr;
    hints.ai_family = EOS_RUST_AF_UNSPEC;
    hints.ai_socktype = EOS_RUST_SOCK_STREAM;
    if (expect(eos_rust_getaddrinfo("127.0.0.1", "8080", &hints, &result) == 0 &&
                   result != nullptr && result->ai_family == EOS_RUST_AF_INET &&
                   result->ai_socktype == EOS_RUST_SOCK_STREAM &&
                   result->ai_protocol == EOS_RUST_IPPROTO_TCP &&
                   reinterpret_cast<eos_rust_sockaddr_in *>(result->ai_addr)
                           ->sin_port == network_u16(8080),
               "getaddrinfo must allocate a numeric stream result")) return EXIT_FAILURE;
    eos_rust_freeaddrinfo(result);
    result = nullptr;
    *eos_rust_errno_location() = 0;
    if (expect(eos_rust_getaddrinfo("localhost", "80", &hints, &result) ==
                       EOS_RUST_EAI_SYSTEM &&
                   *eos_rust_errno_location() == 45 && result == nullptr,
               "hostname lookup must fail honestly with EAI_SYSTEM/ENOTSUP")) {
        return EXIT_FAILURE;
    }
    eos_rust_freeaddrinfo(nullptr);

    hints = {};
    hints.ai_flags = EOS_RUST_AI_CANONNAME;
    if (expect(eos_rust_getaddrinfo("2001:db8::2", "443", &hints,
                                    &result) == 0 &&
                   result_count(result) == 2 &&
                   result->ai_family == EOS_RUST_AF_INET6 &&
                   result->ai_socktype == EOS_RUST_SOCK_STREAM &&
                   result->ai_next->ai_socktype == EOS_RUST_SOCK_DGRAM &&
                   result->ai_canonname != nullptr &&
                   std::strcmp(result->ai_canonname, "2001:db8::2") == 0,
               "numeric IPv6 resolution must provide deterministic type order and canonname")) {
        return EXIT_FAILURE;
    }
    eos_rust_freeaddrinfo(result);
    result = nullptr;

    hints = {};
    if (expect(eos_rust_getaddrinfo(nullptr, "0", &hints, &result) == 0 &&
                   result_count(result) == 4 &&
                   result->ai_family == EOS_RUST_AF_INET6 &&
                   result->ai_next->ai_family == EOS_RUST_AF_INET6 &&
                   result->ai_next->ai_next->ai_family == EOS_RUST_AF_INET,
               "null-node resolution must use deterministic IPv6-then-IPv4/type order")) {
        return EXIT_FAILURE;
    }
    const auto *loopback6 = reinterpret_cast<const eos_rust_sockaddr_in6 *>(
        result->ai_addr);
    if (expect(loopback6->sin6_addr.s6_addr[15] == 1,
               "non-passive null node must produce IPv6 loopback")) return EXIT_FAILURE;
    eos_rust_freeaddrinfo(result);
    result = nullptr;
    hints.ai_family = EOS_RUST_AF_INET;
    hints.ai_socktype = EOS_RUST_SOCK_DGRAM;
    hints.ai_flags = EOS_RUST_AI_PASSIVE;
    if (expect(eos_rust_getaddrinfo(nullptr, "9", &hints, &result) == 0 &&
                   result_count(result) == 1 &&
                   reinterpret_cast<eos_rust_sockaddr_in *>(result->ai_addr)
                           ->sin_addr.s_addr == 0,
               "AI_PASSIVE null IPv4 node must produce wildcard")) return EXIT_FAILURE;
    eos_rust_freeaddrinfo(result);
    result = nullptr;

    hints = {};
    hints.ai_flags = EOS_RUST_AI_NUMERICHOST;
    if (expect(eos_rust_getaddrinfo("not-numeric", "80", &hints, &result) ==
                       EOS_RUST_EAI_NONAME && result == nullptr,
               "AI_NUMERICHOST invalid input must return EAI_NONAME")) return EXIT_FAILURE;
    hints = {};
    hints.ai_flags = EOS_RUST_AI_V4MAPPED;
    if (expect(eos_rust_getaddrinfo("127.0.0.1", "80", &hints, &result) ==
                       EOS_RUST_EAI_BADFLAGS,
               "unsupported resolver flags must return EAI_BADFLAGS")) return EXIT_FAILURE;
    hints = {};
    hints.ai_socktype = EOS_RUST_SOCK_STREAM;
    hints.ai_protocol = EOS_RUST_IPPROTO_UDP;
    if (expect(eos_rust_getaddrinfo("127.0.0.1", "80", &hints, &result) ==
                       EOS_RUST_EAI_SOCKTYPE,
               "inconsistent socket type/protocol must return EAI_SOCKTYPE")) {
        return EXIT_FAILURE;
    }
    hints = {};
    *eos_rust_errno_location() = 0;
    if (expect(eos_rust_getaddrinfo("127.0.0.1", "http", &hints, &result) ==
                       EOS_RUST_EAI_SYSTEM &&
                   *eos_rust_errno_location() == 45,
               "named service must fail honestly with EAI_SYSTEM/ENOTSUP")) {
        return EXIT_FAILURE;
    }

    hints = {};
    hints.ai_family = EOS_RUST_AF_INET;
    hints.ai_socktype = EOS_RUST_SOCK_STREAM;
    hints.ai_flags = EOS_RUST_AI_CANONNAME;
    for (uint32_t failure_point = 0; failure_point < 3; ++failure_point) {
        eos_host_test_fail_alloc_after(failure_point, 15);
        result = reinterpret_cast<eos_rust_addrinfo *>(UINTPTR_MAX);
        if (expect(eos_rust_getaddrinfo("127.0.0.1", "80", &hints,
                                       &result) == EOS_RUST_EAI_MEMORY &&
                       result == nullptr,
                   "resolver allocation fault must clean partial result")) {
            return EXIT_FAILURE;
        }
    }
    if (expect(eos_rust_getaddrinfo("127.0.0.1", "80", &hints, &result) == 0,
               "resolver must recover after every allocation fault")) return EXIT_FAILURE;
    eos_rust_freeaddrinfo(result);
    result = nullptr;

    std::atomic<int> concurrency_failures{0};
    auto resolve_independent = [&](const char *node) {
        eos_rust_addrinfo *local = nullptr;
        eos_rust_addrinfo local_hints{};
        local_hints.ai_socktype = EOS_RUST_SOCK_DGRAM;
        if (eos_rust_getaddrinfo(node, "53", &local_hints, &local) != 0 ||
            local == nullptr) {
            concurrency_failures.fetch_add(1, std::memory_order_relaxed);
        }
        eos_rust_freeaddrinfo(local);
    };
    std::thread first(resolve_independent, "192.0.2.1");
    std::thread second(resolve_independent, "2001:db8::1");
    first.join();
    second.join();
    if (expect(concurrency_failures.load(std::memory_order_relaxed) == 0,
               "independent resolver lists must be concurrency-safe")) return EXIT_FAILURE;

    static constexpr int32_t errors[] = {
        EOS_RUST_EAI_BADFLAGS, EOS_RUST_EAI_NONAME, EOS_RUST_EAI_AGAIN,
        EOS_RUST_EAI_FAIL, EOS_RUST_EAI_FAMILY, EOS_RUST_EAI_SOCKTYPE,
        EOS_RUST_EAI_SERVICE, EOS_RUST_EAI_MEMORY, EOS_RUST_EAI_SYSTEM,
        EOS_RUST_EAI_OVERFLOW, 999};
    for (int32_t error : errors) {
        if (expect(std::strlen(eos_rust_gai_strerror(error)) != 0,
                   "gai_strerror must describe every pinned and unknown EAI value")) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
