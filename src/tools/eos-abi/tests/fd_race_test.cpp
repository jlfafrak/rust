#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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

} // namespace

int main() {
    constexpr int reader_count = 31;
    constexpr uint64_t old_identity = UINT64_C(400);
    constexpr uint64_t new_identity = UINT64_C(401);
    eos_fd_test_reset();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, old_identity, 0);
    expect(descriptor == 3, "close/read race fixture allocation failed");

    std::atomic<int> acquired{0};
    std::atomic<int> failures{0};
    std::atomic<bool> complete_reads{false};
    std::vector<std::thread> threads;
    threads.reserve(reader_count + 1);
    for (int index = 0; index < reader_count; ++index) {
        threads.emplace_back([&] {
            eos_fd_reference reference{};
            uint64_t observed = 0;
            if (eos_fd_test_acquire(descriptor,
                                    EOS_FD_KIND_FILE,
                                    &reference) != 0 ||
                eos_fd_test_reference_identity(&reference, &observed) != 0 ||
                observed != old_identity) {
                failures.fetch_add(1, std::memory_order_relaxed);
                acquired.fetch_add(1, std::memory_order_release);
                return;
            }
            acquired.fetch_add(1, std::memory_order_release);
            while (!complete_reads.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            observed = 0;
            if (eos_fd_test_reference_identity(&reference, &observed) != 0 ||
                observed != old_identity ||
                eos_fd_test_release(&reference) != 0) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    threads.emplace_back([&] {
        while (acquired.load(std::memory_order_acquire) != reader_count) {
            std::this_thread::yield();
        }
        if (eos_fd_test_close(descriptor) != 0 ||
            eos_fd_test_destructor_count(old_identity) != 0) {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
        int32_t replacement =
            eos_fd_test_allocate(EOS_FD_KIND_FILE, new_identity, 0);
        if (replacement != descriptor || eos_fd_test_close(replacement) != 0 ||
            eos_fd_test_destructor_count(new_identity) != 1 ||
            eos_fd_test_destructor_count(old_identity) != 0) {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
        complete_reads.store(true, std::memory_order_release);
    });

    for (std::thread &thread : threads) {
        thread.join();
    }
    expect(failures.load(std::memory_order_relaxed) == 0,
           "32-thread close/read race violated descriptor ownership");
    expect(eos_fd_test_destructor_count(old_identity) == 1,
           "the raced old object must be destroyed exactly once after all reads");
    expect(eos_fd_test_destructor_count(new_identity) == 1,
           "the replacement object must be destroyed exactly once");
    return EXIT_SUCCESS;
}
