#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#ifndef EOS_RUST_TSAN_TEST
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef EOS_RUST_PROCESS_API_DEFINED
extern "C" {
typedef uint32_t eos_rust_process_t;
typedef struct eos_rust_spawn_request {
    const char *program;
    const char *const *argv;
    uint32_t argc;
    const char *const *envp;
    uint32_t envc;
    const char *cwd;
    eos_rust_fd_t stdin_fd;
    eos_rust_fd_t stdout_fd;
    eos_rust_fd_t stderr_fd;
    uint32_t flags;
    uint32_t reserved[7];
} eos_rust_spawn_request;
typedef struct eos_rust_process_status {
    uint32_t kind;
    int32_t code;
    uint32_t reserved[6];
} eos_rust_process_status;
int32_t eos_rust_spawn(const eos_rust_spawn_request *, eos_rust_process_t *);
int32_t eos_rust_process_wait(eos_rust_process_t, eos_rust_process_status *);
int32_t eos_rust_process_try_wait(eos_rust_process_t,
                                  eos_rust_process_status *);
int32_t eos_rust_process_kill(eos_rust_process_t);
int32_t eos_rust_process_close(eos_rust_process_t);
}
#define EOS_RUST_PROCESS_EXITED UINT32_C(1)
#define EOS_RUST_PROCESS_TERMINATED UINT32_C(2)
#endif

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_alloc(int32_t);
void eos_host_test_fail_alloc_after(uint32_t, int32_t);
void eos_host_test_fail_next_sync_create(int32_t);
void eos_host_test_fail_next_process_kill(int32_t);
void eos_host_test_fail_process_kill_after_match(int32_t);
void eos_host_test_fail_next_process_unload(int32_t);
void eos_host_test_fail_next_sync_wait(int32_t);
void eos_host_test_fail_next_thread_create(int32_t);
void eos_host_test_process_release(void);
uint32_t eos_host_test_process_started(void);
uint32_t eos_host_test_process_active(void);
uint32_t eos_process_test_live_records(void);
uint32_t eos_host_test_process_argc(void);
uint32_t eos_host_test_process_envc(void);
uint32_t eos_host_test_process_inherited_count(void);
const char *eos_host_test_process_program(void);
const char *eos_host_test_process_argument(uint32_t);
const char *eos_host_test_process_environment(uint32_t);
const char *eos_host_test_process_cwd(void);
uint32_t eos_host_test_process_started_identity(uint32_t);
uint32_t eos_host_test_process_active_identity(uint32_t);
uint32_t eos_host_test_process_argc_identity(uint32_t);
uint32_t eos_host_test_process_envc_identity(uint32_t);
const char *eos_host_test_process_argument_identity(uint32_t, uint32_t);
const char *eos_host_test_process_environment_identity(uint32_t, uint32_t);
const char *eos_host_test_process_cwd_identity(uint32_t);
uint32_t eos_host_test_process_active_count(void);
uint32_t eos_host_test_process_record_count(void);
}

namespace {
constexpr int32_t kNoEntry = 2;
constexpr int32_t kNoProcess = 3;
constexpr int32_t kIo = 5;
constexpr int32_t kNoMemory = 12;
constexpr int32_t kFault = 14;
constexpr int32_t kInvalid = 22;

[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}
void expect(bool value, const char *message) { if (!value) fail(message); }

void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    const char *runtime_argv[] = {"process-test", nullptr};
    eos_rust_runtime_init(1, runtime_argv);
}

eos_rust_spawn_request request(const char *program) {
    eos_rust_spawn_request result{};
    result.program = program;
    result.stdin_fd = -1;
    result.stdout_fd = -1;
    result.stderr_fd = -1;
    return result;
}

void wait_started() {
    for (uint32_t spin = 0; spin != UINT32_C(1000000); ++spin) {
        if (eos_host_test_process_started() != 0) return;
        std::this_thread::yield();
    }
    fail("blocked host process did not start");
}

void wait_no_records() {
    for (uint32_t spin = 0; spin != UINT32_C(1000000); ++spin) {
        if (eos_process_test_live_records() == 0) return;
        std::this_thread::yield();
    }
    fail("closed process record did not self-reap");
}

void wait_started(eos_rust_process_t process) {
    for (uint32_t spin = 0; spin != UINT32_C(1000000); ++spin) {
        if (eos_host_test_process_started_identity(process) != 0) return;
        std::this_thread::yield();
    }
    fail("keyed blocked host process did not start");
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

void test_layout_validation_and_loader_failure() {
    reset_fixture();
    expect(sizeof(eos_rust_process_t) == 4, "process handle layout drifted");
    expect(sizeof(eos_rust_process_status) == 32 &&
               alignof(eos_rust_process_status) == 4,
           "process status layout drifted");
    expect(sizeof(eos_rust_spawn_request) == (sizeof(void *) == 8 ? 96 : 68),
           "spawn request layout drifted");
    eos_rust_process_t process = 99;
    auto valid = request("/host/exit0");
    expect(eos_rust_spawn(nullptr, &process) == -1 &&
               *eos_rust_errno_location() == kFault && process == 0,
           "spawn must validate the request pointer and clear output");
    expect(eos_rust_spawn(&valid, nullptr) == -1 &&
               *eos_rust_errno_location() == kFault,
           "spawn must validate the output pointer");
    valid.program = nullptr;
    expect(eos_rust_spawn(&valid, &process) == -1 &&
               *eos_rust_errno_location() == kFault,
           "spawn must reject a null program");
    valid = request("/host/exit0");
    valid.flags = 1;
    expect(eos_rust_spawn(&valid, &process) == -1 &&
               *eos_rust_errno_location() == kInvalid,
           "spawn flags must be zero in ABI v1");
    valid.flags = 0;
    valid.reserved[3] = 1;
    expect(eos_rust_spawn(&valid, &process) == -1 &&
               *eos_rust_errno_location() == kInvalid,
           "spawn reserved words must be zero");
    valid = request("/host/missing");
    expect(eos_rust_spawn(&valid, &process) == -1 &&
               *eos_rust_errno_location() == kNoEntry && process == 0,
           "loader failure must be synchronous and mapped");
    wait_no_records();
}

void test_argument_environment_cwd_and_status() {
    reset_fixture();
    const char *argv[] = {"custom-zero", "alpha", "beta"};
    const char *envp[] = {"A=one", "B=two"};
    auto req = request("/host/capture");
    req.argv = argv;
    req.argc = 3;
    req.envp = envp;
    req.envc = 2;
    req.cwd = "/work/tree";
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0 && process != 0,
           "capture spawn failed");
    expect(eos_rust_process_wait(process, &status) == 0 &&
               status.kind == EOS_RUST_PROCESS_EXITED && status.code == 23,
           "nonzero child exit was not preserved");
    expect(std::strcmp(eos_host_test_process_program(), "/host/capture") == 0 &&
               eos_host_test_process_argc() == 3 &&
               std::strcmp(eos_host_test_process_argument(0), "custom-zero") == 0 &&
               std::strcmp(eos_host_test_process_argument(2), "beta") == 0 &&
               eos_host_test_process_envc() == 2 &&
               std::strcmp(eos_host_test_process_environment(1), "B=two") == 0 &&
               std::strcmp(eos_host_test_process_cwd(), "/work/tree") == 0,
           "spawn did not preserve copied arguments/environment/cwd");
    eos_rust_process_status repeated{};
    expect(eos_rust_process_wait(process, &repeated) == 0 &&
               std::memcmp(&status, &repeated, sizeof(status)) == 0,
           "wait must be repeatable and return the cached status");
    expect(eos_rust_process_kill(process) == 0,
           "kill after completion must be an idempotent success");
    expect(eos_rust_process_close(process) == 0,
           "process close failed");
    expect(eos_rust_process_close(process) == -1 &&
               *eos_rust_errno_location() == kNoProcess,
           "repeated process close must report a stale identity");
    expect(eos_rust_process_wait(process, &status) == -1 &&
               *eos_rust_errno_location() == kNoProcess,
           "closed handles must become stale");
}

void test_try_wait_kill_close_reuse_and_concurrency() {
    reset_fixture();
    auto req = request("/host/block");
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0, "blocking spawn failed");
    wait_started();
    expect(eos_rust_process_try_wait(process, &status) == 0,
           "try_wait must report a running child without blocking");
    std::atomic<int32_t> waiter{-99};
    std::thread wait_thread([&] { waiter = eos_rust_process_wait(process, &status); });
    expect(eos_rust_process_kill(process) == 0, "kill failed");
    wait_thread.join();
    expect(waiter == 0 && status.kind == EOS_RUST_PROCESS_TERMINATED &&
               status.code == 1,
           "killed child must report the stable terminated status");
    expect(eos_host_test_process_active() == 0,
           "kill did not stop the host process");
    const eos_rust_process_t stale = process;
    expect(eos_rust_process_close(process) == 0, "killed close failed");
    req = request("/host/exit0");
    expect(eos_rust_spawn(&req, &process) == 0 && process != stale,
           "process identities must not be reused");
    expect(eos_rust_process_wait(process, &status) == 0 && status.code == 0 &&
               eos_rust_process_close(process) == 0,
           "post-reuse process failed");

    req = request("/host/block");
    expect(eos_rust_spawn(&req, &process) == 0, "close-running spawn failed");
    wait_started();
    expect(eos_rust_process_try_wait(process, &status) == 0,
           "a reused block fixture must not inherit the prior release state");
    std::vector<std::thread> racers;
    std::atomic<uint32_t> success{0};
    for (int i = 0; i != 4; ++i) {
        racers.emplace_back([&] {
            eos_rust_process_status concurrent{};
            if (eos_rust_process_wait(process, &concurrent) == 0 &&
                concurrent.kind == EOS_RUST_PROCESS_EXITED) ++success;
        });
    }
    eos_host_test_process_release();
    for (auto &thread : racers) thread.join();
    expect(success == 4, "concurrent waiters did not share completion safely");
    expect(eos_rust_process_close(process) == 0, "concurrent process close failed");

    req = request("/host/block");
    expect(eos_rust_spawn(&req, &process) == 0, "self-reap spawn failed");
    wait_started();
    expect(eos_rust_process_close(process) == 0,
           "close of a running child failed");
    expect(eos_rust_process_kill(process) == -1 &&
               *eos_rust_errno_location() == kNoProcess,
           "close must invalidate a running process immediately");
    eos_host_test_process_release();
    wait_no_records();
}

void test_run_error_and_fault_rollback() {
    reset_fixture();
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    auto req = request("/host/run-error");
    expect(eos_rust_spawn(&req, &process) == 0 &&
               eos_rust_process_wait(process, &status) == -1 &&
               *eos_rust_errno_location() == kIo,
           "child infrastructure failure must surface from wait");
    expect(eos_rust_process_close(process) == 0, "run-error close failed");

    eos_host_test_fail_next_alloc(15);
    req = request("/host/exit0");
    expect(eos_rust_spawn(&req, &process) == -1 &&
               *eos_rust_errno_location() == kNoMemory && process == 0,
           "record allocation failure was not rolled back");
    eos_host_test_fail_next_sync_create(25);
    expect(eos_rust_spawn(&req, &process) == -1 &&
               *eos_rust_errno_location() == kIo && process == 0,
           "completion sync creation failure was not rolled back");
    const char *argv[] = {"zero", "one"};
    req.argv = argv;
    req.argc = 2;
    eos_host_test_fail_alloc_after(4, 15);
    expect(eos_rust_spawn(&req, &process) == -1 &&
               *eos_rust_errno_location() == kNoMemory && process == 0,
           "partial request-copy failure was not rolled back");
    req = request("/host/exit0");
    eos_host_test_fail_next_thread_create(25);
    expect(eos_rust_spawn(&req, &process) == -1 &&
               *eos_rust_errno_location() == kIo && process == 0,
           "worker creation failure was not rolled back");
    eos_host_test_fail_next_sync_wait(25);
    expect(eos_rust_spawn(&req, &process) == -1 &&
               *eos_rust_errno_location() == kIo && process == 0,
           "loader wait failure was not rolled back");
    wait_no_records();
}

void test_kill_failure_leaves_child_waitable() {
    reset_fixture();
    auto req = request("/host/block");
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0,
           "kill-failure process spawn failed");
    wait_started();
    eos_host_test_fail_next_process_kill(25);
    expect(eos_rust_process_kill(process) == -1 &&
               *eos_rust_errno_location() == kIo,
           "native kill failure must map without consuming the handle");
    expect(eos_rust_process_try_wait(process, &status) == 0,
           "failed kill must leave the child running and waitable");
    eos_host_test_process_release();
    expect(eos_rust_process_wait(process, &status) == 0 &&
               status.kind == EOS_RUST_PROCESS_EXITED &&
               eos_rust_process_close(process) == 0,
           "child did not recover after a failed kill");
}

void test_partial_kill_failure_preserves_termination() {
    reset_fixture();
    auto req = request("/host/block");
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0,
           "partial-kill process spawn failed");
    wait_started();
    eos_host_test_fail_process_kill_after_match(25);
    expect(eos_rust_process_kill(process) == -1 &&
               *eos_rust_errno_location() == kIo,
           "partial kill must report its later native operation error");
    expect(eos_rust_process_wait(process, &status) == 0 &&
               status.kind == EOS_RUST_PROCESS_TERMINATED &&
               status.code == 1,
           "wait lost a delivered termination after a later kill error");
    expect(eos_rust_process_close(process) == 0,
           "partial-kill process close failed");
}

void test_two_live_processes_are_isolated() {
    reset_fixture();
    eos_rust_fd_t input_one[2]{-1, -1};
    eos_rust_fd_t output_one[2]{-1, -1};
    eos_rust_fd_t input_two[2]{-1, -1};
    eos_rust_fd_t output_two[2]{-1, -1};
    expect(eos_rust_pipe(input_one, EOS_RUST_O_CLOEXEC) == 0 &&
               eos_rust_pipe(output_one, EOS_RUST_O_CLOEXEC) == 0 &&
               eos_rust_pipe(input_two, EOS_RUST_O_CLOEXEC) == 0 &&
               eos_rust_pipe(output_two, EOS_RUST_O_CLOEXEC) == 0,
           "multi-live stdio fixture creation failed");
    expect(eos_rust_write(input_one[1], "one", 3) == 3 &&
               eos_rust_write(input_two[1], "two", 3) == 3,
           "multi-live stdin fixture write failed");

    const char *argv_one[] = {"child-one", "alpha"};
    const char *env_one[] = {"CHILD=one"};
    auto request_one = request("/host/block-copy3");
    request_one.argv = argv_one;
    request_one.argc = 2;
    request_one.envp = env_one;
    request_one.envc = 1;
    request_one.cwd = "/work/one";
    request_one.stdin_fd = input_one[0];
    request_one.stdout_fd = output_one[1];

    const char *argv_two[] = {"child-two", "beta"};
    const char *env_two[] = {"CHILD=two"};
    auto request_two = request("/host/block-copy3");
    request_two.argv = argv_two;
    request_two.argc = 2;
    request_two.envp = env_two;
    request_two.envc = 1;
    request_two.cwd = "/work/two";
    request_two.stdin_fd = input_two[0];
    request_two.stdout_fd = output_two[1];

    eos_rust_process_t process_one = 0;
    eos_rust_process_t process_two = 0;
    eos_rust_process_status status_one{};
    eos_rust_process_status status_two{};
    expect(eos_rust_spawn(&request_one, &process_one) == 0,
           "first multi-live spawn failed");
    wait_started(process_one);
    expect(eos_rust_spawn(&request_two, &process_two) == 0,
           "second multi-live spawn failed");
    wait_started(process_two);

    expect(eos_host_test_process_started_identity(process_one) != 0 &&
               eos_host_test_process_started_identity(process_two) != 0 &&
               eos_host_test_process_active_count() == 2 &&
               eos_host_test_process_record_count() == 2,
           "second live process clobbered the first host adapter record");
    expect(eos_host_test_process_argc_identity(process_one) == 2 &&
               eos_host_test_process_envc_identity(process_one) == 1 &&
               std::strcmp(eos_host_test_process_argument_identity(
                               process_one, 0),
                           "child-one") == 0 &&
               std::strcmp(eos_host_test_process_environment_identity(
                               process_one, 0),
                           "CHILD=one") == 0 &&
               std::strcmp(eos_host_test_process_cwd_identity(process_one),
                           "/work/one") == 0 &&
               eos_host_test_process_argc_identity(process_two) == 2 &&
               eos_host_test_process_envc_identity(process_two) == 1 &&
               std::strcmp(eos_host_test_process_argument_identity(
                               process_two, 0),
                           "child-two") == 0 &&
               std::strcmp(eos_host_test_process_environment_identity(
                               process_two, 0),
                           "CHILD=two") == 0 &&
               std::strcmp(eos_host_test_process_cwd_identity(process_two),
                           "/work/two") == 0,
           "multi-live argv/env/cwd observations were not isolated");

    expect(eos_rust_process_kill(process_one) == 0,
           "first multi-live kill failed");
    expect(eos_rust_process_try_wait(process_two, &status_two) == 0 &&
               eos_host_test_process_active_identity(process_two) != 0,
           "killing the first process disturbed the second");
    expect(eos_rust_process_wait(process_one, &status_one) == 0 &&
               status_one.kind == EOS_RUST_PROCESS_TERMINATED &&
               eos_rust_process_close(process_one) == 0,
           "first multi-live wait/close failed");
    expect(eos_rust_process_kill(process_two) == 0 &&
               eos_rust_process_wait(process_two, &status_two) == 0 &&
               status_two.kind == EOS_RUST_PROCESS_TERMINATED &&
               eos_rust_process_close(process_two) == 0,
           "second multi-live kill/wait/close failed");

    char first[4]{};
    char second[4]{};
    expect(eos_rust_read(output_one[0], first, 3) == 3 &&
               std::memcmp(first, "one", 3) == 0 &&
               eos_rust_read(output_two[0], second, 3) == 3 &&
               std::memcmp(second, "two", 3) == 0,
           "multi-live stdio contexts were not isolated");
    for (eos_rust_fd_t descriptor :
         {input_one[0], input_one[1], output_one[0], output_one[1],
          input_two[0], input_two[1], output_two[0], output_two[1]}) {
        expect(eos_rust_close(descriptor) == 0,
               "multi-live stdio fixture cleanup failed");
    }
    wait_no_records();
    expect(eos_host_test_process_active_count() == 0 &&
               eos_host_test_process_record_count() == 0,
           "multi-live process adapter records did not clean up");
}

void test_unload_failure_surfaces_from_wait() {
#ifndef EOS_RUST_TSAN_TEST
    reset_fixture();
    expect_abort([] {
        auto req = request("/host/exit0");
        eos_rust_process_t process = 0;
        eos_rust_process_status status{};
        eos_host_test_fail_next_process_unload(25);
        if (eos_rust_spawn(&req, &process) == 0) {
            (void)eos_rust_process_wait(process, &status);
        }
    }, "unload cleanup failure must fail fast");
#endif
}
} // namespace

int main() {
    test_layout_validation_and_loader_failure();
    test_argument_environment_cwd_and_status();
    test_try_wait_kill_close_reuse_and_concurrency();
    test_run_error_and_fault_rollback();
    test_kill_failure_leaves_child_waitable();
    test_partial_kill_failure_preserves_termination();
    test_two_live_processes_are_isolated();
    test_unload_failure_surfaces_from_wait();
    return 0;
}
