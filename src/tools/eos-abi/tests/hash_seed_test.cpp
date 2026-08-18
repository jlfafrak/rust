#include "eos_rust_abi.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {

typedef struct eos_hash_seed_test_sources {
    uint64_t timer_usec;
    uint64_t tick_count;
    uint64_t application_id;
    uint64_t application_name_hash;
    uint64_t thread_identity;
    uint64_t code_address;
    uint64_t heap_address;
    uint64_t stack_address;
    uint64_t device_diversifier;
    uint64_t application_diversifier;
} eos_hash_seed_test_sources;

void eos_hash_seed_test_reset(const eos_hash_seed_test_sources *sources);
void eos_host_test_fail_next_lock(int32_t status);
void eos_host_test_fail_next_unlock(int32_t status);

}

namespace {

struct Seed {
    uint64_t first;
    uint64_t second;

    bool operator==(const Seed &other) const {
        return first == other.first && second == other.second;
    }
};

struct SeedHash {
    std::size_t operator()(const Seed &seed) const {
        return static_cast<std::size_t>(seed.first ^
                                        (seed.second + UINT64_C(0x9e3779b97f4a7c15)));
    }
};

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

eos_hash_seed_test_sources fixed_sources() {
    return {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0x00000000fedcba98),
        UINT64_C(0x0000000000002a5a),
        UINT64_C(0x60b3ecb1d6f49d27),
        UINT64_C(0x000000007e57c0de),
        UINT64_C(0x0000000010042000),
        UINT64_C(0x0000000030051000),
        UINT64_C(0x000000003ffef020),
        UINT64_C(0xa1d2e3f405162738),
        UINT64_C(0x9b8a796857463524),
    };
}

Seed next_seed() {
    Seed seed{};
    expect(eos_rust_hash_seed(&seed.first, &seed.second) == 0,
           "hash seed generation unexpectedly failed");
    return seed;
}

void test_injected_sources_have_known_answers() {
    const eos_hash_seed_test_sources sources = fixed_sources();
    eos_hash_seed_test_reset(&sources);

    const Seed first = next_seed();
    const Seed second = next_seed();
    expect(first == Seed{UINT64_C(0x22e3d9508b0e13d4),
                         UINT64_C(0x98d46b55b7f3f40f)},
           "the first injected-source seed did not match its known answer");
    expect(second == Seed{UINT64_C(0x4952107efd705702),
                          UINT64_C(0x6d0f4da5cbdb4716)},
           "the second injected-source seed did not match its known answer");

    eos_hash_seed_test_reset(&sources);
    expect(next_seed() == first,
           "resetting identical injected sources must reproduce the sequence");

    eos_hash_seed_test_sources changed = sources;
    changed.tick_count ^= UINT64_C(1);
    eos_hash_seed_test_reset(&changed);
    expect(!(next_seed() == first),
           "changing one injected source must separate the seed state");
}

void test_null_outputs_are_ignored_but_advance_state() {
    const eos_hash_seed_test_sources sources = fixed_sources();
    eos_hash_seed_test_reset(&sources);

    uint64_t second_only = 0;
    expect(eos_rust_hash_seed(nullptr, &second_only) == 0,
           "hash seed generation with null first output failed");
    expect(second_only == UINT64_C(0x98d46b55b7f3f40f),
           "a null first output must not suppress the second key");

    uint64_t first_only = 0;
    expect(eos_rust_hash_seed(&first_only, nullptr) == 0,
           "hash seed generation with null second output failed");
    expect(first_only == UINT64_C(0x4952107efd705702),
           "a null second output must not suppress the first key");

    expect(eos_rust_hash_seed(nullptr, nullptr) == 0,
           "hash seed generation with null outputs failed");
    const Seed fourth = next_seed();
    expect(fourth == Seed{UINT64_C(0xbefb53bbf672c78b),
                          UINT64_C(0xa852706b925f1917)},
           "a call with two null outputs must still advance process state");
}

void test_lock_failure_preserves_outputs_and_sequence() {
    const eos_hash_seed_test_sources sources = fixed_sources();
    eos_hash_seed_test_reset(&sources);
    uint64_t first = UINT64_C(0x1111111111111111);
    uint64_t second = UINT64_C(0x2222222222222222);
    eos_host_test_fail_next_lock(INT32_C(17));
    expect(eos_rust_hash_seed(&first, &second) == -1,
           "hash lock failure must be observable by the caller");
    expect(first == UINT64_C(0x1111111111111111) &&
               second == UINT64_C(0x2222222222222222),
           "hash lock failure must preserve caller outputs");
    expect(*eos_rust_errno_location() == 16,
           "hash lock failure must report EOS EBUSY (16)");
    expect(next_seed() == Seed{UINT64_C(0x22e3d9508b0e13d4),
                               UINT64_C(0x98d46b55b7f3f40f)},
           "hash lock failure must not advance process state");
}

void test_hash_lock_release_failure_aborts() {
    const eos_hash_seed_test_sources sources = fixed_sources();
    pid_t child = fork();
    expect(child >= 0, "fork for hash unlock-failure test failed");
    if (child == 0) {
        eos_hash_seed_test_reset(&sources);
        eos_host_test_fail_next_unlock(INT32_C(17));
        (void)next_seed();
        _exit(90);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child,
           "waitpid for hash unlock-failure child failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "hash lock release failure must abort instead of returning keys");
}

void test_concurrent_calls_return_distinct_pairs() {
    constexpr int thread_count = 8;
    constexpr int calls_per_thread = 4000;
    const eos_hash_seed_test_sources sources = fixed_sources();
    eos_hash_seed_test_reset(&sources);

    std::atomic<bool> start{false};
    std::mutex results_mutex;
    std::vector<Seed> results;
    results.reserve(thread_count * calls_per_thread);
    std::vector<std::thread> workers;
    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::vector<Seed> local;
            local.reserve(calls_per_thread);
            for (int call = 0; call < calls_per_thread; ++call) {
                local.push_back(next_seed());
            }
            std::lock_guard<std::mutex> guard(results_mutex);
            results.insert(results.end(), local.begin(), local.end());
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers) {
        worker.join();
    }

    const std::unordered_set<Seed, SeedHash> unique(results.begin(), results.end());
    expect(unique.size() == results.size(),
           "concurrent seed calls returned a repeated pair");
}

void test_one_hundred_thousand_calls_do_not_repeat() {
    constexpr std::size_t call_count = 100000;
    const eos_hash_seed_test_sources sources = fixed_sources();
    eos_hash_seed_test_reset(&sources);

    std::unordered_set<Seed, SeedHash> unique;
    unique.reserve(call_count);
    for (std::size_t call = 0; call < call_count; ++call) {
        if (!unique.insert(next_seed()).second) {
            fail("seed pair repeated within 100,000 calls");
        }
    }
    expect(unique.size() == call_count,
           "the 100,000-call seed set has the wrong size");
}

} // namespace

int main() {
    test_injected_sources_have_known_answers();
    test_null_outputs_are_ignored_but_advance_state();
    test_lock_failure_preserves_outputs_and_sequence();
    test_hash_lock_release_failure_aborts();
    test_concurrent_calls_return_distinct_pairs();
    test_one_hundred_thousand_calls_do_not_repeat();
    return EXIT_SUCCESS;
}
