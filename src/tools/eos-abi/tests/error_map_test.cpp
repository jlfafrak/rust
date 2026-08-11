#include "eos_error.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>

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
        {1, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 1"},
        {2, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 2"},
        {3, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 3"},
        {4, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 4"},
        {5, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 5"},
        {6, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 6"},
        {7, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 7"},
        {8, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 8"},
        {9, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 9"},
        {10, EOS_ERROR_ERRNO, EINVAL, "invalid parameter 10"},
        {11, EOS_ERROR_ERRNO, EINVAL, "invalid object type"},
        {12, EOS_ERROR_ERRNO, ENOENT, "object not found"},
        {13, EOS_ERROR_ERRNO, EEXIST, "object exists"},
        {14, EOS_ERROR_ERRNO, ENOTSUP, "not callable from ISR"},
        {15, EOS_ERROR_ERRNO, ENOMEM, "allocation error"},
        {16, EOS_ERROR_ERRNO, EACCES, "insufficient ACL"},
        {17, EOS_ERROR_ERRNO, EBUSY, "object in use"},
        {18, EOS_ERROR_ERRNO, EROFS, "object is read-only"},
        {19, EOS_ERROR_ERRNO, ETIMEDOUT, "timeout expired"},
        {20, EOS_ERROR_ERRNO, EINVAL, "mutex was not locked"},
        {21, EOS_ERROR_ERRNO, EWOULDBLOCK, "would block from ISR"},
        {22, EOS_ERROR_ERRNO, EINVAL, "object was not taken"},
        {23, EOS_ERROR_ERRNO, EINVAL, "memory misalignment"},
        {24, EOS_ERROR_ERRNO, EIO, "system not initialized"},
        {25, EOS_ERROR_ERRNO, EIO, "device error"},
        {26, EOS_ERROR_ERRNO, EIO, "device read error"},
        {27, EOS_ERROR_ERRNO, EIO, "device write error"},
        {28, EOS_ERROR_ERRNO, EIO, "device erase error"},
        {29, EOS_ERROR_ERRNO, EIO, "partition error"},
        {30, EOS_ERROR_ERRNO, EACCES, "invalid authentication hash"},
        {31, EOS_ERROR_ERRNO, EIO, "thread not started"},
        {32, EOS_ERROR_END_OF_OBJECT, 0, "end of object"},
        {33, EOS_ERROR_ERRNO, EIO, "symbol error"},
        {34, EOS_ERROR_ERRNO, EINVAL, "parse error"},
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
        if (actual.kind != EOS_ERROR_ERRNO || actual.error_number != EIO) {
            const ErrorCase test_case{value, EOS_ERROR_ERRNO, EIO,
                                      "count or unknown status"};
            fail(test_case, actual);
        }
    }
}

} // namespace

int main() {
    test_every_martos_14_0_39_status();
    test_count_and_unknown_values_map_to_eio();
    return EXIT_SUCCESS;
}
