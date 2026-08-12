#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
void eos_host_test_reset(void);
uint32_t eos_host_test_last_tls_get_slot(void);
uint32_t eos_host_test_last_tls_set_slot(void);
void eos_host_test_fail_next_tls_get(int32_t status);
void eos_host_test_fail_next_tls_set(int32_t status);
void eos_host_test_fail_tls_set_after(uint32_t successful_sets, int32_t status);
void eos_host_test_fail_next_alloc(int32_t status);
void eos_host_test_fail_next_free(int32_t status);
}

namespace {
[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}
void expect(bool condition, const char *message) {
    if (!condition) fail(message);
}

#ifndef EOS_RUST_TSAN_TEST
template <typename Function>
void expect_abort(Function function, const char *message) {
    const pid_t child = fork();
    expect(child >= 0, "fork failed");
    if (child == 0) {
        function();
        _exit(0);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child, "waitpid failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, message);
}
#endif

struct WorkerData {
    eos_rust_tls_key_t key;
    uintptr_t value;
};

void *tls_worker(void *opaque) {
    auto *data = static_cast<WorkerData *>(opaque);
    if (eos_rust_pthread_getspecific(data->key) != nullptr) return nullptr;
    if (eos_rust_pthread_setspecific(
            data->key, reinterpret_cast<void *>(data->value)) != 0) return nullptr;
    *eos_rust_errno_location() = static_cast<int32_t>(data->value & 0x7fffU);
    if (eos_rust_pthread_getspecific(data->key) !=
            reinterpret_cast<void *>(data->value)) return nullptr;
    return reinterpret_cast<void *>(static_cast<uintptr_t>(
        *eos_rust_errno_location()));
}

#ifndef EOS_RUST_TSAN_TEST
void *empty_worker(void *) { return nullptr; }
#endif

void test_errno_and_value_isolation() {
    eos_rust_tls_key_t key = 0;
    expect(eos_rust_pthread_key_create(&key, nullptr) == 0 && key != 0,
           "key creation failed");
    expect(eos_rust_pthread_getspecific(key) == nullptr,
           "TLS values must begin null");
    *eos_rust_errno_location() = 700;
    expect(eos_rust_pthread_setspecific(
               key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x1111U))) == 0,
           "main TLS set failed");

    WorkerData data{key, static_cast<uintptr_t>(0x2222U)};
    eos_rust_thread_t worker = 0;
    void *worker_result = nullptr;
    expect(eos_rust_pthread_create(&worker, nullptr, tls_worker, &data) == 0 &&
               eos_rust_pthread_join(worker, &worker_result) == 0 &&
               reinterpret_cast<uintptr_t>(worker_result) ==
                   (data.value & static_cast<uintptr_t>(0x7fffU)),
           "Rust-created thread TLS isolation failed");
    expect(*eos_rust_errno_location() == 700 &&
               eos_rust_pthread_getspecific(key) ==
                   reinterpret_cast<void *>(static_cast<uintptr_t>(0x1111U)),
           "child TLS changed main-thread state");

    std::atomic<void *> ordinary_value{reinterpret_cast<void *>(UINTPTR_MAX)};
    std::atomic<int32_t> ordinary_errno{-1};
    std::thread ordinary([&] {
        *eos_rust_errno_location() = 701;
        ordinary_value = eos_rust_pthread_getspecific(key);
        (void)eos_rust_pthread_setspecific(
            key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x3333U)));
        ordinary_errno = *eos_rust_errno_location();
    });
    ordinary.join();
    expect(ordinary_value.load() == nullptr && ordinary_errno.load() == 701,
           "ordinary host thread TLS isolation failed");
    expect(eos_host_test_last_tls_get_slot() == 7 &&
               eos_host_test_last_tls_set_slot() == 7,
           "compatibility roots must use only reserved TLS slot 7");
    expect(eos_rust_pthread_setspecific(key, nullptr) == 0 &&
               eos_rust_pthread_getspecific(key) == nullptr,
           "setspecific(NULL) must remove the value");
}

void test_key_lifecycle_and_concurrency() {
    eos_rust_tls_key_t deleted = 0;
    eos_rust_tls_key_t replacement = 0;
    expect(eos_rust_pthread_key_create(&deleted, nullptr) == 0 &&
               eos_rust_pthread_setspecific(
                   deleted, reinterpret_cast<void *>(static_cast<uintptr_t>(0x41U))) == 0 &&
               eos_rust_pthread_key_delete(deleted) == 0,
           "key deletion setup failed");
    expect(eos_rust_pthread_getspecific(deleted) == nullptr &&
               eos_rust_pthread_setspecific(deleted,
                                            reinterpret_cast<void *>(1)) == 22 &&
               eos_rust_pthread_key_delete(deleted) == 22,
           "deleted key must be stale and inaccessible");
    expect(eos_rust_pthread_key_create(&replacement, nullptr) == 0 &&
               replacement > deleted &&
               eos_rust_pthread_getspecific(replacement) == nullptr,
           "key identities must be monotonic and must not reveal stale values");

    eos_rust_tls_key_t allocation_key = UINT32_MAX;
    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_pthread_key_create(&allocation_key, nullptr) == 12 &&
               allocation_key == 0,
           "failed key allocation must leave its output cleared");
    expect(eos_rust_pthread_key_create(&allocation_key, nullptr) == 0,
           "key registry must remain usable after allocation failure");
    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_pthread_setspecific(
               allocation_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x88U))) == 12 &&
               eos_rust_pthread_getspecific(allocation_key) == nullptr,
           "setspecific allocation failure must preserve the old null value");

    constexpr size_t count = 128;
    std::vector<eos_rust_tls_key_t> keys(count);
    for (auto &key : keys) {
        expect(eos_rust_pthread_key_create(&key, nullptr) == 0,
               "many-key creation failed");
    }
    std::vector<std::thread> threads;
    std::atomic<bool> okay{true};
    for (uintptr_t thread_index = 1; thread_index <= 12; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            for (size_t index = 0; index < keys.size(); ++index) {
                const uintptr_t literal = (thread_index << 16) | index | 1U;
                if (eos_rust_pthread_setspecific(
                        keys[index], reinterpret_cast<void *>(literal)) != 0 ||
                    eos_rust_pthread_getspecific(keys[index]) !=
                        reinterpret_cast<void *>(literal)) {
                    okay = false;
                }
            }
        });
    }
    for (auto &thread : threads) thread.join();
    expect(okay.load(), "many concurrent keys/threads lost isolation");

    eos_rust_tls_key_t raced_key = 0;
    expect(eos_rust_pthread_key_create(&raced_key, nullptr) == 0,
           "concurrent-delete key creation failed");
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    threads.clear();
    for (uintptr_t index = 1; index <= 16; ++index) {
        threads.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) {}
            while (!stop.load(std::memory_order_acquire)) {
                const int32_t result = eos_rust_pthread_setspecific(
                    raced_key, reinterpret_cast<void *>(index));
                if (result != 0 && result != 22) okay = false;
                (void)eos_rust_pthread_getspecific(raced_key);
            }
        });
    }
    start.store(true, std::memory_order_release);
    expect(eos_rust_pthread_key_delete(raced_key) == 0,
           "concurrent key deletion failed");
    stop.store(true, std::memory_order_release);
    for (auto &thread : threads) thread.join();
    expect(okay.load() && eos_rust_pthread_getspecific(raced_key) == nullptr,
           "concurrent deletion caused an invalid result or exposed a stale value");
}

#ifndef EOS_RUST_TSAN_TEST
void test_root_failures_abort() {
    expect_abort([] {
        eos_host_test_fail_next_tls_get(25);
        (void)eos_rust_errno_location();
    }, "TLS root get failure must abort");
    expect_abort([] {
        eos_host_test_fail_next_tls_set(25);
        std::thread worker([] { (void)eos_rust_errno_location(); });
        worker.join();
    }, "TLS root install failure must abort");
    expect_abort([] {
        eos_host_test_fail_next_alloc(15);
        std::thread worker([] { (void)eos_rust_errno_location(); });
        worker.join();
    }, "TLS root allocation failure must abort");
    expect_abort([] {
        eos_rust_thread_t worker = 0;
        eos_host_test_fail_tls_set_after(1, 25);
        (void)eos_rust_pthread_create(&worker, nullptr, empty_worker, nullptr);
        (void)eos_rust_pthread_join(worker, nullptr);
    }, "TLS root clear failure after destructors must abort");
    expect_abort([] {
        eos_rust_thread_t worker = 0;
        eos_host_test_fail_next_free(25);
        (void)eos_rust_pthread_create(&worker, nullptr, empty_worker, nullptr);
        (void)eos_rust_pthread_join(worker, nullptr);
    }, "TLS root free failure after destructors must abort");
}
#endif
} // namespace

int main() {
    eos_host_test_reset();
    test_errno_and_value_isolation();
    test_key_lifecycle_and_concurrency();
#ifndef EOS_RUST_TSAN_TEST
    test_root_failures_abort();
#endif
    return 0;
}
