#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

void test_version_negotiation() {
    expect(eos_rust_abi_version() == UINT32_C(0x00010000),
           "ABI version must encode major 1 and minor 0");

    *eos_rust_errno_location() = 73;
    expect(eos_rust_abi_require(UINT32_C(1), UINT32_C(0)) == 0,
           "ABI 1.0 must satisfy a 1.0 requirement");
    expect(*eos_rust_errno_location() == 73,
           "successful ABI negotiation must preserve errno");

    expect(eos_rust_abi_require(UINT32_C(1), UINT32_C(1)) == -1,
           "ABI 1.0 must reject a newer required minor");
    expect(*eos_rust_errno_location() == 45,
           "an unavailable ABI minor must report EOS ENOTSUP (45)");

    expect(eos_rust_abi_require(UINT32_C(2), UINT32_C(0)) == -1,
           "ABI 1.x must reject required major 2");
    expect(*eos_rust_errno_location() == 43,
           "an incompatible ABI major must report EOS EPROTONOSUPPORT (43)");
}

void test_errno_is_thread_local() {
    constexpr int main_value = 101;
    constexpr int first_value = 202;
    constexpr int second_value = 303;

    *eos_rust_errno_location() = main_value;
    std::atomic<int32_t *> first_location{nullptr};
    std::atomic<int32_t *> second_location{nullptr};
    std::atomic<int> first_observed{0};
    std::atomic<int> second_observed{0};
    std::atomic<int> ready{0};
    std::atomic<bool> inspect{false};

    std::thread first([&] {
        first_location.store(eos_rust_errno_location(), std::memory_order_release);
        *eos_rust_errno_location() = first_value;
        ready.fetch_add(1, std::memory_order_release);
        while (!inspect.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        first_observed.store(*eos_rust_errno_location(), std::memory_order_release);
    });
    std::thread second([&] {
        second_location.store(eos_rust_errno_location(), std::memory_order_release);
        *eos_rust_errno_location() = second_value;
        ready.fetch_add(1, std::memory_order_release);
        while (!inspect.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        second_observed.store(*eos_rust_errno_location(), std::memory_order_release);
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    expect(first_location.load(std::memory_order_acquire) !=
               second_location.load(std::memory_order_acquire),
           "concurrent threads must receive different errno locations");
    inspect.store(true, std::memory_order_release);
    first.join();
    second.join();

    expect(first_location.load(std::memory_order_acquire) != nullptr,
           "the first thread must receive an errno location");
    expect(second_location.load(std::memory_order_acquire) != nullptr,
           "the second thread must receive an errno location");
    expect(first_observed.load(std::memory_order_acquire) == first_value,
           "the first thread must preserve its errno value");
    expect(second_observed.load(std::memory_order_acquire) == second_value,
           "the second thread must preserve its errno value");
    expect(*eos_rust_errno_location() == main_value,
           "worker writes must not change the main thread errno");
}

} // namespace

int main() {
    test_version_negotiation();
    test_errno_is_thread_local();
    return EXIT_SUCCESS;
}
