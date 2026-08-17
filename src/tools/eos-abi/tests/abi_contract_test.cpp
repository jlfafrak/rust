#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <cstddef>

static_assert(sizeof(eos_rust_timespec) == 16);
static_assert(alignof(eos_rust_timespec) == alignof(int64_t));
static_assert(offsetof(eos_rust_timespec, tv_sec) == 0);
static_assert(offsetof(eos_rust_timespec, tv_nsec) == 8);
static_assert(sizeof(eos_rust_pthread_mutex) == 16);
static_assert(alignof(eos_rust_pthread_mutex) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_mutex, words) == 0);
static_assert(sizeof(eos_rust_pthread_mutexattr) == 8);
static_assert(alignof(eos_rust_pthread_mutexattr) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_mutexattr, words) == 0);
static_assert(sizeof(eos_rust_pthread_cond) == 16);
static_assert(alignof(eos_rust_pthread_cond) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_cond, words) == 0);
static_assert(sizeof(eos_rust_pthread_condattr) == 8);
static_assert(alignof(eos_rust_pthread_condattr) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_condattr, words) == 0);
static_assert(sizeof(eos_rust_pthread_rwlock) == 16);
static_assert(alignof(eos_rust_pthread_rwlock) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_rwlock, words) == 0);
static_assert(sizeof(eos_rust_pthread_once_t) == 8);
static_assert(alignof(eos_rust_pthread_once_t) == alignof(uint32_t));
static_assert(offsetof(eos_rust_pthread_once_t, words) == 0);

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
