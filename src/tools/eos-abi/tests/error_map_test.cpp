#include "eos_error.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

struct ErrorCase {
    int32_t status;
    eos_error_kind kind;
    int32_t error_number;
    const char *name;
};

[[noreturn]] void fail(const ErrorCase &test_case, eos_error_result actual) {
    std::cerr << test_case.name << ": expected kind " << test_case.kind
              << " and errno " << test_case.error_number << ", got kind "
              << actual.kind << " and errno " << actual.error_number << '\n';
    std::exit(EXIT_FAILURE);
}

void test_every_martos_14_0_39_status() {
    // These literal status values are the MARTOS-SMP 14.0.39 ABI values from
    // OS_STS_OK through OS_STS_PARSE_ERROR. Keeping native names out of this
    // test protects the private port boundary.
    const ErrorCase cases[] = {
        {0, EOS_ERROR_NONE, 0, "ok"},
        {1, EOS_ERROR_ERRNO, 22, "invalid parameter 1"},
        {2, EOS_ERROR_ERRNO, 22, "invalid parameter 2"},
        {3, EOS_ERROR_ERRNO, 22, "invalid parameter 3"},
        {4, EOS_ERROR_ERRNO, 22, "invalid parameter 4"},
        {5, EOS_ERROR_ERRNO, 22, "invalid parameter 5"},
        {6, EOS_ERROR_ERRNO, 22, "invalid parameter 6"},
        {7, EOS_ERROR_ERRNO, 22, "invalid parameter 7"},
        {8, EOS_ERROR_ERRNO, 22, "invalid parameter 8"},
        {9, EOS_ERROR_ERRNO, 22, "invalid parameter 9"},
        {10, EOS_ERROR_ERRNO, 22, "invalid parameter 10"},
        {11, EOS_ERROR_ERRNO, 22, "invalid object type"},
        {12, EOS_ERROR_ERRNO, 2, "object not found"},
        {13, EOS_ERROR_ERRNO, 17, "object exists"},
        {14, EOS_ERROR_ERRNO, 45, "not callable from ISR"},
        {15, EOS_ERROR_ERRNO, 12, "allocation error"},
        {16, EOS_ERROR_ERRNO, 13, "insufficient ACL"},
        {17, EOS_ERROR_ERRNO, 16, "object in use"},
        {18, EOS_ERROR_ERRNO, 30, "object is read-only"},
        {19, EOS_ERROR_ERRNO, 60, "timeout expired"},
        {20, EOS_ERROR_ERRNO, 22, "mutex was not locked"},
        {21, EOS_ERROR_ERRNO, 35, "would block from ISR"},
        {22, EOS_ERROR_ERRNO, 22, "object was not taken"},
        {23, EOS_ERROR_ERRNO, 22, "memory misalignment"},
        {24, EOS_ERROR_ERRNO, 5, "system not initialized"},
        {25, EOS_ERROR_ERRNO, 5, "device error"},
        {26, EOS_ERROR_ERRNO, 5, "device read error"},
        {27, EOS_ERROR_ERRNO, 5, "device write error"},
        {28, EOS_ERROR_ERRNO, 5, "device erase error"},
        {29, EOS_ERROR_ERRNO, 5, "partition error"},
        {30, EOS_ERROR_ERRNO, 13, "invalid authentication hash"},
        {31, EOS_ERROR_ERRNO, 5, "thread not started"},
        {32, EOS_ERROR_END_OF_OBJECT, 0, "end of object"},
        {33, EOS_ERROR_ERRNO, 5, "symbol error"},
        {34, EOS_ERROR_ERRNO, 22, "parse error"},
    };

    for (const ErrorCase &test_case : cases) {
        const eos_error_result actual = eos_error_from_port_status(test_case.status);
        if (actual.kind != test_case.kind ||
            actual.error_number != test_case.error_number) {
            fail(test_case, actual);
        }
    }
}

void test_count_and_unknown_values_map_to_eio() {
    const int32_t invalid_values[] = {35, -1, 36, INT32_MAX};

    for (int32_t value : invalid_values) {
        const eos_error_result actual = eos_error_from_port_status(value);
        if (actual.kind != EOS_ERROR_ERRNO || actual.error_number != 5) {
            const ErrorCase test_case{value, EOS_ERROR_ERRNO, 5,
                                      "count or unknown status"};
            fail(test_case, actual);
        }
    }
}

void test_debug_record_preserves_unknown_status_and_operation() {
    eos_error_debug_clear();
    (void)eos_error_from_port_status_for_operation(0, "known.operation");
    eos_error_debug_record record = eos_error_debug_last_record();
    if (record.valid != 0) {
        std::cerr << "known status unexpectedly created a debug record\n";
        std::exit(EXIT_FAILURE);
    }

    const eos_error_result actual =
        eos_error_from_port_status_for_operation(-77, "filesystem.read");
    if (actual.kind != EOS_ERROR_ERRNO || actual.error_number != 5) {
        std::cerr << "debug mapping changed release unknown-status behavior\n";
        std::exit(EXIT_FAILURE);
    }

    record = eos_error_debug_last_record();
    if (record.valid != 1 || record.status != -77 ||
        std::strcmp(record.operation, "filesystem.read") != 0) {
        std::cerr << "debug record did not preserve status and operation\n";
        std::exit(EXIT_FAILURE);
    }
}

struct InterleaveContext {
    std::atomic<int> calls{0};
    std::atomic<bool> first_paused{false};
    std::atomic<bool> release_first{false};
};

void pause_first_debug_writer(void *opaque) {
    auto *context = static_cast<InterleaveContext *>(opaque);
    if (context->calls.fetch_add(1, std::memory_order_acq_rel) != 0) {
        return;
    }
    context->first_paused.store(true, std::memory_order_release);
    while (!context->release_first.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void test_concurrent_debug_records_are_coherent_and_nonblocking() {
    eos_error_debug_clear();
    InterleaveContext context;
    eos_error_debug_set_interleave_hook(pause_first_debug_writer, &context);

    std::thread first([] {
        (void)eos_error_from_port_status_for_operation(-77, "first.operation");
    });
    while (!context.first_paused.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::atomic<bool> second_done{false};
    std::thread second([&] {
        (void)eos_error_from_port_status_for_operation(-88, "second.operation");
        second_done.store(true, std::memory_order_release);
    });
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!second_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool second_blocked = !second_done.load(std::memory_order_acquire);

    context.release_first.store(true, std::memory_order_release);
    first.join();
    second.join();
    eos_error_debug_set_interleave_hook(nullptr, nullptr);

    if (second_blocked) {
        std::cerr << "debug recording blocked behind a preempted writer\n";
        std::exit(EXIT_FAILURE);
    }

    const eos_error_debug_record record = eos_error_debug_last_record();
    const bool coherent_first =
        record.status == -77 &&
        std::strcmp(record.operation, "first.operation") == 0;
    const bool coherent_second =
        record.status == -88 &&
        std::strcmp(record.operation, "second.operation") == 0;
    if (!coherent_first && !coherent_second) {
        std::cerr << "concurrent debug writers produced a torn record: status "
                  << record.status << ", operation " << record.operation << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    test_every_martos_14_0_39_status();
    test_count_and_unknown_values_map_to_eio();
    test_debug_record_preserves_unknown_status_and_operation();
    test_concurrent_debug_records_are_coherent_and_nonblocking();
    return EXIT_SUCCESS;
}
