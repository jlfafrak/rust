#include "eos_rust_abi.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char *test_name = "EOS_RUST_TASK4_ENV";
constexpr const char *race_name = "EOS_RUST_TASK4_RACE";

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

const char *find_snapshot_value(char **snapshot, const char *name) {
    const std::size_t name_length = std::strlen(name);
    for (std::size_t index = 0; snapshot[index] != nullptr; ++index) {
        if (std::strncmp(snapshot[index], name, name_length) == 0 &&
            snapshot[index][name_length] == '=') {
            return snapshot[index] + name_length + 1;
        }
    }
    return nullptr;
}

void test_abort_and_exit_terminate_only_the_child() {
    pid_t child = fork();
    expect(child >= 0, "fork for abort test failed");
    if (child == 0) {
        eos_rust_abort();
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child, "waitpid for abort child failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "eos_rust_abort must terminate with SIGABRT on the host");

    child = fork();
    expect(child >= 0, "fork for exit test failed");
    if (child == 0) {
        eos_rust_exit(INT32_C(37));
    }
    status = 0;
    expect(waitpid(child, &status, 0) == child, "waitpid for exit child failed");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 37,
           "eos_rust_exit must preserve the low eight status bits on the host");
}

void test_environment_validation_and_overwrite() {
    expect(eos_rust_unsetenv(test_name) == 0,
           "initial environment cleanup must succeed");

    *eos_rust_errno_location() = 81;
    expect(eos_rust_setenv(test_name, "first", 1) == 0,
           "setting a new environment value must succeed");
    expect(*eos_rust_errno_location() == 81,
           "successful setenv must preserve errno");
    expect(std::strcmp(eos_rust_getenv(test_name), "first") == 0,
           "getenv must return the newly set value");

    expect(eos_rust_setenv(test_name, "ignored", 0) == 0,
           "setenv overwrite=false must succeed for an existing name");
    expect(std::strcmp(eos_rust_getenv(test_name), "first") == 0,
           "setenv overwrite=false must preserve the existing value");

    expect(eos_rust_setenv(test_name, "second", 1) == 0,
           "setenv overwrite=true must replace the value");
    expect(std::strcmp(eos_rust_getenv(test_name), "second") == 0,
           "getenv must return the replacement value");

    expect(eos_rust_unsetenv(test_name) == 0,
           "unsetting an existing value must succeed");
    expect(eos_rust_getenv(test_name) == nullptr,
           "getenv must return null after unsetenv");
    expect(eos_rust_unsetenv(test_name) == 0,
           "unsetting an absent value must succeed");

    expect(eos_rust_setenv(nullptr, "value", 1) == -1,
           "null environment name must fail");
    expect(*eos_rust_errno_location() == 22,
           "null environment name must report EOS EINVAL (22)");
    expect(eos_rust_setenv("", "value", 1) == -1,
           "empty environment name must fail");
    expect(eos_rust_setenv("BAD=NAME", "value", 1) == -1,
           "environment name containing '=' must fail");
    expect(eos_rust_setenv(test_name, nullptr, 1) == -1,
           "null environment value must fail");
    expect(eos_rust_unsetenv("BAD=NAME") == -1,
           "unsetenv must reject a name containing '='");
}

void test_environment_snapshots_have_process_lifetime() {
    expect(eos_rust_setenv(test_name, "snapshot-one", 1) == 0,
           "snapshot fixture set failed");
    char **first = eos_rust_environ();
    const char *first_value = find_snapshot_value(first, test_name);
    expect(first_value != nullptr && std::strcmp(first_value, "snapshot-one") == 0,
           "first snapshot must contain its published value");
    char *first_getenv = eos_rust_getenv(test_name);

    expect(eos_rust_setenv(test_name, "snapshot-two", 1) == 0,
           "snapshot replacement failed");
    char **second = eos_rust_environ();
    expect(second != first, "an environment update must publish a new vector");
    expect(std::strcmp(first_value, "snapshot-one") == 0,
           "an old environment vector must remain valid and immutable");
    expect(std::strcmp(first_getenv, "snapshot-one") == 0,
           "an old getenv result must remain valid after replacement");
    expect(std::strcmp(find_snapshot_value(second, test_name), "snapshot-two") == 0,
           "the new environment vector must contain the replacement");

    expect(eos_rust_unsetenv(test_name) == 0,
           "snapshot fixture cleanup failed");
    expect(find_snapshot_value(eos_rust_environ(), test_name) == nullptr,
           "the current snapshot must omit an unset value");
    expect(std::strcmp(first_value, "snapshot-one") == 0,
           "an old snapshot must remain valid after unsetenv");
}

void test_concurrent_environment_updates_publish_complete_snapshots() {
    constexpr int reader_count = 6;
    constexpr int iterations = 3000;
    expect(eos_rust_setenv(race_name, "alpha", 1) == 0,
           "concurrency fixture set failed");

    std::atomic<bool> start{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < reader_count; ++reader) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const char *value = eos_rust_getenv(race_name);
                if (value == nullptr ||
                    (std::strcmp(value, "alpha") != 0 &&
                     std::strcmp(value, "beta") != 0 &&
                     std::strcmp(value, "final") != 0)) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    break;
                }

                char **snapshot = eos_rust_environ();
                std::size_t entries = 0;
                while (snapshot[entries] != nullptr && entries < 64) {
                    if (std::strchr(snapshot[entries], '=') == nullptr) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    ++entries;
                }
                if (entries == 64) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    std::thread writer([&] {
        start.store(true, std::memory_order_release);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const char *value = (iteration & 1) == 0 ? "beta" : "alpha";
            if (eos_rust_setenv(race_name, value, 1) != 0) {
                failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (eos_rust_setenv(race_name, "final", 1) != 0) {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
    });

    writer.join();
    for (std::thread &reader : readers) {
        reader.join();
    }
    expect(failures.load(std::memory_order_relaxed) == 0,
           "concurrent environment readers observed a torn snapshot");
    expect(std::strcmp(eos_rust_getenv(race_name), "final") == 0,
           "the final environment update must be visible");
    expect(eos_rust_unsetenv(race_name) == 0,
           "concurrency fixture cleanup failed");
}

} // namespace

int main() {
    test_abort_and_exit_terminate_only_the_child();
    test_environment_validation_and_overwrite();
    test_environment_snapshots_have_process_lifetime();
    test_concurrent_environment_updates_publish_complete_snapshots();
    return EXIT_SUCCESS;
}
