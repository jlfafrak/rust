#include "eos_rust_abi.h"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_free(int32_t status);
void eos_host_test_fail_next_tls_get(int32_t status);
void eos_host_test_fail_next_tls_set(int32_t status);
}

namespace {

int destructor_calls;

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

void count_destructor(void *value) {
    if (value == reinterpret_cast<void *>(uintptr_t{0x1234})) {
        ++destructor_calls;
    }
}

void expect_cleanup_abort(void (*inject_failure)(int32_t),
                          const char *message) {
    pid_t child = fork();
    expect(child >= 0, "runtime cleanup fork failed");
    if (child == 0) {
        eos_host_test_reset();
        *eos_rust_errno_location() = 17;
        inject_failure(INT32_C(25));
        eos_rust_runtime_cleanup();
        _exit(91);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "runtime cleanup child wait failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, message);
}

void test_runtime_cleanup_is_current_thread_only_and_idempotent() {
    eos_host_test_reset();
    destructor_calls = 0;
    eos_rust_tls_key_t key = 0;
    expect(eos_rust_pthread_key_create(&key, count_destructor) == 0,
           "runtime cleanup TLS key creation failed");
    expect(eos_rust_pthread_setspecific(
               key, reinterpret_cast<void *>(uintptr_t{0x1234})) == 0,
           "runtime cleanup TLS value installation failed");

    eos_rust_runtime_cleanup();
    expect(destructor_calls == 1,
           "runtime cleanup must run the calling thread TLS destructor once");
    eos_rust_runtime_cleanup();
    expect(destructor_calls == 1,
           "idempotent runtime cleanup must not rerun TLS destructors");
}

void test_runtime_cleanup_failures_abort() {
    expect_cleanup_abort(
        eos_host_test_fail_next_tls_get,
        "reserved TLS slot read failure must abort cleanup");
    expect_cleanup_abort(
        eos_host_test_fail_next_tls_set,
        "reserved TLS slot clear failure must abort cleanup");
    expect_cleanup_abort(
        eos_host_test_fail_next_free,
        "TLS root release failure must abort cleanup");
}

void test_cpu_count_matches_supported_zynq_processors() {
    expect(eos_rust_cpu_count() == UINT32_C(2),
           "EOS available processor count must be exactly two");
}

} // namespace

int main() {
    test_runtime_cleanup_is_current_thread_only_and_idempotent();
    test_runtime_cleanup_failures_abort();
    test_cpu_count_matches_supported_zynq_processors();
    return EXIT_SUCCESS;
}
