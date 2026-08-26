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

void test_saturated_table_reuses_a_closed_slot_with_an_active_reader() {
    constexpr uint64_t retained_identity = UINT64_C(600);
    constexpr uint64_t replacement_identity = UINT64_C(700);
    eos_fd_test_reset();
    const char *arguments[] = {"fd-saturated-reuse", nullptr};
    eos_rust_runtime_init(INT32_C(1), arguments);

    std::vector<int32_t> descriptors;
    for (uint32_t index = UINT32_C(3);
         index < eos_fd_test_capacity();
         ++index) {
        int32_t descriptor = eos_fd_test_allocate(
            EOS_FD_KIND_FILE,
            retained_identity + (uint64_t)(index - UINT32_C(3)),
            0);
        expect(descriptor == (int32_t)index,
               "saturated-reuse fixture did not fill the descriptor table");
        descriptors.push_back(descriptor);
    }

    eos_fd_reference retained{};
    expect(eos_fd_test_acquire(descriptors.front(),
                               EOS_FD_KIND_FILE,
                               &retained) == 0,
           "saturated-reuse reader acquire failed");
    expect(eos_fd_test_close(descriptors.front()) == 0,
           "saturated-reuse close failed");
    expect(eos_fd_test_destructor_count(retained_identity) == 0,
           "the active reader must defer native destruction");
    int32_t replacement = eos_fd_test_allocate(
        EOS_FD_KIND_FILE, replacement_identity, 0);
    expect(replacement == descriptors.front(),
           "a saturated table must reuse the free fd before the old read ends");
    expect(eos_fd_test_release(&retained) == 0,
           "saturated-reuse reader release failed");
    expect(eos_fd_test_destructor_count(retained_identity) == 1,
           "retained object must be destroyed after the reader releases");
    expect(eos_fd_test_close(replacement) == 0,
           "saturated-reuse replacement close failed");
    for (std::size_t index = 1; index < descriptors.size(); ++index) {
        expect(eos_fd_test_close(descriptors[index]) == 0,
               "saturated-reuse fixture cleanup failed");
    }
}

void test_concurrent_copied_releases_consume_one_server_lease() {
    constexpr uint64_t identity = UINT64_C(800);
    eos_fd_test_reset();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, identity, 0);
    eos_fd_reference first{};
    expect(eos_fd_test_acquire(descriptor, EOS_FD_KIND_FILE, &first) == 0,
           "concurrent copied-release fixture acquire failed");
    eos_fd_reference second = first;
    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    std::atomic<bool> start{false};
    auto release_copy = [&](eos_fd_reference *reference) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        if (eos_fd_test_release(reference) == 0) {
            successes.fetch_add(1, std::memory_order_relaxed);
        } else {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread first_release(release_copy, &first);
    std::thread second_release(release_copy, &second);
    start.store(true, std::memory_order_release);
    first_release.join();
    second_release.join();
    expect(successes.load(std::memory_order_relaxed) == 1 &&
               failures.load(std::memory_order_relaxed) == 1,
           "concurrent copied releases must consume exactly one server lease");
    eos_fd_reference proof{};
    uint64_t observed = 0;
    expect(eos_fd_test_acquire(descriptor, EOS_FD_KIND_FILE, &proof) == 0 &&
               eos_fd_test_reference_identity(&proof, &observed) == 0 &&
               observed == identity && eos_fd_test_release(&proof) == 0,
           "copied-release race must leave descriptor ownership intact");
    expect(eos_fd_test_close(descriptor) == 0 &&
               eos_fd_test_destructor_count(identity) == 1,
           "copied-release race object must be destroyed exactly once");
}

} // namespace

int main() {
    test_saturated_table_reuses_a_closed_slot_with_an_active_reader();
    test_concurrent_copied_releases_consume_one_server_lease();
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
