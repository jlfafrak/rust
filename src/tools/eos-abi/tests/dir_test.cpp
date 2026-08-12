#include "eos_fd_table.h"
#include "eos_rust_abi.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_alloc(int32_t status);
void eos_host_test_pause_closedir_after_validation(void);
void eos_host_test_wait_closedir_validation(void);
void eos_host_test_resume_closedir(void);
void eos_fs_test_reset(void);
}

static_assert(sizeof(eos_rust_dirent) == 80, "stable dirent size changed");
static_assert(alignof(eos_rust_dirent) == 8, "stable dirent alignment changed");
static_assert(offsetof(eos_rust_dirent, d_name) == 16,
              "stable dirent name offset changed");

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool value, const char *message) {
    if (!value) fail(message);
}

std::string fixture_path() {
    return "/tmp/eos-rust-dir-" + std::to_string((long long)getpid());
}

void reset_fixture() {
    eos_fd_test_reset();
    eos_host_test_reset();
    eos_fs_test_reset();
    const char *argv[] = {"dir-test", nullptr};
    eos_rust_runtime_init(1, argv);
}

void touch(const char *path) {
    eos_rust_fd_t descriptor = eos_rust_open(
        path, EOS_RUST_O_CREAT | EOS_RUST_O_TRUNC | EOS_RUST_O_WRONLY, 0666);
    expect(descriptor >= 3, "fixture file open failed");
    expect(eos_rust_close(descriptor) == 0, "fixture file close failed");
}

void test_directory_snapshot_is_stable(const std::string &base) {
    expect(eos_rust_mkdir(base.c_str(), 0777) == 0,
           "directory fixture create failed");
    expect(eos_rust_chdir(base.c_str()) == 0,
           "directory fixture chdir failed");
    touch("alpha");
    expect(eos_rust_mkdir("child", 0777) == 0,
           "child directory fixture create failed");

    eos_rust_fd_t directory = eos_rust_opendir(".");
    expect(directory >= 3, "opendir must create a descriptor snapshot");
    touch("after-snapshot");

    eos_rust_dirent first{};
    eos_rust_dirent entry{};
    std::set<std::string> names;
    int32_t result = eos_rust_readdir(directory, &first);
    expect(result == 1, "nonempty snapshot must yield an entry");
    expect(first.d_name_length == std::strlen(first.d_name) &&
               first.d_name_length <= EOS_RUST_NAME_MAX,
           "directory entry must carry a bounded copied name");
    names.insert(first.d_name);
    const eos_rust_dirent stable_copy = first;
    while ((result = eos_rust_readdir(directory, &entry)) == 1) {
        expect(entry.d_name[entry.d_name_length] == '\0',
               "directory entry name must be terminated");
        expect(entry.d_type == EOS_RUST_DT_REG ||
                   entry.d_type == EOS_RUST_DT_DIR,
               "directory entry type must use the fixed compatibility enum");
        names.insert(entry.d_name);
    }
    expect(result == 0, "directory iteration must report EOF as zero");
    expect(eos_rust_readdir(directory, &entry) == 0,
           "directory EOF must remain stable");
    expect(names == std::set<std::string>({"alpha", "child"}),
           "snapshot must exclude later directory mutations");
    expect(std::strcmp(first.d_name, stable_copy.d_name) == 0 &&
               first.d_type == stable_copy.d_type,
           "returned entry storage must remain caller-owned and stable");
    expect(eos_rust_closedir(directory) == 0,
           "closedir must close the directory descriptor");
    expect(eos_rust_readdir(directory, &entry) == -1 &&
               *eos_rust_errno_location() == 9,
           "readdir on a closed descriptor must report EBADF");
}

void test_empty_wrong_kind_and_failure_contracts() {
    expect(eos_rust_mkdir("empty", 0777) == 0,
           "empty directory create failed");
    eos_rust_fd_t empty = eos_rust_opendir("empty");
    expect(empty >= 3, "empty opendir failed");
    eos_rust_dirent entry{};
    expect(eos_rust_readdir(empty, &entry) == 0,
           "an empty directory snapshot must immediately return EOF");
    expect(eos_rust_closedir(empty) == 0, "empty closedir failed");

    eos_rust_fd_t file = eos_rust_open("alpha", EOS_RUST_O_RDONLY, 0);
    expect(file >= 3, "wrong-kind file fixture open failed");
    expect(eos_rust_readdir(file, &entry) == -1 &&
               *eos_rust_errno_location() == 9,
           "readdir must reject a file descriptor kind");
    expect(eos_rust_closedir(file) == -1 &&
               *eos_rust_errno_location() == 9,
           "closedir must not close the wrong descriptor kind");
    expect(eos_rust_close(file) == 0, "wrong-kind fixture close failed");
    expect(eos_rust_readdir(-1, nullptr) == -1 &&
               *eos_rust_errno_location() == 14,
           "readdir must validate its output before descriptor lookup");

    eos_host_test_fail_next_alloc(15);
    expect(eos_rust_opendir("empty") == -1 &&
               *eos_rust_errno_location() == 12,
           "snapshot allocation failure must report ENOMEM");

    expect(eos_rust_rmdir(".") == -1 &&
               *eos_rust_errno_location() == 66,
           "rmdir must reject a nonempty directory without recursive deletion");
    expect(eos_rust_unlink("child") == -1 &&
               *eos_rust_errno_location() == 21,
           "unlink must reject a directory with EISDIR");
    expect(eos_rust_rmdir("alpha") == -1 &&
               *eos_rust_errno_location() == 20,
           "rmdir must reject a regular file with ENOTDIR");
}

void test_closedir_does_not_close_a_reused_descriptor() {
    expect(eos_rust_mkdir("race-original", 0777) == 0 &&
               eos_rust_mkdir("race-replacement", 0777) == 0,
           "closedir race fixture directories failed");
    eos_rust_fd_t original = eos_rust_opendir("race-original");
    expect(original >= 3, "closedir race original open failed");

    std::atomic<int32_t> close_result{-99};
    std::atomic<int32_t> close_errno{-99};
    eos_host_test_pause_closedir_after_validation();
    std::thread closer([&] {
        close_result.store(eos_rust_closedir(original),
                           std::memory_order_release);
        close_errno.store(*eos_rust_errno_location(),
                          std::memory_order_release);
    });
    eos_host_test_wait_closedir_validation();

    expect(eos_rust_close(original) == 0,
           "concurrent integer close of original directory failed");
    eos_rust_fd_t replacement = eos_rust_opendir("race-replacement");
    expect(replacement == original,
           "closedir race must reuse the validated descriptor number");
    eos_host_test_resume_closedir();
    closer.join();

    expect(close_result.load(std::memory_order_acquire) == -1 &&
               close_errno.load(std::memory_order_acquire) == 9,
           "stale closedir must report EBADF after descriptor reuse");
    eos_rust_dirent entry{};
    expect(eos_rust_readdir(replacement, &entry) == 0,
           "stale closedir must not close the replacement directory");
    expect(eos_rust_closedir(replacement) == 0,
           "replacement directory close failed");
    expect(eos_rust_rmdir("race-original") == 0 &&
               eos_rust_rmdir("race-replacement") == 0,
           "closedir race fixture cleanup failed");
}

void cleanup_fixture(const std::string &base) {
    expect(eos_rust_unlink("alpha") == 0, "alpha cleanup failed");
    expect(eos_rust_unlink("after-snapshot") == 0,
           "snapshot mutation cleanup failed");
    expect(eos_rust_rmdir("child") == 0 && eos_rust_rmdir("empty") == 0,
           "child directory cleanup failed");
    expect(eos_rust_chdir("/") == 0, "directory cwd reset failed");
    expect(eos_rust_rmdir(base.c_str()) == 0,
           "fixture root cleanup failed");
}

} // namespace

int main() {
    reset_fixture();
    const std::string base = fixture_path();
    std::filesystem::remove_all(base);
    test_directory_snapshot_is_stable(base);
    test_empty_wrong_kind_and_failure_contracts();
    test_closedir_does_not_close_a_reused_descriptor();
    cleanup_fixture(base);
    std::filesystem::remove_all(base);
    return EXIT_SUCCESS;
}
