#include "eos_rust_abi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_alloc(int32_t status);
void eos_host_test_fail_next_lock(int32_t status);
void eos_host_test_fail_next_sync_wait(int32_t status);
void eos_host_test_fail_next_thread_create(int32_t status);
uint32_t eos_host_test_last_thread_stack(void);
uint32_t eos_host_test_last_thread_features(void);
uint32_t eos_host_test_last_thread_priority(void);
uint32_t eos_host_test_native_thread_delete_count(void);
uint32_t eos_thread_test_live_records(void);
uint32_t eos_thread_test_state(eos_rust_thread_t thread);
void eos_host_test_finish_thread_before_create_returns(int enabled);
uint32_t eos_host_test_sync_create_count(void);
uint32_t eos_host_test_sync_destroy_count(void);
void eos_host_test_fail_next_sync_destroy(int32_t status);
void eos_host_test_start_thread_then_fail(int32_t status);
void eos_thread_test_pause_after_completion(int enabled);
uint32_t eos_thread_test_completion_pause_entered(void);
void eos_thread_test_resume_after_completion(void);
uint32_t eos_thread_test_completion_cleanup_finished(void);
void eos_thread_test_reset_publication_audit(void);
uint32_t eos_thread_test_destroyed_before_create_return(void);
}

namespace {
constexpr int32_t kEsrch = 3;
constexpr int32_t kIo = 5;
constexpr int32_t kDeadlock = 11;
constexpr int32_t kNoMemory = 12;
constexpr int32_t kInvalid = 22;
constexpr int32_t kRange = 34;
constexpr int32_t kNotSupported = 45;
constexpr int32_t kNameTooLong = 63;

[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

void expect(bool condition, const char *message) {
    if (!condition) fail(message);
}

void wait_until_stale(eos_rust_thread_t thread, const char *message) {
    char name[2]{};
    int32_t status = 0;
    for (int spin = 0; spin != 10000; ++spin) {
        status = eos_rust_pthread_getname_np(thread, name, sizeof(name));
        if (status == kEsrch) return;
        std::this_thread::yield();
    }
    fail(message);
}

void wait_for_sync_destroy(uint32_t before, const char *message) {
    for (int spin = 0; spin != 10000; ++spin) {
        if (eos_host_test_sync_destroy_count() > before) return;
        std::this_thread::yield();
    }
    fail(message);
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

void *return_argument(void *argument) { return argument; }

void *self_join(void *) {
    const eos_rust_thread_t self = eos_rust_pthread_self();
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(eos_rust_pthread_join(self, nullptr)));
}

void *self_detach(void *) {
    const eos_rust_thread_t self = eos_rust_pthread_self();
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(eos_rust_pthread_detach(self)));
}

void *set_own_name(void *) {
    const eos_rust_thread_t self = eos_rust_pthread_self();
    const int32_t result = eos_rust_pthread_setname_np(self, "worker-name");
    return reinterpret_cast<void *>(static_cast<uintptr_t>(result));
}

struct Gate {
    std::atomic<bool> release{false};
};

void *wait_gate(void *argument) {
    auto *gate = static_cast<Gate *>(argument);
    while (!gate->release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return reinterpret_cast<void *>(static_cast<uintptr_t>(0x12345678U));
}

void test_attributes_and_forwarding() {
    eos_rust_pthread_attr zero{};
    eos_rust_pthread_attr initialized{};
    uint32_t stack = 0;
    eos_rust_thread_t thread = 0;
    void *result = nullptr;

    expect(sizeof(eos_rust_thread_t) == 4, "thread handle layout drifted");
    expect(sizeof(eos_rust_tls_key_t) == 4, "TLS key layout drifted");
    expect(sizeof(eos_rust_pthread_attr) == 16 &&
               alignof(eos_rust_pthread_attr) == alignof(uint32_t),
           "pthread attr layout drifted");
    expect(eos_rust_pthread_attr_getstacksize(&zero, &stack) == 0 &&
               stack == 4096,
           "all-zero attributes must select the 4096-byte default");
    expect(eos_rust_pthread_attr_init(&initialized) == 0,
           "attribute init failed");
    expect(eos_rust_pthread_attr_getstacksize(&initialized, &stack) == 0 &&
               stack == 4096,
           "initialized attributes must select the default stack");
    expect(eos_rust_pthread_attr_setstacksize(&initialized, 4095) == kInvalid,
           "a sub-minimum stack must be rejected");
    expect(eos_rust_pthread_attr_setstacksize(&initialized, 4097) == kInvalid,
           "a misaligned stack must be rejected");
    expect(eos_rust_pthread_attr_setstacksize(&initialized, 8192) == 0,
           "a valid stack must be accepted");
    expect(eos_rust_pthread_create(&thread, &initialized, return_argument,
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(0x81U))) == 0,
           "thread creation with attributes failed");
    expect(eos_host_test_last_thread_stack() == 8192 &&
               eos_host_test_last_thread_features() == 0 &&
               eos_host_test_last_thread_priority() == 200,
           "native thread arguments were not forwarded exactly");
    expect(eos_rust_pthread_join(thread, &result) == 0 &&
               result == reinterpret_cast<void *>(static_cast<uintptr_t>(0x81U)),
           "join did not preserve the exact start result");
    expect(eos_rust_pthread_attr_destroy(&initialized) == 0,
           "attribute destroy failed");
    expect(eos_rust_pthread_attr_getstacksize(&initialized, &stack) == kInvalid &&
               eos_rust_pthread_attr_setstacksize(&initialized, 8192) == kInvalid,
           "destroyed attributes must be rejected");
    expect(eos_rust_pthread_attr_destroy(&zero) == 0,
           "destroying an all-zero attribute must succeed");
    *eos_rust_errno_location() = 777;
    expect(eos_rust_pthread_attr_setstacksize(&zero, 1) == kInvalid &&
               *eos_rust_errno_location() == 777,
           "pthread-shaped failures must preserve compatibility errno");

    const eos_rust_pthread_attr malformed[] = {
        {{0, 4096, 0, 0}},
        {{0, 0, 1, 0}},
        {{0, 0, 0, 1}},
        {{UINT32_C(0x45504131), 0, 0, 0}},
        {{UINT32_C(0x45504131), 4097, 0, 0}},
        {{UINT32_C(0x45504131), 4096, 1, 0}},
        {{UINT32_C(0x45504131), 4096, 0, 1}},
        {{UINT32_C(0x45504431), 0, 0, 0}},
        {{UINT32_C(0x10203040), 4096, 0, 0}},
    };
    for (const auto &representation : malformed) {
        eos_rust_pthread_attr candidate = representation;
        eos_rust_thread_t rejected = UINT32_MAX;
        expect(eos_rust_pthread_attr_getstacksize(&candidate, &stack) ==
                   kInvalid,
               "malformed attr getstacksize must fail");
        candidate = representation;
        expect(eos_rust_pthread_attr_setstacksize(&candidate, 8192) == kInvalid,
               "malformed attr setstacksize must fail");
        candidate = representation;
        expect(eos_rust_pthread_attr_destroy(&candidate) == kInvalid,
               "malformed attr destroy must fail");
        expect(eos_rust_pthread_create(&rejected, &representation,
                                       return_argument, nullptr) == kInvalid &&
                   rejected == 0,
               "malformed attr create must fail before native publication");
    }
}

void test_identity_join_detach_and_failures() {
    eos_rust_thread_t first = 0;
    eos_rust_thread_t second = 0;
    eos_rust_thread_t thread = 0;
    void *result = nullptr;

    expect(eos_rust_pthread_create(&first, nullptr, return_argument,
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(0xcafebabeU))) == 0 &&
               eos_rust_pthread_create(&second, nullptr, return_argument, nullptr) == 0,
           "basic thread creation failed");
    expect(first != 0 && second != 0 && first != second,
           "thread identities must be unique and nonzero");
    expect(eos_rust_pthread_equal(first, first) != 0 &&
               eos_rust_pthread_equal(first, second) == 0,
           "pthread_equal identity behavior is wrong");
    expect(eos_rust_pthread_join(first, &result) == 0 &&
               result == reinterpret_cast<void *>(static_cast<uintptr_t>(0xcafebabeU)),
           "join result was not preserved");
    expect(eos_rust_pthread_join(first, nullptr) == kEsrch,
           "a harvested handle must be stale");
    const uint32_t second_destroy_before = eos_host_test_sync_destroy_count();
    expect(eos_rust_pthread_detach(second) == 0,
           "detach failed");
    wait_until_stale(second, "detached basic worker did not self-reap");
    wait_for_sync_destroy(second_destroy_before,
                          "detached basic worker did not destroy its sync");

    eos_rust_thread_t self_thread = 0;
    expect(eos_rust_pthread_create(&self_thread, nullptr, self_join, nullptr) == 0 &&
               eos_rust_pthread_join(self_thread, &result) == 0 &&
               static_cast<int32_t>(reinterpret_cast<uintptr_t>(result)) == kDeadlock,
           "joining self must report EDEADLK");

    const uint32_t live_before_failure = eos_thread_test_live_records();
    const uint32_t sync_before_failure = eos_host_test_sync_destroy_count();
    eos_host_test_fail_next_thread_create(25);
    thread = 0;
    expect(eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr) == kIo &&
               thread == 0,
           "native creation failure must map and clear output");
    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr) == kNoMemory,
           "record allocation failure must report ENOMEM");
    eos_host_test_fail_next_lock(17);
    expect(eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr) == 16,
           "registry lock failure must map directly");
    expect(eos_host_test_native_thread_delete_count() == 0,
           "compatibility code must never delete an auto-reaped native thread");
    expect(eos_thread_test_live_records() == live_before_failure &&
               eos_host_test_sync_destroy_count() == sync_before_failure + 2,
           "failed create paths must release every compatibility record/sync once");
}

void test_auto_start_publication_and_claims() {
    const uint32_t live_before = eos_thread_test_live_records();
    const uint32_t sync_created_before = eos_host_test_sync_create_count();
    const uint32_t sync_destroyed_before = eos_host_test_sync_destroy_count();
    eos_rust_thread_t completed = 0;
    void *result = nullptr;
    eos_host_test_finish_thread_before_create_returns(1);
    expect(eos_rust_pthread_create(
               &completed, nullptr, return_argument,
               reinterpret_cast<void *>(static_cast<uintptr_t>(0xa5U))) == 0,
           "child-before-create-return setup failed");
    eos_host_test_finish_thread_before_create_returns(0);
    expect(eos_rust_pthread_join(completed, &result) == 0 &&
               result == reinterpret_cast<void *>(static_cast<uintptr_t>(0xa5U)),
           "a child that exits before create returns must remain joinable");
    expect(eos_thread_test_live_records() == live_before &&
               eos_host_test_sync_create_count() == sync_created_before + 1 &&
               eos_host_test_sync_destroy_count() == sync_destroyed_before + 1,
           "completed publication must release its compatibility resources once");

    eos_rust_thread_t self_detached = 0;
    eos_thread_test_reset_publication_audit();
    eos_host_test_finish_thread_before_create_returns(1);
    expect(eos_rust_pthread_create(&self_detached, nullptr, self_detach,
                                   nullptr) == 0,
           "self-detach-before-create-return setup failed");
    eos_host_test_finish_thread_before_create_returns(0);
    expect(eos_thread_test_destroyed_before_create_return() == 0,
           "auto-start self-detach destroyed the record before create returned");
    wait_until_stale(self_detached,
                     "auto-start self-detached handle did not become stale");

    eos_rust_thread_t self_joined = 0;
    eos_host_test_finish_thread_before_create_returns(1);
    expect(eos_rust_pthread_create(&self_joined, nullptr, self_join, nullptr) == 0,
           "self-join-before-create-return setup failed");
    eos_host_test_finish_thread_before_create_returns(0);
    expect(eos_rust_pthread_join(self_joined, &result) == 0 &&
               static_cast<int32_t>(reinterpret_cast<uintptr_t>(result)) ==
                   kDeadlock,
           "auto-start self-join did not remain safely joinable");

    Gate gate;
    eos_rust_thread_t joinable = 0;
    std::atomic<int32_t> first_join{kIo};
    expect(eos_rust_pthread_create(&joinable, nullptr, wait_gate, &gate) == 0,
           "double-join worker creation failed");
    std::thread joiner([&] { first_join = eos_rust_pthread_join(joinable, nullptr); });
    while (eos_thread_test_state(joinable) != 1) std::this_thread::yield();
    expect(eos_rust_pthread_join(joinable, nullptr) == kInvalid,
           "a second live join claim must report EINVAL");
    gate.release = true;
    joiner.join();
    expect(first_join.load() == 0, "the original join claim failed");

    Gate detach_gate;
    eos_rust_thread_t detachable = 0;
    expect(eos_rust_pthread_create(&detachable, nullptr, wait_gate,
                                   &detach_gate) == 0 &&
               eos_rust_pthread_detach(detachable) == 0 &&
               eos_rust_pthread_detach(detachable) == kInvalid,
           "a second live detach claim must report EINVAL");
    detach_gate.release = true;
    wait_until_stale(detachable, "double-detach worker did not self-reap");
}

void test_names_and_yield() {
    const eos_rust_thread_t current = eos_rust_pthread_self();
    char name[80]{};
    char short_name[5]{};
    char max_name[64];
    char too_long[65];
    eos_rust_thread_t child = 0;
    void *result = nullptr;
    std::memset(max_name, 'x', 63);
    max_name[63] = '\0';
    std::memset(too_long, 'y', 64);
    too_long[64] = '\0';

    expect(current != 0 && eos_rust_pthread_equal(current, current) != 0,
           "current thread must receive a public identity");
    expect(eos_rust_pthread_setname_np(current, "") == 0 &&
               eos_rust_pthread_getname_np(current, name, sizeof(name)) == 0 &&
               name[0] == '\0',
           "zero-byte name behavior failed");
    expect(eos_rust_pthread_setname_np(current, max_name) == 0,
           "63-byte name must be accepted");
    expect(eos_rust_pthread_setname_np(current, too_long) == kNameTooLong,
           "64-byte name must be rejected");
    expect(eos_rust_pthread_getname_np(current, short_name,
                                       sizeof(short_name)) == kRange &&
               short_name[sizeof(short_name) - 1] == '\0' &&
               std::memcmp(short_name, "xxxx", 4) == 0,
           "short getname buffers must contain a terminated prefix");
    expect(eos_rust_pthread_getname_np(current, name, 0) == kRange,
           "zero-capacity getname must report ERANGE");
    expect(eos_rust_pthread_create(&child, nullptr, set_own_name, nullptr) == 0 &&
               eos_rust_pthread_join(child, &result) == 0 && result == nullptr,
           "child name operation failed");
    expect(eos_rust_pthread_getname_np(child, name, sizeof(name)) == kEsrch,
           "harvested names must be stale");
    expect(eos_rust_pthread_yield() == kNotSupported,
           "yield must honestly report ENOTSUP on MARTOS 14.0.39");

    Gate gate;
    eos_rust_thread_t other = 0;
    expect(eos_rust_pthread_create(&other, nullptr, wait_gate, &gate) == 0 &&
               eos_rust_pthread_getname_np(other, name, sizeof(name)) == 0 &&
               std::strcmp(name, "eos.rust") == 0 &&
               eos_rust_pthread_setname_np(other, "other") == 0 &&
               eos_rust_pthread_getname_np(other, name, sizeof(name)) == 0 &&
               std::strcmp(name, "other") == 0,
           "current caller must safely access another live compatibility name");
    gate.release = true;
    expect(eos_rust_pthread_join(other, nullptr) == 0,
           "named other thread join failed");
}

void test_join_detach_race_and_wait_failure() {
    for (int iteration = 0; iteration != 50; ++iteration) {
        Gate gate;
        eos_rust_thread_t thread = 0;
        std::atomic<int32_t> joined{kIo};
        std::atomic<int32_t> detached{kIo};
        expect(eos_rust_pthread_create(&thread, nullptr, wait_gate, &gate) == 0,
               "race worker creation failed");
        std::thread joiner([&] { joined = eos_rust_pthread_join(thread, nullptr); });
        std::thread detacher([&] { detached = eos_rust_pthread_detach(thread); });
        gate.release.store(true, std::memory_order_release);
        joiner.join();
        detacher.join();
        const int successes = (joined.load() == 0 ? 1 : 0) +
                              (detached.load() == 0 ? 1 : 0);
        expect(successes == 1,
               "exactly one concurrent join/detach claim must succeed");
        expect(joined.load() == 0 || joined.load() == kInvalid ||
                   joined.load() == kEsrch,
               "losing join returned an invalid status");
        expect(detached.load() == 0 || detached.load() == kInvalid ||
                   detached.load() == kEsrch,
               "losing detach returned an invalid status");
        if (detached.load() == 0) {
            wait_until_stale(thread, "detach-race worker did not self-reap");
        }
    }

    Gate gate;
    eos_rust_thread_t thread = 0;
    expect(eos_rust_pthread_create(&thread, nullptr, wait_gate, &gate) == 0,
           "wait-failure worker creation failed");
    eos_host_test_fail_next_sync_wait(25);
    expect(eos_rust_pthread_join(thread, nullptr) == kIo,
           "completion wait failure must map directly");
    gate.release.store(true, std::memory_order_release);
    expect(eos_rust_pthread_detach(thread) == 0,
           "failed join must roll back its claim");
    wait_until_stale(thread, "wait-failure detached worker did not self-reap");
}

void test_detach_after_publication_before_child_registry_cleanup() {
    const uint32_t baseline = eos_thread_test_live_records();
    const uint32_t destroyed = eos_host_test_sync_destroy_count();
    eos_rust_thread_t thread = 0;
    char name[8]{};
    eos_thread_test_pause_after_completion(1);
    expect(eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr) == 0,
           "post-publication pause worker creation failed");
    while (eos_thread_test_completion_pause_entered() == 0) {
        std::this_thread::yield();
    }
    expect(eos_rust_pthread_detach(thread) == 0,
           "detach in post-publication window failed");
    eos_thread_test_resume_after_completion();
    for (int spin = 0; spin != 10000 &&
                       eos_thread_test_completion_cleanup_finished() == 0;
         ++spin) {
        std::this_thread::yield();
    }
    expect(eos_thread_test_completion_cleanup_finished() != 0 &&
               eos_thread_test_live_records() == baseline &&
               eos_host_test_sync_destroy_count() == destroyed + 1 &&
               eos_rust_pthread_getname_np(thread, name, sizeof(name)) == kEsrch,
           "post-publication detach did not release exactly one registry reference");
}

#ifndef EOS_RUST_TSAN_TEST
void test_irreversible_cleanup_failure_aborts() {
    expect_abort([] {
        eos_rust_thread_t thread = 0;
        (void)eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr);
        eos_host_test_fail_next_sync_destroy(25);
        (void)eos_rust_pthread_join(thread, nullptr);
    }, "completion-sync destruction failure must abort");
    expect_abort([] {
        eos_rust_thread_t thread = 0;
        eos_host_test_start_thread_then_fail(25);
        (void)eos_rust_pthread_create(&thread, nullptr, return_argument, nullptr);
    }, "a malformed native create that starts then fails must abort");
}
#endif
} // namespace

int main() {
    eos_host_test_reset();
    test_attributes_and_forwarding();
    test_detach_after_publication_before_child_registry_cleanup();
    test_identity_join_detach_and_failures();
    test_auto_start_publication_and_claims();
    test_names_and_yield();
    test_join_detach_race_and_wait_failure();
#ifndef EOS_RUST_TSAN_TEST
    test_irreversible_cleanup_failure_aborts();
#endif
    return 0;
}
