#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <cstddef>
#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_fs_test_reset(void);
void eos_host_test_file_partial_read(uint32_t bytes, int32_t status);
void eos_host_test_file_partial_write(uint32_t bytes, int32_t status);
void eos_host_test_fail_next_file_close(int32_t status);
void eos_host_test_fail_seek_after(uint32_t successful_seeks, int32_t status);
void eos_host_test_fail_lock_after(uint32_t successful_locks, int32_t status);
void eos_host_test_fail_next_lock(int32_t status);
void eos_host_test_fail_next_alloc(int32_t status);
}

static_assert(sizeof(eos_rust_stat) == 128, "stable stat size changed");
static_assert(alignof(eos_rust_stat) == 8, "stable stat alignment changed");
static_assert(offsetof(eos_rust_stat, st_size) == 32, "stat size offset changed");
static_assert(offsetof(eos_rust_stat, st_mtime_sec) == 56,
              "stat mtime offset changed");
static_assert(offsetof(eos_rust_stat, reserved) == 100,
              "stat reserved offset changed");

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool value, const char *message) {
    if (!value) fail(message);
}

std::string fixture_path() {
    return "/tmp/eos-rust-fs-" + std::to_string((long long)getpid());
}

void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    eos_fs_test_reset();
    *eos_rust_errno_location() = 0;
    const char *argv[] = {"fs-test", nullptr};
    eos_rust_runtime_init(1, argv);
}

void test_file_io_offsets_duplication_and_metadata(const std::string &base) {
    expect(eos_rust_mkdir(base.c_str(), UINT32_C(0777)) == 0,
           "mkdir must create the fixture directory");
    expect(eos_rust_chdir(base.c_str()) == 0, "chdir must accept a directory");

    char cwd[EOS_RUST_PATH_MAX]{};
    expect(eos_rust_getcwd(cwd, sizeof(cwd)) == 0 && cwd == base,
           "getcwd must return the coherent compatibility cwd");
    char short_cwd[4] = {'x', 'x', 'x', 'x'};
    expect(eos_rust_getcwd(short_cwd, sizeof(short_cwd)) == -1 &&
               *eos_rust_errno_location() == 34 && short_cwd[3] == '\0',
           "getcwd truncation must terminate and report ERANGE");

    eos_rust_fd_t fd = eos_rust_open("data.bin",
        EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC | EOS_RUST_O_RDWR |
            EOS_RUST_O_CLOEXEC,
        UINT32_C(0666));
    expect(fd == 3, "first ordinary file must use fd 3");
    expect(eos_rust_fcntl(fd, EOS_RUST_F_GETFD, 0) == EOS_RUST_FD_CLOEXEC,
           "open CLOEXEC must be descriptor-local");

    const char payload[] = "abcdef";
    eos_host_test_file_partial_write(UINT32_C(2), INT32_C(25));
    expect(eos_rust_write(fd, payload, UINT32_C(6)) == 2,
           "partial native write must outrank its trailing error");
    expect(eos_rust_write(fd, payload + 2, UINT32_C(4)) == 4,
           "write must complete the remaining bytes");
    expect(eos_rust_lseek(fd, 0, EOS_RUST_SEEK_SET) == 0,
           "seek to start failed");

    char data[8]{};
    eos_host_test_file_partial_read(UINT32_C(3), INT32_C(26));
    expect(eos_rust_read(fd, data, UINT32_C(6)) == 3 &&
               std::memcmp(data, "abc", 3) == 0,
           "partial native read must return its completed bytes");
    expect(eos_rust_read(fd, data + 3, UINT32_C(3)) == 3 &&
               std::memcmp(data, "abcdef", 6) == 0,
           "ordinary read must continue from shared offset");
    expect(eos_rust_read(fd, data, UINT32_C(1)) == 0,
           "read at end of file must return zero");
    expect(eos_rust_read(fd, nullptr, 0) == 0 &&
               eos_rust_write(fd, nullptr, 0) == 0,
           "zero-length I/O must accept a null buffer");
    expect(eos_rust_read(fd, nullptr, 1) == -1 &&
               *eos_rust_errno_location() == 14,
           "nonzero read must reject a null buffer with EFAULT");

    expect(eos_rust_lseek(fd, 4, EOS_RUST_SEEK_SET) == 4,
           "seek fixture failed");
    char positional[3]{};
    expect(eos_rust_pread(fd, positional, 2, 1) == 2 &&
               std::memcmp(positional, "bc", 2) == 0,
           "pread must read from its explicit offset");
    expect(eos_rust_lseek(fd, 0, EOS_RUST_SEEK_CUR) == 4,
           "pread must restore the shared open-file offset");
    expect(eos_rust_pwrite(fd, "Z", 1, 2) == 1,
           "pwrite must update the explicit position");
    expect(eos_rust_lseek(fd, 0, EOS_RUST_SEEK_CUR) == 4,
           "pwrite must restore the shared open-file offset");

    eos_rust_fd_t duplicate = eos_rust_dup(fd);
    expect(duplicate == 4, "dup must use the lowest free descriptor");
    expect(eos_rust_fcntl(duplicate, EOS_RUST_F_GETFD, 0) == 0,
           "dup must clear close-on-exec");
    expect(eos_rust_read(duplicate, data, 1) == 1 && data[0] == 'e',
           "duplicates must share one file offset");
    expect(eos_rust_fcntl(duplicate, EOS_RUST_F_SETFL,
                          EOS_RUST_O_APPEND) == 0 &&
               (eos_rust_fcntl(fd, EOS_RUST_F_GETFL, 0) &
                    (int32_t)EOS_RUST_O_APPEND) != 0,
           "status flags must be shared by duplicates");
    expect(eos_rust_fcntl(fd, EOS_RUST_F_SETFD, 0) == 0 &&
               eos_rust_fcntl(duplicate, EOS_RUST_F_GETFD, 0) == 0,
           "descriptor flags must remain independent");
    expect(eos_rust_dup2(fd, fd) == fd,
           "dup2 of one descriptor must be a validated no-op");

    eos_rust_stat by_fd{};
    eos_rust_stat by_path{};
    eos_rust_stat by_lstat{};
    expect(eos_rust_fstat(fd, &by_fd) == 0 && by_fd.st_size == 6,
           "fstat must report the file size");
    expect(eos_rust_stat_path("data.bin", &by_path) == 0 &&
               by_path.st_size == by_fd.st_size,
           "stat must report compatible metadata");
    expect(eos_rust_lstat("data.bin", &by_lstat) == 0 &&
               by_lstat.st_ino == by_path.st_ino,
           "lstat must match stat when links are unsupported");
    expect(eos_rust_fsync(fd) == 0, "fsync must flush the native file");

    std::vector<std::thread> positional_writers;
    std::vector<int32_t> positional_results(16, -1);
    for (int index = 0; index < 16; ++index) {
        positional_writers.emplace_back([&, index] {
            const char value = static_cast<char>('A' + index);
            positional_results[(std::size_t)index] =
                eos_rust_pwrite(duplicate, &value, 1, 6 + index);
        });
    }
    for (std::thread &thread : positional_writers) thread.join();
    char positional_block[16]{};
    expect(eos_rust_pread(fd, positional_block, sizeof(positional_block), 6) ==
               (int32_t)sizeof(positional_block),
           "concurrent positional writes did not produce a complete block");
    for (int index = 0; index < 16; ++index) {
        expect(positional_results[(std::size_t)index] == 1 &&
                   positional_block[index] == static_cast<char>('A' + index),
               "positional I/O must serialize seek/use/restore on shared state");
    }
    expect(eos_rust_lseek(fd, 0, EOS_RUST_SEEK_CUR) == 5,
           "concurrent pwrite operations must preserve the shared offset");
    expect(eos_rust_close(fd) == 0 && eos_rust_close(duplicate) == 0,
           "all duplicate file descriptions must close");
    expect(eos_rust_read(fd, data, 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "a stale closed descriptor must report EBADF");

    expect(eos_rust_rename("data.bin", "renamed.bin") == 0,
           "rename within the cwd failed");
    char resolved[EOS_RUST_PATH_MAX]{};
    expect(eos_rust_realpath("renamed.bin", resolved, sizeof(resolved)) == 0 &&
               std::string(resolved) == base + "/renamed.bin",
           "realpath must resolve against a cwd snapshot");
    expect(eos_rust_unlink("renamed.bin") == 0,
           "unlink must remove a regular file");
}

void test_null_validation_and_unsupported_contracts(const std::string &base) {
    eos_rust_fd_t null_fd = eos_rust_open("/dev/null", EOS_RUST_O_RDWR, 0);
    expect(null_fd >= 3, "internal /dev/null open failed");
    char byte = 'x';
    expect(eos_rust_read(null_fd, &byte, 1) == 0,
           "/dev/null reads must return EOF");
    expect(eos_rust_write(null_fd, "hello", 5) == 5,
           "/dev/null writes must consume every byte");
    expect(eos_rust_close(null_fd) == 0, "/dev/null close failed");

    eos_rust_fd_t read_null =
        eos_rust_open("/dev/null", EOS_RUST_O_RDONLY, 0);
    eos_rust_fd_t write_null =
        eos_rust_open("/dev/null", EOS_RUST_O_WRONLY, 0);
    expect(read_null >= 3 && write_null >= 3,
           "/dev/null access-mode fixtures failed");
    expect(eos_rust_read(read_null, &byte, 1) == 0,
           "read-only /dev/null must remain readable");
    expect(eos_rust_write(read_null, &byte, 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "read-only /dev/null must reject writes with EBADF");
    expect(eos_rust_write(write_null, &byte, 1) == 1,
           "write-only /dev/null must remain writable");
    expect(eos_rust_read(write_null, &byte, 1) == -1 &&
               *eos_rust_errno_location() == 9,
           "write-only /dev/null must reject reads with EBADF");

    constexpr uint32_t oversized =
        static_cast<uint32_t>(INT32_MAX) + UINT32_C(1);
    expect(eos_rust_read(read_null, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_write(write_null, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22,
           "oversized /dev/null I/O must fail EINVAL before buffer access");
    expect(eos_rust_close(read_null) == 0 &&
               eos_rust_close(write_null) == 0,
           "/dev/null access-mode fixture cleanup failed");

    expect(eos_rust_open(nullptr, EOS_RUST_O_RDONLY, 0) == -1 &&
               *eos_rust_errno_location() == 14,
           "open must validate a null path");
    std::string too_long(EOS_RUST_PATH_MAX, 'a');
    expect(eos_rust_open(too_long.c_str(), EOS_RUST_O_RDONLY, 0) == -1 &&
               *eos_rust_errno_location() == 63,
           "paths at or beyond the storage capacity must fail ENAMETOOLONG");
    expect(eos_rust_chdir("missing") == -1,
           "chdir must reject missing paths");
    expect(eos_rust_rmdir(base.c_str()) == 0,
           "rmdir must remove the empty fixture directory");

    expect(eos_rust_symlink("a", "b") == -1 &&
               *eos_rust_errno_location() == 45,
           "symlink must explicitly report ENOTSUP");
    expect(eos_rust_link("a", "b") == -1 &&
               eos_rust_chown("a", 1, 1) == -1 &&
               eos_rust_lchown("a", 1, 1) == -1 &&
               eos_rust_chmod("a", 0777) == -1 &&
               *eos_rust_errno_location() == 45,
           "link, ownership, and permission mutation must not report success");
    expect(eos_rust_fchown(-1, 1, 1) == -1 &&
               eos_rust_fchmod(-1, 0777) == -1 &&
               *eos_rust_errno_location() == 45,
           "fd ownership/permission mutation must consistently report ENOTSUP");
}

void test_canonical_parent_paths(const std::string &base) {
    expect(eos_rust_mkdir("parent", 0777) == 0 &&
               eos_rust_mkdir("child", 0777) == 0,
           "canonical path fixture directories failed");
    expect(eos_rust_chdir("parent") == 0,
           "canonical path fixture chdir failed");

    char resolved[EOS_RUST_PATH_MAX]{};
    expect(eos_rust_realpath("..", resolved, sizeof(resolved)) == 0 &&
               std::string(resolved) == base,
           "realpath(..) must not retain a trailing slash");
    expect(eos_rust_realpath("../child", resolved, sizeof(resolved)) == 0 &&
               std::string(resolved) == base + "/child",
           "realpath(../child) must not create a double slash");
    expect(eos_rust_realpath("../../../../", resolved, sizeof(resolved)) == 0 &&
               std::string(resolved) == "/",
           "repeated parents must clamp to the root path");
    expect(eos_rust_realpath("/../../", resolved, sizeof(resolved)) == 0 &&
               std::string(resolved) == "/",
           "absolute parents must clamp to the root path");

    expect(eos_rust_chdir("..") == 0,
           "canonical parent chdir failed");
    char cwd[EOS_RUST_PATH_MAX]{};
    expect(eos_rust_getcwd(cwd, sizeof(cwd)) == 0 &&
               std::string(cwd) == base,
           "chdir/getcwd must store the canonical parent without a slash");
    expect(eos_rust_rmdir("parent") == 0 && eos_rust_rmdir("child") == 0,
           "canonical path fixture cleanup failed");
}

void test_oversized_native_io() {
    static const char path[] = "/tmp/eos-rust-io-limits";
    constexpr uint32_t oversized =
        static_cast<uint32_t>(INT32_MAX) + UINT32_C(1);
    eos_rust_fd_t descriptor = eos_rust_open(
        path, EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC | EOS_RUST_O_RDWR, 0666);
    expect(descriptor >= 3, "I/O limit fixture open failed");

    expect(eos_rust_read(descriptor, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_write(descriptor, nullptr, oversized) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_pread(descriptor, nullptr, oversized, 0) == -1 &&
               *eos_rust_errno_location() == 22 &&
               eos_rust_pwrite(descriptor, nullptr, oversized, 0) == -1 &&
               *eos_rust_errno_location() == 22,
           "oversized native and positional I/O must fail EINVAL before dispatch");

    expect(eos_rust_close(descriptor) == 0 && eos_rust_unlink(path) == 0,
           "I/O limit fixture cleanup failed");
}

void test_lseek_error_preservation() {
    static const char path[] = "/tmp/eos-rust-lseek-errors";
    eos_rust_fd_t descriptor = eos_rust_open(
        path, EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC | EOS_RUST_O_RDWR, 0666);
    expect(descriptor >= 3, "lseek error fixture open failed");
    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_lseek(descriptor, 0, EOS_RUST_SEEK_CUR) == -1 &&
               *eos_rust_errno_location() == 12,
           "lseek must preserve lease ENOMEM instead of replacing it with ESPIPE");
    eos_host_test_fail_next_lock(17);
    expect(eos_rust_lseek(descriptor, 0, EOS_RUST_SEEK_CUR) == -1 &&
               *eos_rust_errno_location() == 16,
           "lseek must preserve descriptor lock failure instead of ESPIPE");
    expect(eos_rust_lseek(descriptor, 0, EOS_RUST_SEEK_CUR) == 0,
           "lseek must remain usable after injected acquisition failures");
    expect(eos_rust_close(descriptor) == 0 && eos_rust_unlink(path) == 0,
           "lseek error fixture cleanup failed");
}

void test_irreversible_close_failure_is_fail_fast() {
    eos_rust_fd_t descriptor = eos_rust_open(
        "/tmp/eos-rust-close-fault", EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC |
            EOS_RUST_O_WRONLY, 0666);
    expect(descriptor >= 3, "close-failure fixture open failed");
    pid_t child = fork();
    expect(child >= 0, "close-failure fixture fork failed");
    if (child == 0) {
        eos_host_test_fail_next_file_close(25);
        (void)eos_rust_close(descriptor);
        _exit(91);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "close-failure child wait failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "irreversible native close failure must abort, not report success");
    expect(eos_rust_close(descriptor) == 0,
           "parent close-failure fixture cleanup failed");
    expect(eos_rust_unlink("/tmp/eos-rust-close-fault") == 0,
           "close-failure fixture unlink failed");
}

void test_positional_restore_failure_is_fail_fast() {
    eos_rust_fd_t descriptor = eos_rust_open(
        "/tmp/eos-rust-restore-fault", EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC |
            EOS_RUST_O_RDWR, 0666);
    expect(descriptor >= 3 && eos_rust_write(descriptor, "a", 1) == 1,
           "restore-failure fixture initialization failed");
    pid_t child = fork();
    expect(child >= 0, "restore-failure fixture fork failed");
    if (child == 0) {
        char byte = 0;
        eos_host_test_fail_seek_after(1, 25);
        (void)eos_rust_pread(descriptor, &byte, 1, 0);
        _exit(92);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "restore-failure child wait failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "positional restore failure must abort after transferred data");
    expect(eos_rust_close(descriptor) == 0,
           "restore-failure parent fixture cleanup failed");
    expect(eos_rust_unlink("/tmp/eos-rust-restore-fault") == 0,
           "restore-failure fixture unlink failed");
}

void test_post_io_lease_release_failure_is_fail_fast() {
    eos_rust_fd_t descriptor = eos_rust_open(
        "/tmp/eos-rust-release-fault", EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC |
            EOS_RUST_O_RDWR, 0666);
    expect(descriptor >= 3 && eos_rust_write(descriptor, "a", 1) == 1 &&
               eos_rust_lseek(descriptor, 0, EOS_RUST_SEEK_SET) == 0,
           "release-failure fixture initialization failed");
    pid_t child = fork();
    expect(child >= 0, "release-failure fixture fork failed");
    if (child == 0) {
        char byte = 0;
        eos_host_test_fail_lock_after(2, 17);
        (void)eos_rust_read(descriptor, &byte, 1);
        _exit(93);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "release-failure child wait failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "post-I/O lease-release failure must abort after transferred data");
    expect(eos_rust_close(descriptor) == 0,
           "release-failure parent fixture cleanup failed");
    expect(eos_rust_unlink("/tmp/eos-rust-release-fault") == 0,
           "release-failure fixture unlink failed");
}

} // namespace

int main(int argc, char **argv) {
    reset_fixture();
    const std::string base = fixture_path();
    std::filesystem::remove_all(base);
    if (argc == 2 && std::strcmp(argv[1], "canonical") == 0) {
        expect(eos_rust_mkdir(base.c_str(), UINT32_C(0777)) == 0 &&
                   eos_rust_chdir(base.c_str()) == 0,
               "canonical selector setup failed");
        test_canonical_parent_paths(base);
        expect(eos_rust_chdir("/") == 0 && eos_rust_rmdir(base.c_str()) == 0,
               "canonical selector cleanup failed");
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::strcmp(argv[1], "io-validation") == 0) {
        expect(eos_rust_mkdir(base.c_str(), UINT32_C(0777)) == 0,
               "I/O validation selector setup failed");
        test_null_validation_and_unsupported_contracts(base);
        test_oversized_native_io();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::strcmp(argv[1], "lseek") == 0) {
        test_lseek_error_preservation();
        return EXIT_SUCCESS;
    }
    test_file_io_offsets_duplication_and_metadata(base);
    test_canonical_parent_paths(base);
    expect(eos_rust_chdir("/") == 0, "fixture cwd reset failed");
    test_null_validation_and_unsupported_contracts(base);
    test_oversized_native_io();
    test_lseek_error_preservation();
    test_irreversible_close_failure_is_fail_fast();
    test_positional_restore_failure_is_fail_fast();
    test_post_io_lease_release_failure_is_fail_fast();
    std::filesystem::remove_all(base);
    return EXIT_SUCCESS;
}
