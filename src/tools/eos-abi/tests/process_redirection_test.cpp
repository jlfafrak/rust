#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <thread>

#ifndef EOS_RUST_PROCESS_API_DEFINED
extern "C" {
typedef uint32_t eos_rust_process_t;
typedef struct eos_rust_spawn_request {
    const char *program; const char *const *argv; uint32_t argc;
    const char *const *envp; uint32_t envc; const char *cwd;
    eos_rust_fd_t stdin_fd; eos_rust_fd_t stdout_fd; eos_rust_fd_t stderr_fd;
    uint32_t flags; uint32_t reserved[7];
} eos_rust_spawn_request;
typedef struct eos_rust_process_status {
    uint32_t kind; int32_t code; uint32_t reserved[6];
} eos_rust_process_status;
int32_t eos_rust_spawn(const eos_rust_spawn_request *, eos_rust_process_t *);
int32_t eos_rust_process_wait(eos_rust_process_t, eos_rust_process_status *);
int32_t eos_rust_process_close(eos_rust_process_t);
}
#endif

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_console_input(const char *);
uint32_t eos_host_test_console_output(uint32_t, char *, uint32_t);
uint32_t eos_host_test_process_inherited_count(void);
int32_t eos_host_test_process_inherited_fd(uint32_t);
uint32_t eos_host_test_process_started(void);
void eos_host_test_process_release(void);
uint32_t eos_process_test_live_records(void);
void eos_host_test_use_martos_process_capabilities(void);
}

namespace {
[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message); std::exit(1);
}
void expect(bool value, const char *message) { if (!value) fail(message); }

void reset_fixture() {
    eos_fd_test_reset(); eos_host_test_reset();
    const char *argv[] = {"process-redirection-test", nullptr};
    eos_rust_runtime_init(1, argv);
}
eos_rust_spawn_request request(const char *program) {
    eos_rust_spawn_request result{}; result.program = program;
    result.stdin_fd = result.stdout_fd = result.stderr_fd = -1; return result;
}
void finish(eos_rust_spawn_request &req) {
    eos_rust_process_t process = 0; eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0, "redirected spawn failed");
    expect(eos_rust_process_wait(process, &status) == 0 && status.code == 0,
           "redirected child failed");
    expect(eos_rust_process_close(process) == 0, "redirected close failed");
}
void wait_started() {
    for (uint32_t spin = 0; spin != UINT32_C(1000000); ++spin) {
        if (eos_host_test_process_started() != 0) return;
        std::this_thread::yield();
    }
    fail("redirected blocked process did not start");
}
void wait_no_records() {
    for (uint32_t spin = 0; spin != UINT32_C(1000000); ++spin) {
        if (eos_process_test_live_records() == 0) return;
        std::this_thread::yield();
    }
    fail("redirected rollback record did not reap");
}

void test_inherited_and_null_stdio() {
    reset_fixture();
    eos_host_test_console_input("abc");
    auto req = request("/host/copy3");
    finish(req);
    char output[8]{};
    expect(eos_host_test_console_output(1, output, sizeof(output)) == 3 &&
               std::memcmp(output, "abc", 3) == 0,
           "inherited stdin/stdout did not bridge bytes");

    eos_rust_fd_t null_in = eos_rust_open("/dev/null", EOS_RUST_O_RDONLY, 0);
    eos_rust_fd_t null_out = eos_rust_open("/dev/null", EOS_RUST_O_WRONLY, 0);
    expect(null_in >= 3 && null_out >= 3, "null descriptors failed");
    req = request("/host/copy3"); req.stdin_fd = null_in; req.stdout_fd = null_out;
    finish(req);
    req = request("/host/stderr"); req.stderr_fd = null_out; finish(req);
    expect(eos_rust_close(null_in) == 0 && eos_rust_close(null_out) == 0,
           "null descriptor cleanup failed");
}

void test_pipe_redirection_cloexec_and_pins() {
    reset_fixture();
    eos_rust_fd_t input[2]{-1, -1}; eos_rust_fd_t output[2]{-1, -1};
    expect(eos_rust_pipe(input, EOS_RUST_O_CLOEXEC) == 0 &&
               eos_rust_pipe(output, EOS_RUST_O_CLOEXEC) == 0,
           "redirect pipes failed");
    expect(eos_rust_write(input[1], "xyz", 3) == 3, "pipe fixture write failed");
    auto req = request("/host/block-copy3"); req.stdin_fd = input[0]; req.stdout_fd = output[1];
    eos_rust_process_t process = 0; eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0,
           "explicit CLOEXEC descriptors must remain valid stdio");
    wait_started();
    expect(eos_rust_close(input[0]) == 0 && eos_rust_close(output[1]) == 0,
           "parent close of pinned stdio failed");
    eos_rust_fd_t replacement[2]{-1, -1};
    expect(eos_rust_pipe(replacement, EOS_RUST_O_CLOEXEC) == 0 &&
               replacement[0] == input[0],
           "closed parent descriptor was not reused for ABA coverage");
    expect(eos_rust_write(replacement[1], "bad", 3) == 3,
           "replacement descriptor fixture write failed");
    eos_host_test_process_release();
    expect(eos_rust_process_wait(process, &status) == 0 &&
               eos_rust_process_close(process) == 0,
           "pinned redirected process failed");
    char bytes[4]{};
    expect(eos_rust_read(output[0], bytes, 3) == 3 &&
               std::memcmp(bytes, "xyz", 3) == 0,
           "child pins did not survive parent close/reuse");
    expect(eos_rust_close(input[1]) == 0 && eos_rust_close(output[0]) == 0,
           "pipe redirection cleanup failed");
    expect(eos_rust_close(replacement[0]) == 0 &&
               eos_rust_close(replacement[1]) == 0,
           "replacement descriptor cleanup failed");
}

void test_loader_rollback_releases_pipe_pins() {
    reset_fixture();
    eos_rust_fd_t output[2]{-1, -1};
    expect(eos_rust_pipe(output, EOS_RUST_O_NONBLOCK |
                                    EOS_RUST_O_CLOEXEC) == 0,
           "loader rollback pipe creation failed");
    auto req = request("/host/missing");
    req.stdout_fd = output[1];
    eos_rust_process_t process = 99;
    expect(eos_rust_spawn(&req, &process) == -1 && process == 0,
           "loader rollback fixture unexpectedly spawned");
    expect(eos_rust_close(output[1]) == 0,
           "loader rollback parent writer close failed");
    wait_no_records();
    char byte = 0;
    expect(eos_rust_read(output[0], &byte, 1) == 0,
           "loader failure retained the child pipe writer pin");
    expect(eos_rust_close(output[0]) == 0,
           "loader rollback reader cleanup failed");
}

void test_redirection_failures_surface_from_wait() {
    reset_fixture();
    eos_rust_fd_t input[2]{-1, -1};
    expect(eos_rust_pipe(input, EOS_RUST_O_CLOEXEC) == 0,
           "read-failure pipe creation failed");
    auto req = request("/host/copy3");
    req.stdin_fd = input[1];
    eos_rust_process_t process = 0;
    eos_rust_process_status status{};
    expect(eos_rust_spawn(&req, &process) == 0 &&
               eos_rust_process_wait(process, &status) == -1 &&
               *eos_rust_errno_location() == 5 &&
               eos_rust_process_close(process) == 0,
           "redirected read failure did not surface from wait");
    expect(eos_rust_close(input[0]) == 0 && eos_rust_close(input[1]) == 0,
           "read-failure pipe cleanup failed");

    eos_rust_fd_t output[2]{-1, -1};
    expect(eos_rust_pipe(output, EOS_RUST_O_CLOEXEC) == 0,
           "write-failure pipe creation failed");
    req = request("/host/stdout");
    req.stdout_fd = output[0];
    expect(eos_rust_spawn(&req, &process) == 0 &&
               eos_rust_process_wait(process, &status) == -1 &&
               *eos_rust_errno_location() == 5 &&
               eos_rust_process_close(process) == 0,
           "redirected write failure did not surface from wait");
    expect(eos_rust_close(output[0]) == 0 && eos_rust_close(output[1]) == 0,
           "write-failure pipe cleanup failed");
}

void test_inheritance_filter_and_stderr_pipe() {
    reset_fixture();
    eos_rust_fd_t inherited[2]{-1, -1}; eos_rust_fd_t hidden[2]{-1, -1};
    eos_rust_fd_t errors[2]{-1, -1};
    expect(eos_rust_pipe(inherited, 0) == 0 &&
               eos_rust_pipe(hidden, EOS_RUST_O_CLOEXEC) == 0 &&
               eos_rust_pipe(errors, EOS_RUST_O_CLOEXEC) == 0,
           "inheritance fixture creation failed");
    auto req = request("/host/stderr"); req.stderr_fd = errors[1]; finish(req);
    char bytes[8]{};
    expect(eos_rust_read(errors[0], bytes, 3) == 3 &&
               std::memcmp(bytes, "err", 3) == 0,
           "stderr pipe redirection lost bytes");
    expect(eos_host_test_process_inherited_count() == 2 &&
               eos_host_test_process_inherited_fd(0) == inherited[0] &&
               eos_host_test_process_inherited_fd(1) == inherited[1],
           "CLOEXEC filtering or inherited descriptor snapshot is wrong");
    for (eos_rust_fd_t fd : {inherited[0], inherited[1], hidden[0], hidden[1],
                              errors[0], errors[1]}) {
        expect(eos_rust_close(fd) == 0, "inheritance fixture cleanup failed");
    }
}

void test_martos_inherited_stderr_requires_native_standard_error() {
    reset_fixture();
    eos_rust_fd_t saved_stderr = eos_rust_dup(2);
    eos_rust_fd_t errors[2]{-1, -1};
    expect(saved_stderr >= 3 &&
               eos_rust_pipe(errors, EOS_RUST_O_CLOEXEC) == 0,
           "MARTOS inherited-stderr fixture creation failed");
    expect(eos_rust_dup2(errors[1], 2) == 2,
           "MARTOS inherited-stderr fixture could not replace fd 2");
    eos_host_test_use_martos_process_capabilities();

    auto req = request("/host/exit0");
    eos_rust_process_t process = 0;
    const int32_t remapped_result = eos_rust_spawn(&req, &process);
    const int32_t remapped_errno = *eos_rust_errno_location();
    if (remapped_result == 0) {
        eos_rust_process_status status{};
        (void)eos_rust_process_wait(process, &status);
        (void)eos_rust_process_close(process);
    }

    expect(eos_rust_dup2(saved_stderr, 2) == 2 &&
               eos_rust_close(saved_stderr) == 0,
           "MARTOS inherited-stderr fixture did not restore fd 2");
    expect(remapped_result == -1 && remapped_errno == 45 && process == 0,
           "MARTOS must reject inherited stderr after compatibility fd 2 is remapped");

    process = 0;
    expect(eos_rust_spawn(&req, &process) == 0,
           "MARTOS must allow its original native/default inherited stderr");
    eos_rust_process_status status{};
    expect(eos_rust_process_wait(process, &status) == 0 &&
               eos_rust_process_close(process) == 0,
           "native/default inherited stderr process failed");
    expect(eos_rust_close(errors[0]) == 0 && eos_rust_close(errors[1]) == 0,
           "MARTOS inherited-stderr fixture cleanup failed");
}
} // namespace

int main() {
    test_inherited_and_null_stdio();
    test_pipe_redirection_cloexec_and_pins();
    test_loader_rollback_releases_pipe_pins();
    test_redirection_failures_surface_from_wait();
    test_inheritance_filter_and_stderr_pipe();
    test_martos_inherited_stderr_requires_native_standard_error();
    return 0;
}
