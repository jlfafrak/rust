#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_fs_test_reset(void);
void eos_host_test_console_input(const char *text);
uint32_t eos_host_test_console_output(uint32_t stream, char *buffer,
                                      uint32_t capacity);
void eos_host_test_console_partial(uint32_t stream, uint32_t bytes,
                                   int32_t status);
void eos_host_test_hostname(const char *name);
void eos_host_test_fail_next_sync_wait(int32_t status);
void eos_host_test_fail_next_alloc(int32_t status);
}

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool value, const char *message) {
    if (!value) fail(message);
}

void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    eos_fs_test_reset();
    const char *argv[] = {"pipe-stdio-test", nullptr};
    eos_rust_runtime_init(1, argv);
}

void test_standard_streams_and_hostname() {
    expect(eos_rust_isatty(0) == 1 && eos_rust_isatty(1) == 1 &&
               eos_rust_isatty(2) == 1,
           "all established console descriptors must be terminals");
    eos_host_test_console_input("input");
    char input[8]{};
    expect(eos_rust_read(0, input, 5) == 5 &&
               std::memcmp(input, "input", 5) == 0,
           "standard input must route through the console byte service");
    expect(eos_rust_read(0, input, 1) == 0,
           "host console input exhaustion must report EOF");
    expect(eos_rust_read(1, input, 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "standard output must reject reads");
    expect(eos_rust_write(0, "x", 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "standard input must reject writes");

    eos_host_test_console_partial(1, 2, 25);
    expect(eos_rust_write(1, "hello", 5) == 2,
           "console partial progress must outrank a later error");
    expect(eos_rust_write(2, "err", 3) == 3,
           "standard error must use its distinct console route");
    char output[16]{};
    expect(eos_host_test_console_output(1, output, sizeof(output)) == 2 &&
               std::memcmp(output, "he", 2) == 0,
           "standard output route captured the wrong bytes");
    std::memset(output, 0, sizeof(output));
    expect(eos_host_test_console_output(2, output, sizeof(output)) == 3 &&
               std::memcmp(output, "err", 3) == 0,
           "standard error route captured the wrong bytes");

    eos_host_test_hostname("eos-zynq");
    char hostname[16]{};
    expect(eos_rust_gethostname(hostname, sizeof(hostname)) == 0 &&
               std::strcmp(hostname, "eos-zynq") == 0,
           "hostname must be copied and terminated");
    char truncated[5] = {};
    expect(eos_rust_gethostname(truncated, sizeof(truncated)) == -1 &&
               *eos_rust_errno_location() == 63 &&
               std::strcmp(truncated, "eos-") == 0,
           "hostname truncation must terminate and report ENAMETOOLONG");
    expect(eos_rust_gethostname(nullptr, 4) == -1 &&
               *eos_rust_errno_location() == 14,
           "hostname must reject a null destination");
}

void test_nonblocking_pipe_capacity_partial_eof_and_epipe() {
    eos_rust_fd_t descriptors[2] = {-1, -1};
    expect(eos_rust_pipe(descriptors, EOS_RUST_O_NONBLOCK |
                                       EOS_RUST_O_CLOEXEC) == 0,
           "nonblocking pipe creation failed");
    expect(descriptors[0] == 3 && descriptors[1] == 4,
           "pipe endpoints must use consecutive lowest descriptors");
    expect(eos_rust_fcntl(descriptors[0], EOS_RUST_F_GETFD, 0) ==
               EOS_RUST_FD_CLOEXEC &&
               eos_rust_fcntl(descriptors[1], EOS_RUST_F_GETFD, 0) ==
               EOS_RUST_FD_CLOEXEC,
           "pipe CLOEXEC must apply independently to both endpoints");
    expect((eos_rust_fcntl(descriptors[0], EOS_RUST_F_GETFL, 0) &
                (int32_t)EOS_RUST_O_NONBLOCK) != 0,
           "pipe nonblocking state must be observable");

    char byte = 0;
    expect(eos_rust_read(descriptors[0], &byte, 1) == -1 &&
               *eos_rust_errno_location() == 35,
           "empty nonblocking pipe with a writer must report EAGAIN");
    std::vector<char> payload(5000, 'p');
    expect(eos_rust_write(descriptors[1], payload.data(), payload.size()) ==
               4096,
           "nonblocking write must return bounded partial capacity");
    expect(eos_rust_write(descriptors[1], &byte, 1) == -1 &&
               *eos_rust_errno_location() == 35,
           "full nonblocking pipe must report EAGAIN");
    std::vector<char> received(4096);
    expect(eos_rust_read(descriptors[0], received.data(), 100) == 100,
           "pipe read must permit partial queue draining");
    expect(eos_rust_read(descriptors[0], received.data() + 100, 3996) == 3996,
           "pipe read must preserve queued byte order");

    eos_rust_fd_t writer_copy = eos_rust_dup(descriptors[1]);
    expect(writer_copy == 5, "pipe writer dup failed");
    expect(eos_rust_close(descriptors[1]) == 0,
           "original writer close failed");
    expect(eos_rust_read(descriptors[0], &byte, 1) == -1 &&
               *eos_rust_errno_location() == 35,
           "a writer duplicate must prevent early EOF");
    expect(eos_rust_close(writer_copy) == 0,
           "final writer duplicate close failed");
    expect(eos_rust_read(descriptors[0], &byte, 1) == 0,
           "EOF must appear only after all writer descriptions close");
    expect(eos_rust_close(descriptors[0]) == 0,
           "reader endpoint close failed");

    expect(eos_rust_pipe(descriptors, EOS_RUST_O_NONBLOCK) == 0,
           "EPIPE fixture pipe creation failed");
    eos_rust_fd_t reader_copy = eos_rust_dup(descriptors[0]);
    expect(reader_copy >= 5, "pipe reader dup failed");
    expect(eos_rust_close(descriptors[0]) == 0,
           "original reader close failed");
    expect(eos_rust_write(descriptors[1], "x", 1) == 1,
           "reader duplicate must prevent early EPIPE");
    expect(eos_rust_close(reader_copy) == 0,
           "final reader duplicate close failed");
    expect(eos_rust_write(descriptors[1], "x", 1) == -1 &&
               *eos_rust_errno_location() == 32,
           "write with no readers must report EPIPE");
    expect(eos_rust_close(descriptors[1]) == 0,
           "EPIPE fixture writer close failed");
}

void test_blocking_wakeup_status_sharing_and_close_race() {
    eos_rust_fd_t descriptors[2] = {-1, -1};
    expect(eos_rust_pipe(descriptors, 0) == 0,
           "blocking pipe creation failed");
    std::atomic<bool> entered{false};
    std::atomic<int32_t> read_result{-99};
    char received = 0;
    std::thread reader([&] {
        entered.store(true, std::memory_order_release);
        read_result.store(eos_rust_read(descriptors[0], &received, 1),
                          std::memory_order_release);
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(eos_rust_close(descriptors[0]) == 0,
           "concurrent close must remove the reader descriptor immediately");
    expect(eos_rust_write(descriptors[1], "w", 1) == 1,
           "writer must wake an already leased blocked reader");
    reader.join();
    expect(read_result.load(std::memory_order_acquire) == 1 && received == 'w',
           "blocked leased read must finish safely after concurrent close");
    expect(eos_rust_close(descriptors[1]) == 0,
           "blocking pipe writer cleanup failed");

    expect(eos_rust_pipe(descriptors, 0) == 0,
           "blocked EOF fixture creation failed");
    entered.store(false, std::memory_order_relaxed);
    read_result.store(-99, std::memory_order_relaxed);
    std::thread eof_reader([&] {
        entered.store(true, std::memory_order_release);
        read_result.store(eos_rust_read(descriptors[0], &received, 1),
                          std::memory_order_release);
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(eos_rust_close(descriptors[1]) == 0,
           "closing the last writer failed");
    eof_reader.join();
    expect(read_result.load(std::memory_order_acquire) == 0,
           "closing the last writer must wake a blocked reader with EOF");
    expect(eos_rust_close(descriptors[0]) == 0,
           "blocked EOF reader cleanup failed");

    expect(eos_rust_pipe(descriptors, 0) == 0,
           "blocked EPIPE fixture creation failed");
    std::vector<char> full(4096, 'f');
    expect(eos_rust_write(descriptors[1], full.data(), full.size()) == 4096,
           "blocked EPIPE fixture fill failed");
    entered.store(false, std::memory_order_relaxed);
    std::atomic<int32_t> write_result{-99};
    std::thread blocked_writer([&] {
        entered.store(true, std::memory_order_release);
        write_result.store(eos_rust_write(descriptors[1], "x", 1),
                           std::memory_order_release);
    });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(eos_rust_close(descriptors[0]) == 0,
           "closing the last reader failed");
    blocked_writer.join();
    expect(write_result.load(std::memory_order_acquire) == -1,
           "closing the last reader must wake a blocked writer with EPIPE");
    expect(eos_rust_close(descriptors[1]) == 0,
           "blocked EPIPE writer cleanup failed");

    expect(eos_rust_pipe(descriptors, 0) == 0,
           "status-sharing pipe creation failed");
    eos_rust_fd_t duplicate = eos_rust_dup(descriptors[0]);
    expect(duplicate >= 5, "status-sharing dup failed");
    expect(eos_rust_fcntl(duplicate, EOS_RUST_F_SETFL,
                          EOS_RUST_O_NONBLOCK) == 0,
           "pipe SETFL nonblocking failed");
    expect((eos_rust_fcntl(descriptors[0], EOS_RUST_F_GETFL, 0) &
                (int32_t)EOS_RUST_O_NONBLOCK) != 0,
           "pipe status flags must be shared by duplicates");
    expect(eos_rust_fcntl(descriptors[0], EOS_RUST_F_SETFL,
                          EOS_RUST_O_APPEND) == -1 &&
               *eos_rust_errno_location() == 22,
           "pipe SETFL must reject unsupported status bits");
    expect(eos_rust_close(descriptors[0]) == 0 &&
               eos_rust_close(duplicate) == 0,
           "status-sharing readers cleanup failed");
    expect(eos_rust_write(descriptors[1], "x", 1) == -1 &&
               *eos_rust_errno_location() == 32,
           "closing the final shared reader object must wake peers as EPIPE");
    expect(eos_rust_close(descriptors[1]) == 0,
           "status-sharing writer cleanup failed");
}

void test_pipe_argument_and_kind_validation() {
    eos_rust_fd_t descriptors[2] = {-1, -1};
    char byte = 0;
    expect(eos_rust_pipe(nullptr, 0) == -1 &&
               *eos_rust_errno_location() == 14,
           "pipe must validate its output array");
    expect(eos_rust_pipe(descriptors, EOS_RUST_O_APPEND) == -1 &&
               *eos_rust_errno_location() == 22,
           "pipe must reject unsupported creation flags");
    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_pipe(descriptors, 0) == -1 &&
               *eos_rust_errno_location() == 12 && descriptors[0] == -1 &&
               descriptors[1] == -1,
           "pipe allocation failure must report ENOMEM without endpoints");
    expect(eos_rust_isatty(-1) == 0 &&
               *eos_rust_errno_location() == 9,
           "isatty must return zero and EBADF for an invalid descriptor");
    expect(eos_rust_pipe(descriptors, 0) == 0,
           "isatty pipe fixture creation failed");
    eos_host_test_fail_next_sync_wait(17);
    expect(eos_rust_read(descriptors[0], &byte, 1) == -1 &&
               *eos_rust_errno_location() == 16,
           "pipe wait failure must return its mapped error without spinning");
    expect(eos_rust_isatty(descriptors[0]) == 0 &&
               *eos_rust_errno_location() == 25,
           "isatty must return zero and ENOTTY for a valid nonterminal");
    expect(eos_rust_write(descriptors[0], "x", 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "pipe reader descriptor must reject writes");
    expect(eos_rust_read(descriptors[1], &byte, 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "pipe writer descriptor must reject reads");
    expect(eos_rust_close(descriptors[0]) == 0 &&
               eos_rust_close(descriptors[1]) == 0,
           "validation fixture cleanup failed");
}

void test_oversized_console_and_pipe_io_is_rejected_before_dispatch() {
    constexpr uint32_t oversized =
        static_cast<uint32_t>(INT32_MAX) + UINT32_C(1);
    expect(eos_rust_read(0, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_write(1, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22,
           "oversized console I/O must fail EINVAL before buffer access");

    eos_rust_fd_t descriptors[2] = {-1, -1};
    expect(eos_rust_pipe(descriptors, EOS_RUST_O_NONBLOCK) == 0,
           "oversized pipe fixture creation failed");
    expect(eos_rust_read(descriptors[0], nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_write(descriptors[1], nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22,
           "oversized pipe I/O must fail EINVAL before buffer access");
    expect(eos_rust_close(descriptors[0]) == 0 &&
               eos_rust_close(descriptors[1]) == 0,
           "oversized pipe fixture cleanup failed");
}

} // namespace

int main() {
    reset_fixture();
    test_standard_streams_and_hostname();
    test_nonblocking_pipe_capacity_partial_eof_and_epipe();
    test_blocking_wakeup_status_sharing_and_close_race();
    test_pipe_argument_and_kind_validation();
    test_oversized_console_and_pipe_io_is_rejected_before_dispatch();
    return EXIT_SUCCESS;
}
