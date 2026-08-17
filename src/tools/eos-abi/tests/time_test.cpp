#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_lock(int32_t status);
void eos_time_test_reset(void);
void eos_time_test_set_monotonic_sequence(const uint64_t *values,
                                          uint32_t count);
void eos_time_test_set_realtime(uint64_t value, int32_t status);
void eos_time_test_fail_delay_after(uint32_t successful_calls,
                                    int32_t status);
void eos_time_test_set_delay_advances_clock(int enabled);
uint32_t eos_time_test_delay_count(void);
uint32_t eos_time_test_delay_value(uint32_t index);
uint32_t eos_time_test_delay_usec_count(void);
uint32_t eos_time_test_delay_usec_value(uint32_t index);
uint32_t eos_time_test_deadline_ticks(uint64_t now_usec,
                                      const eos_rust_timespec *deadline,
                                      uint32_t ticks_per_second,
                                      int32_t *expired);
}

namespace {
constexpr int32_t kIo = 5;
constexpr int32_t kBusy = 16;
constexpr int32_t kInvalid = 22;
constexpr int32_t kNotSupported = 45;
constexpr int32_t kOverflow = 84;
constexpr uint32_t kMaximumFinite = UINT32_MAX - UINT32_C(1);

[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) fail(message);
}

void test_clock_layout_and_validation() {
    static_assert(sizeof(eos_rust_timespec) == 16);
    static_assert(alignof(eos_rust_timespec) == alignof(int64_t));
    expect(EOS_RUST_CLOCK_REALTIME == 0 && EOS_RUST_CLOCK_MONOTONIC == 1,
           "clock identifiers drifted");
    eos_rust_timespec value{17, 23};
    *eos_rust_errno_location() = 777;
    expect(eos_rust_clock_gettime(9, &value) == -1 &&
               *eos_rust_errno_location() == kInvalid && value.tv_sec == 17 &&
               value.tv_nsec == 23,
           "unknown clock must fail without mutating output");
    *eos_rust_errno_location() = 777;
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, nullptr) == -1 &&
               *eos_rust_errno_location() == kInvalid,
           "null clock output must report EINVAL");
    value = {31, 37};
    int32_t *const compatibility_errno = eos_rust_errno_location();
    *compatibility_errno = 777;
    eos_host_test_fail_next_lock(17);
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &value) == -1 &&
               *compatibility_errno == kBusy && value.tv_sec == 31 &&
               value.tv_nsec == 37,
           "monotonic clamp-lock failure must map exactly and preserve output");
}

void test_monotonic_non_regression_and_concurrency() {
    const uint64_t source[] = {UINT64_C(1000001), UINT64_C(999999),
                               UINT64_C(1000002)};
    eos_time_test_set_monotonic_sequence(source, 3);
    eos_rust_timespec first{};
    eos_rust_timespec second{};
    eos_rust_timespec third{};
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &first) == 0 &&
               eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &second) == 0 &&
               eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &third) == 0,
           "scripted monotonic reads failed");
    expect(first.tv_sec == 1 && first.tv_nsec == 1000 &&
               second.tv_sec == 1 && second.tv_nsec == 1000 &&
               third.tv_sec == 1 && third.tv_nsec == 2000,
           "monotonic source regression was not clamped");

    std::vector<uint64_t> concurrent_source;
    for (uint64_t index = 0; index != 32000; ++index) {
        concurrent_source.push_back(UINT64_C(2000000) +
                                    (index % 7 == 0 ? 0 : index));
    }
    eos_time_test_set_monotonic_sequence(concurrent_source.data(),
                                         static_cast<uint32_t>(
                                             concurrent_source.size()));
    std::atomic<uint64_t> maximum{0};
    std::atomic<bool> regressed{false};
    std::vector<std::thread> readers;
    for (uint32_t thread = 0; thread != 8; ++thread) {
        readers.emplace_back([&] {
            uint64_t local = 0;
            for (uint32_t iteration = 0; iteration != 4000; ++iteration) {
                eos_rust_timespec observed{};
                if (eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC,
                                           &observed) != 0) {
                    regressed.store(true);
                    return;
                }
                const uint64_t current =
                    static_cast<uint64_t>(observed.tv_sec) * UINT64_C(1000000) +
                    static_cast<uint64_t>(observed.tv_nsec / 1000);
                if (current < local) regressed.store(true);
                local = current;
                uint64_t prior = maximum.load();
                while (prior < current &&
                       !maximum.compare_exchange_weak(prior, current)) {}
            }
        });
    }
    for (auto &reader : readers) reader.join();
    expect(!regressed.load() && maximum.load() >= UINT64_C(2000000),
           "concurrent monotonic reads regressed");
}

void test_realtime_separation_and_errors() {
    eos_time_test_set_realtime(UINT64_C(1234567), 0);
    eos_rust_timespec realtime{};
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_REALTIME, &realtime) == 0 &&
               realtime.tv_sec == 1 && realtime.tv_nsec == 234567000,
           "UTC microseconds were not normalized exactly");
    eos_time_test_set_realtime(UINT64_C(7654321), 25);
    realtime = {9, 11};
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_REALTIME, &realtime) == -1 &&
               *eos_rust_errno_location() == kIo && realtime.tv_sec == 9 &&
               realtime.tv_nsec == 11,
           "UTC failure must map and preserve output");

    const uint64_t monotonic[] = {UINT64_C(5000000), UINT64_C(5000001)};
    eos_time_test_set_monotonic_sequence(monotonic, 2);
    eos_time_test_set_realtime(UINT64_C(999999999), 0);
    eos_rust_timespec before{};
    eos_rust_timespec wall{};
    eos_rust_timespec after{};
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &before) == 0 &&
               eos_rust_clock_gettime(EOS_RUST_CLOCK_REALTIME, &wall) == 0 &&
               eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &after) == 0 &&
               before.tv_sec == 5 && after.tv_sec == 5 &&
               after.tv_nsec == 1000,
           "realtime reads contaminated monotonic state");
}

void test_deadline_rounding_and_saturation() {
    int32_t expired = -1;
    eos_rust_timespec deadline{1, 1};
    expect(eos_time_test_deadline_ticks(UINT64_C(1000000), &deadline, 1000,
                                        &expired) == 1 &&
               expired == 0,
           "positive sub-tick interval must round to one tick");
    deadline = {1, 1000000};
    expect(eos_time_test_deadline_ticks(UINT64_C(1000000), &deadline, 1000,
                                        &expired) == 1,
           "an exact millisecond must be one tick");
    deadline = {1, 1000001};
    expect(eos_time_test_deadline_ticks(UINT64_C(1000000), &deadline, 1000,
                                        &expired) == 2,
           "tick conversion must use ceiling division");
    deadline = {0, 999999999};
    expect(eos_time_test_deadline_ticks(UINT64_C(1000000), &deadline, 1000,
                                        &expired) == 0 &&
               expired == 1,
           "expired deadline must not request a native wait");
    deadline = {INT64_MAX, 999999999};
    expect(eos_time_test_deadline_ticks(0, &deadline, 1000, &expired) ==
               kMaximumFinite &&
               expired == 0,
           "huge deadline must saturate below wait-forever");
    deadline = {-1, 0};
    expect(eos_time_test_deadline_ticks(0, &deadline, 1000, &expired) == 0 &&
               expired == -1,
           "negative deadline must be rejected");
    deadline = {0, INT64_C(1000000000)};
    expect(eos_time_test_deadline_ticks(0, &deadline, 1000, &expired) == 0 &&
               expired == -1,
           "unnormalized deadline must be rejected");
    deadline = {1, 0};
    expect(eos_time_test_deadline_ticks(0, &deadline, 0, &expired) == 0 &&
               expired == -1,
           "zero tick rate must be rejected");
}

void test_nanosleep_split_rounding_and_remainder() {
    eos_time_test_reset();
    eos_time_test_set_delay_advances_clock(1);
    eos_rust_timespec request{2, 500};
    eos_rust_timespec remaining{7, 9};
    expect(eos_rust_nanosleep(&request, &remaining) == 0 &&
               remaining.tv_sec == 0 && remaining.tv_nsec == 0,
           "nanosleep basic split failed");
    expect(eos_time_test_delay_count() == 1 &&
               eos_time_test_delay_value(0) == 2000 &&
               eos_time_test_delay_usec_count() == 1 &&
               eos_time_test_delay_usec_value(0) == 1,
           "nanosleep must split milliseconds and round sub-microseconds up");

    eos_time_test_reset();
    eos_time_test_set_delay_advances_clock(1);
    request = {INT64_C(5000000), 0};
    expect(eos_rust_nanosleep(&request, nullptr) == 0,
           "long nanosleep chunking failed");
    expect(eos_time_test_delay_count() >= 2 &&
               eos_time_test_delay_value(0) == kMaximumFinite,
           "long nanosleep must use maximum finite chunks, never forever");
    for (uint32_t index = 0; index != eos_time_test_delay_count(); ++index) {
        expect(eos_time_test_delay_value(index) != UINT32_MAX,
               "nanosleep passed wait-forever to os_delay");
    }

    eos_time_test_reset();
    eos_time_test_set_delay_advances_clock(1);
    eos_time_test_fail_delay_after(0, 25);
    request = {3, 0};
    remaining = {-1, -1};
    expect(eos_rust_nanosleep(&request, &remaining) == -1 &&
               *eos_rust_errno_location() == kIo && remaining.tv_sec == 3 &&
               remaining.tv_nsec == 0,
           "delay failure must report a normalized conservative remainder");

    const eos_rust_timespec malformed[] = {
        {-1, 0}, {0, -1}, {0, INT64_C(1000000000)}};
    for (const auto &value : malformed) {
        remaining = {4, 5};
        expect(eos_rust_nanosleep(&value, &remaining) == -1 &&
                   *eos_rust_errno_location() == kInvalid &&
                   remaining.tv_sec == 4 && remaining.tv_nsec == 5,
               "invalid nanosleep request must fail before native delay");
    }

    request = {INT64_MAX, 999999999};
    remaining = {6, 7};
    expect(eos_rust_nanosleep(&request, &remaining) == -1 &&
               *eos_rust_errno_location() == kOverflow &&
               remaining.tv_sec == 6 && remaining.tv_nsec == 7,
           "overflowing relative deadline must fail before native delay");
}

} // namespace

int main() {
    eos_host_test_reset();
    eos_time_test_reset();
    test_clock_layout_and_validation();
    test_monotonic_non_regression_and_concurrency();
    test_realtime_separation_and_errors();
    test_deadline_rounding_and_saturation();
    test_nanosleep_split_rounding_and_remainder();
    return EXIT_SUCCESS;
}
