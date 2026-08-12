#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_console(uint32_t stream, int32_t status);
uint32_t eos_host_test_console_open_count(uint32_t stream);
void eos_host_test_fail_next_lock(int32_t status);
void eos_host_test_fail_lock_after(uint32_t successful_locks, int32_t status);
void eos_host_test_fail_next_unlock(int32_t status);
void eos_host_test_fail_next_alloc(int32_t status);
}

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

void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    *eos_rust_errno_location() = 0;
}

uint64_t identity_of(int32_t descriptor, eos_fd_kind kind) {
    eos_fd_reference reference{};
    uint64_t identity = 0;
    expect(eos_fd_test_acquire(descriptor, kind, &reference) == 0,
           "descriptor acquire failed");
    expect(eos_fd_test_reference_identity(&reference, &identity) == 0,
           "acquired reference did not expose its native identity");
    expect(eos_fd_test_release(&reference) == 0,
           "descriptor reference release failed");
    return identity;
}

void test_standard_descriptors_are_reserved_and_initialized_once() {
    reset_fixture();
    int32_t ordinary =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(100), 0);
    expect(ordinary == 3,
           "ordinary allocation before runtime init must start at fd 3");
    expect(eos_fd_test_close(ordinary) == 0,
           "ordinary pre-init descriptor close failed");

    const char *arguments[] = {"fd-table-test", nullptr};
    eos_rust_runtime_init(INT32_C(1), arguments);
    for (int32_t descriptor = 0; descriptor < 3; ++descriptor) {
        expect(eos_fd_test_is_kind(descriptor, EOS_FD_KIND_CONSOLE) == 1,
               "runtime init must install console descriptors 0, 1, and 2");
        expect(eos_host_test_console_open_count((uint32_t)descriptor) == 1,
               "each standard console must be established exactly once");
    }

    eos_rust_runtime_init(INT32_C(1), arguments);
    for (uint32_t stream = 0; stream < UINT32_C(3); ++stream) {
        expect(eos_host_test_console_open_count(stream) == 1,
               "idempotent runtime init must not reopen standard consoles");
    }

    expect(eos_fd_test_close(0) == 0,
           "an initialized standard descriptor must be closeable");
    ordinary = eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(101), 0);
    expect(ordinary == 3,
           "ordinary allocation must never reuse a reserved standard number");
    expect(eos_fd_test_close(ordinary) == 0,
           "ordinary descriptor cleanup failed");
}

void test_runtime_init_is_concurrency_safe() {
    reset_fixture();
    constexpr int thread_count = 32;
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    const char *arguments[] = {"fd-init-race", nullptr};
    threads.reserve(thread_count);
    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            eos_rust_runtime_init(INT32_C(1), arguments);
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &thread : threads) {
        thread.join();
    }

    for (uint32_t stream = 0; stream < UINT32_C(3); ++stream) {
        expect(eos_host_test_console_open_count(stream) == 1,
               "concurrent runtime init must establish each console once");
        expect(eos_fd_test_is_kind((int32_t)stream, EOS_FD_KIND_CONSOLE) == 1,
               "concurrent runtime init must publish every standard descriptor");
    }
}

void test_allocation_reuse_and_exhaustion() {
    reset_fixture();
    const uint32_t ordinary_capacity = eos_fd_test_capacity() - UINT32_C(3);
    std::vector<int32_t> descriptors;
    descriptors.reserve(ordinary_capacity);
    for (uint32_t index = 0; index < ordinary_capacity; ++index) {
        int32_t descriptor = eos_fd_test_allocate(
            EOS_FD_KIND_FILE, UINT64_C(1000) + index, 0);
        expect(descriptor == (int32_t)(index + UINT32_C(3)),
               "allocation must choose the lowest free ordinary descriptor");
        descriptors.push_back(descriptor);
    }

    expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(9999), 0) == -1,
           "a full descriptor table must reject allocation");
    expect(*eos_rust_errno_location() == 24,
           "descriptor exhaustion must report EOS EMFILE (24)");

    expect(eos_fd_test_close(5) == 0 && eos_fd_test_close(3) == 0,
           "reuse fixture closes failed");
    descriptors[2] = -1;
    descriptors[0] = -1;
    expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(2000), 0) == 3,
           "allocation must reuse the lowest free descriptor first");
    expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(2001), 0) == 5,
           "allocation must reuse the next lowest free descriptor");

    for (int32_t descriptor : descriptors) {
        if (descriptor >= 0) {
            expect(eos_fd_test_close(descriptor) == 0,
                   "exhaustion fixture cleanup failed");
        }
    }
    expect(eos_fd_test_close(3) == 0 && eos_fd_test_close(5) == 0,
           "reused descriptor cleanup failed");
}

void test_duplication_shares_ownership_and_not_descriptor_flags() {
    reset_fixture();
    int32_t original = eos_fd_test_allocate(
        EOS_FD_KIND_FILE, UINT64_C(10), EOS_FD_FLAG_CLOEXEC);
    int32_t duplicate = eos_fd_test_dup(original);
    expect(original == 3 && duplicate == 4,
           "dup must choose the lowest free descriptor");

    uint32_t original_flags = 0;
    uint32_t duplicate_flags = UINT32_MAX;
    expect(eos_fd_test_get_flags(original, &original_flags) == 0 &&
               eos_fd_test_get_flags(duplicate, &duplicate_flags) == 0,
           "descriptor flag query failed");
    expect(original_flags == EOS_FD_FLAG_CLOEXEC && duplicate_flags == 0,
           "dup must clear per-descriptor close-on-exec state");
    expect(eos_fd_test_set_cloexec(duplicate, 1) == 0,
           "setting close-on-exec on a duplicate failed");
    expect(eos_fd_test_get_flags(original, &original_flags) == 0 &&
               original_flags == EOS_FD_FLAG_CLOEXEC,
           "duplicate flag mutation must not alter the source descriptor");

    expect(eos_fd_test_close(original) == 0,
           "closing the source descriptor failed");
    expect(eos_fd_test_destructor_count(UINT64_C(10)) == 0,
           "a duplicate must keep the shared native object alive");
    expect(identity_of(duplicate, EOS_FD_KIND_FILE) == UINT64_C(10),
           "a duplicate must retain the shared native object");
    expect(eos_fd_test_close(duplicate) == 0,
           "closing the final duplicate failed");
    expect(eos_fd_test_destructor_count(UINT64_C(10)) == 1,
           "the shared native object must be destroyed exactly once");
}

void test_dup2_replacement_defers_old_object_destruction() {
    reset_fixture();
    int32_t source =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(20), 0);
    int32_t target = eos_fd_test_allocate(
        EOS_FD_KIND_FILE, UINT64_C(21), EOS_FD_FLAG_CLOEXEC);
    eos_fd_reference old_target{};
    expect(eos_fd_test_acquire(target, EOS_FD_KIND_FILE, &old_target) == 0,
           "dup2 replacement fixture acquire failed");

    expect(eos_fd_test_dup2(source, target) == target,
           "dup2 must return the requested target descriptor");
    expect(identity_of(target, EOS_FD_KIND_FILE) == UINT64_C(20),
           "dup2 target must share the source native object");
    uint32_t flags = UINT32_MAX;
    expect(eos_fd_test_get_flags(target, &flags) == 0 && flags == 0,
           "dup2 must clear close-on-exec on the replacement descriptor");
    expect(eos_fd_test_destructor_count(UINT64_C(21)) == 0,
           "dup2 must defer destruction while an operation owns the old target");

    expect(eos_fd_test_release(&old_target) == 0,
           "old dup2 target reference release failed");
    expect(eos_fd_test_destructor_count(UINT64_C(21)) == 1,
           "old dup2 target must be destroyed after its final reference");
    expect(eos_fd_test_close(source) == 0,
           "dup2 source close failed");
    expect(eos_fd_test_destructor_count(UINT64_C(20)) == 0,
           "dup2 target must keep the shared source object alive");
    expect(eos_fd_test_set_cloexec(target, 1) == 0,
           "dup2 no-op flag fixture failed");
    expect(eos_fd_test_dup2(target, target) == target,
           "dup2(old, old) must be a validated no-op");
    expect(eos_fd_test_get_flags(target, &flags) == 0 &&
               flags == EOS_FD_FLAG_CLOEXEC,
           "dup2(old, old) must preserve descriptor flags");
    expect(eos_fd_test_close(target) == 0,
           "dup2 target close failed");
    expect(eos_fd_test_destructor_count(UINT64_C(20)) == 1,
           "dup2 shared source object must be destroyed exactly once");
}

void test_kind_generation_and_double_release_validation() {
    reset_fixture();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(30), 0);
    eos_fd_token stale_token{};
    eos_fd_reference old_reference{};
    eos_fd_reference rejected{};
    expect(eos_fd_test_get_token(descriptor, &stale_token) == 0,
           "generation token capture failed");
    expect(eos_fd_test_acquire(descriptor, EOS_FD_KIND_SOCKET, &rejected) == -1,
           "kind mismatch must be rejected");
    expect(*eos_rust_errno_location() == 9,
           "kind mismatch must report EOS EBADF (9)");
    expect(eos_fd_test_acquire(descriptor,
                               EOS_FD_KIND_FILE,
                               &old_reference) == 0,
           "old generation reference acquire failed");
    expect(eos_fd_test_close(descriptor) == 0,
           "old generation close failed");

    int32_t replacement =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(31), 0);
    expect(replacement == descriptor,
           "closed descriptor must be reusable while an old operation completes");
    expect(eos_fd_test_acquire_token(stale_token,
                                     EOS_FD_KIND_FILE,
                                     &rejected) == -1,
           "a stale descriptor generation must not acquire the replacement");
    expect(*eos_rust_errno_location() == 9,
           "stale generation must report EOS EBADF (9)");
    expect(eos_fd_test_reference_identity(&old_reference, nullptr) == -1,
           "reference identity query must reject a null output");
    uint64_t old_identity = 0;
    expect(eos_fd_test_reference_identity(&old_reference, &old_identity) == 0 &&
               old_identity == UINT64_C(30),
           "an acquired operation must retain the old object after slot reuse");
    expect(eos_fd_test_release(&old_reference) == 0,
           "old generation release failed");
    expect(eos_fd_test_destructor_count(UINT64_C(30)) == 1,
           "old generation object must be destroyed after its final reference");
    expect(eos_fd_test_release(&old_reference) == -1,
           "double release of a stale reference must fail safely");
    expect(identity_of(replacement, EOS_FD_KIND_FILE) == UINT64_C(31),
           "stale release must never affect the replacement object");
    expect(eos_fd_test_close(replacement) == 0,
           "replacement close failed");
    expect(eos_fd_test_close(replacement) == -1,
           "double close must be rejected");
    expect(*eos_rust_errno_location() == 9,
           "double close must report EOS EBADF (9)");
    expect(eos_fd_test_destructor_count(UINT64_C(31)) == 1,
           "replacement object must be destroyed exactly once");
}

void test_copied_reference_release_is_consumed_once() {
    reset_fixture();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(40), 0);
    eos_fd_reference original{};
    expect(eos_fd_test_acquire(descriptor,
                               EOS_FD_KIND_FILE,
                               &original) == 0,
           "copied-reference fixture acquire failed");
    eos_fd_reference copied = original;
    expect(eos_fd_test_release(&original) == 0,
           "first release of an acquired reference failed");
    expect(eos_fd_test_release(&copied) == -1,
           "a copied reference must not release one acquisition twice");
    expect(*eos_rust_errno_location() == 9,
           "a copied release must report EOS EBADF (9)");
    expect(identity_of(descriptor, EOS_FD_KIND_FILE) == UINT64_C(40),
           "a copied release must not invalidate the open descriptor");
    expect(eos_fd_test_destructor_count(UINT64_C(40)) == 0,
           "a copied release must not destroy an object owned by a descriptor");
    uint64_t consumed_identity = 0;
    expect(eos_fd_test_reference_identity(&copied, &consumed_identity) == -1,
           "a consumed copied reference must not remain usable");

    eos_fd_reference replacement{};
    expect(eos_fd_test_acquire(descriptor,
                               EOS_FD_KIND_FILE,
                               &replacement) == 0,
           "lease reuse fixture acquire failed");
    expect(replacement.lease_id != copied.lease_id,
           "a later acquisition must receive a distinct lease identity");
    expect(eos_fd_test_release(&copied) == -1,
           "a stale lease identity must not release a later acquisition");
    expect(eos_fd_test_release(&replacement) == 0,
           "later acquisition release failed after stale-release rejection");
    expect(eos_fd_test_close(descriptor) == 0,
           "copied-reference fixture close failed");
    expect(eos_fd_test_destructor_count(UINT64_C(40)) == 1,
           "copied-reference object must be destroyed exactly once");
}

void test_lease_id_exhaustion_never_wraps_or_aliases() {
    reset_fixture();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(41), 0);
    eos_fd_test_exhaust_lease_ids_after_next_acquire();
    eos_fd_reference last{};
    eos_fd_reference rejected{};
    expect(eos_fd_test_acquire(descriptor, EOS_FD_KIND_FILE, &last) == 0 &&
               last.lease_id == UINT64_MAX,
           "the final unique lease identity must remain usable");
    expect(eos_fd_test_acquire(descriptor, EOS_FD_KIND_FILE, &rejected) == -1,
           "lease identities must never wrap after UINT64_MAX");
    expect(*eos_rust_errno_location() == 35,
           "lease-id exhaustion must report EOS EAGAIN (35)");
    expect(eos_fd_test_release(&last) == 0,
           "final unique lease release failed");
    uint32_t flags = UINT32_MAX;
    expect(eos_fd_test_get_flags(descriptor, &flags) == 0 && flags == 0,
           "lease-id exhaustion must not invalidate the open descriptor");
    expect(eos_fd_test_close(descriptor) == 0,
           "lease-id exhaustion fixture close failed");
}

void test_active_leases_are_heap_limited_not_fd_limited() {
    reset_fixture();
    constexpr std::size_t lease_count = 2048;
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(44), 0);
    std::vector<eos_fd_reference> leases(lease_count);
    for (eos_fd_reference &lease : leases) {
        expect(eos_fd_test_acquire(descriptor,
                                   EOS_FD_KIND_FILE,
                                   &lease) == 0,
               "active lease allocation stopped at an fd-derived limit");
    }
    expect(eos_fd_test_close(descriptor) == 0,
           "heap-limited lease fixture close failed");
    expect(eos_fd_test_destructor_count(UINT64_C(44)) == 0,
           "active heap leases must retain the closed object");
    for (eos_fd_reference &lease : leases) {
        expect(eos_fd_test_release(&lease) == 0,
               "heap-limited lease release failed");
    }
    expect(eos_fd_test_destructor_count(UINT64_C(44)) == 1,
           "heap-limited lease object must be destroyed exactly once");
}

void test_lease_allocation_failure_preserves_descriptor_ownership() {
    reset_fixture();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(45), 0);
    eos_fd_reference rejected{};
    eos_host_test_fail_next_alloc(INT32_C(15));
    expect(eos_fd_test_acquire(descriptor,
                               EOS_FD_KIND_FILE,
                               &rejected) == -1,
           "lease allocation failure must reject acquisition");
    expect(*eos_rust_errno_location() == 12,
           "lease allocation failure must report EOS ENOMEM (12)");
    expect(identity_of(descriptor, EOS_FD_KIND_FILE) == UINT64_C(45),
           "lease allocation failure must preserve descriptor ownership");
    expect(eos_fd_test_close(descriptor) == 0,
           "lease allocation failure fixture close failed");
}

void test_destruction_has_no_fallible_lock_reacquisition() {
    reset_fixture();
    int32_t descriptor =
        eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(42), 0);
    eos_host_test_fail_lock_after(UINT32_C(1), INT32_C(17));
    expect(eos_fd_test_close(descriptor) == 0,
           "close must finish bookkeeping before irreversible destruction");
    expect(eos_fd_test_destructor_count(UINT64_C(42)) == 1,
           "no-reacquire close must destroy the object exactly once");
    expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(43), 0) == -1,
           "the delayed lock fault must remain for the operation after close");
    expect(*eos_rust_errno_location() == 16,
           "the delayed lock fault must report EOS EBUSY (16)");
}

void test_runtime_init_failure_diagnoses_and_aborts() {
    reset_fixture();
    int diagnostic_pipe[2] = {-1, -1};
    expect(pipe(diagnostic_pipe) == 0,
           "runtime-init diagnostic pipe creation failed");
    eos_host_test_fail_console(UINT32_C(1), INT32_C(15));
    pid_t child = fork();
    expect(child >= 0, "runtime-init failure fork failed");
    if (child == 0) {
        (void)close(diagnostic_pipe[0]);
        if (dup2(diagnostic_pipe[1], STDERR_FILENO) < 0) {
            _exit(91);
        }
        (void)close(diagnostic_pipe[1]);
        const char *arguments[] = {"fd-init-failure", nullptr};
        eos_rust_runtime_init(INT32_C(1), arguments);
        _exit(92);
    }

    (void)close(diagnostic_pipe[1]);
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "runtime-init failure child wait failed");
    std::string diagnostic;
    char buffer[256];
    ssize_t length;
    while ((length = read(diagnostic_pipe[0], buffer, sizeof(buffer))) > 0) {
        diagnostic.append(buffer, (std::size_t)length);
    }
    (void)close(diagnostic_pipe[0]);
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "failed standard stream initialization must abort");
    expect(diagnostic.find(
               "EOS Rust runtime initialization failed: standard descriptor 1") !=
               std::string::npos,
           "failed runtime init must emit a direct diagnostic naming the stream");
}

void test_descriptor_lock_failures_do_not_report_false_success() {
    reset_fixture();
    eos_host_test_fail_next_lock(INT32_C(17));
    expect(eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(500), 0) == -1,
           "descriptor lock acquisition failure must reject allocation");
    expect(*eos_rust_errno_location() == 16,
           "descriptor lock acquisition failure must report EOS EBUSY (16)");

    pid_t child = fork();
    expect(child >= 0, "descriptor unlock-failure fork failed");
    if (child == 0) {
        eos_host_test_fail_next_unlock(INT32_C(17));
        (void)eos_fd_test_allocate(EOS_FD_KIND_FILE, UINT64_C(501), 0);
        _exit(93);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "descriptor unlock-failure child wait failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "descriptor unlock failure must abort instead of reporting success");
}

} // namespace

int main() {
    test_standard_descriptors_are_reserved_and_initialized_once();
    test_runtime_init_is_concurrency_safe();
    test_allocation_reuse_and_exhaustion();
    test_duplication_shares_ownership_and_not_descriptor_flags();
    test_dup2_replacement_defers_old_object_destruction();
    test_kind_generation_and_double_release_validation();
    test_copied_reference_release_is_consumed_once();
    test_lease_id_exhaustion_never_wraps_or_aliases();
    test_active_leases_are_heap_limited_not_fd_limited();
    test_lease_allocation_failure_preserves_descriptor_ownership();
    test_destruction_has_no_fallible_lock_reacquisition();
    test_runtime_init_failure_diagnoses_and_aborts();
    test_descriptor_lock_failures_do_not_report_false_success();
    return EXIT_SUCCESS;
}
