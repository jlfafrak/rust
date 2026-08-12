#include "eos_rust_abi.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_alloc(int32_t status);
void eos_host_test_fail_next_aligned_alloc(int32_t status);
void eos_host_test_fail_next_realloc(int32_t status);
}

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

void test_zero_size_allocations_are_owned_and_freeable() {
    void *allocated = eos_rust_malloc(UINT32_C(0));
    expect(allocated != nullptr, "zero-size malloc must return owned storage");
    static_cast<unsigned char *>(allocated)[0] = 0xa5;
    eos_rust_free(allocated);

    void *zeroed = eos_rust_calloc(UINT32_C(0), UINT32_C(31));
    expect(zeroed != nullptr, "zero-count calloc must return owned storage");
    expect(static_cast<unsigned char *>(zeroed)[0] == 0,
           "zero-count calloc storage must be zero initialized");
    eos_rust_free(zeroed);

    void *resized = eos_rust_realloc(nullptr, UINT32_C(0));
    expect(resized != nullptr, "zero-size realloc must return owned storage");
    static_cast<unsigned char *>(resized)[0] = 0x5a;
    eos_rust_free(resized);

    eos_rust_free(nullptr);
}

void test_calloc_rejects_32_bit_product_overflow() {
    constexpr uint32_t element_count = UINT32_C(37);
    constexpr uint32_t element_size = UINT32_C(11);
    auto *zeroed = static_cast<unsigned char *>(
        eos_rust_calloc(element_count, element_size));
    expect(zeroed != nullptr, "positive-size calloc must allocate storage");
    for (uint32_t index = 0; index < element_count * element_size; ++index) {
        if (zeroed[index] != 0) {
            fail("calloc must zero every byte in the requested product");
        }
    }
    eos_rust_free(zeroed);

    *eos_rust_errno_location() = 91;
    void *memory = eos_rust_calloc(std::numeric_limits<uint32_t>::max(),
                                   UINT32_C(2));
    expect(memory == nullptr, "overflowing calloc must fail");
    expect(*eos_rust_errno_location() == 12,
           "overflowing calloc must report EOS ENOMEM (12)");
}

void test_supported_alignments_are_honored() {
    for (uint32_t alignment = UINT32_C(4); alignment <= UINT32_C(4096);
         alignment *= UINT32_C(2)) {
        void *memory = nullptr;
        const int32_t result =
            eos_rust_posix_memalign(&memory, alignment, alignment + UINT32_C(17));
        expect(result == 0, "supported aligned allocation must succeed");
        expect(memory != nullptr, "aligned allocation must return storage");
        expect(reinterpret_cast<uintptr_t>(memory) % alignment == 0,
               "aligned allocation returned a misaligned address");
        auto *bytes = static_cast<unsigned char *>(memory);
        bytes[0] = 0x1a;
        bytes[alignment + UINT32_C(16)] = 0xb2;
        eos_rust_free(memory);
    }

    void *memory = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    expect(eos_rust_posix_memalign(&memory, UINT32_C(3), UINT32_C(8)) == 22,
           "non-power-of-two alignment must return EOS EINVAL (22)");
    expect(memory == nullptr, "failed aligned allocation must clear its output");

    memory = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    expect(eos_rust_posix_memalign(&memory, UINT32_C(8192), UINT32_C(8)) == 22,
           "alignment above 4096 must return EOS EINVAL (22)");
    expect(memory == nullptr, "oversized alignment failure must clear its output");

    expect(eos_rust_posix_memalign(nullptr, UINT32_C(8), UINT32_C(8)) == 22,
           "null aligned-allocation output must return EOS EINVAL (22)");
}

void test_realloc_preserves_the_existing_prefix() {
    constexpr uint32_t old_size = UINT32_C(73);
    constexpr uint32_t new_size = UINT32_C(409);
    auto *memory = static_cast<unsigned char *>(eos_rust_malloc(old_size));
    expect(memory != nullptr, "realloc fixture allocation failed");
    for (uint32_t index = 0; index < old_size; ++index) {
        memory[index] = static_cast<unsigned char>((index * UINT32_C(17)) & 0xff);
    }

    auto *resized =
        static_cast<unsigned char *>(eos_rust_realloc(memory, new_size));
    expect(resized != nullptr, "growing realloc must succeed");
    for (uint32_t index = 0; index < old_size; ++index) {
        const auto expected =
            static_cast<unsigned char>((index * UINT32_C(17)) & 0xff);
        if (resized[index] != expected) {
            fail("realloc did not preserve the existing prefix");
        }
    }
    eos_rust_free(resized);
}

void test_successful_operations_preserve_errno() {
    *eos_rust_errno_location() = 73;
    void *memory = eos_rust_malloc(UINT32_C(16));
    expect(memory != nullptr, "errno-preservation allocation failed");
    expect(*eos_rust_errno_location() == 73,
           "successful malloc must preserve errno");
    eos_rust_free(memory);
    expect(*eos_rust_errno_location() == 73,
           "successful free must preserve errno");
}

void test_native_allocation_failures_preserve_contracts() {
    eos_host_test_reset();
    eos_host_test_fail_next_alloc(INT32_C(15));
    expect(eos_rust_malloc(UINT32_C(8)) == nullptr,
           "native malloc failure must return null");
    expect(*eos_rust_errno_location() == 12,
           "native malloc failure must report EOS ENOMEM (12)");

    eos_host_test_fail_next_alloc(INT32_C(15));
    expect(eos_rust_calloc(UINT32_C(2), UINT32_C(8)) == nullptr,
           "native calloc allocation failure must return null");
    expect(*eos_rust_errno_location() == 12,
           "native calloc failure must report EOS ENOMEM (12)");

    void *aligned = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    eos_host_test_fail_next_aligned_alloc(INT32_C(15));
    expect(eos_rust_posix_memalign(&aligned, UINT32_C(64), UINT32_C(8)) == 12,
           "native aligned allocation failure must return EOS ENOMEM (12)");
    expect(aligned == nullptr, "failed aligned allocation must clear output");

    auto *original = static_cast<unsigned char *>(eos_rust_malloc(UINT32_C(16)));
    expect(original != nullptr, "failed-realloc fixture allocation failed");
    std::memset(original, 0x6d, 16);
    eos_host_test_fail_next_realloc(INT32_C(15));
    expect(eos_rust_realloc(original, UINT32_C(64)) == nullptr,
           "native realloc failure must return null");
    expect(*eos_rust_errno_location() == 12,
           "native realloc failure must report EOS ENOMEM (12)");
    for (uint32_t index = 0; index < UINT32_C(16); ++index) {
        expect(original[index] == 0x6d,
               "failed realloc must preserve usable original storage");
    }
    eos_rust_free(original);

    original = static_cast<unsigned char *>(eos_rust_malloc(UINT32_C(4)));
    expect(original != nullptr, "zero-realloc fixture allocation failed");
    original[0] = 0x31;
    auto *zero_resized =
        static_cast<unsigned char *>(eos_rust_realloc(original, UINT32_C(0)));
    expect(zero_resized != nullptr,
           "realloc(nonnull, 0) must return one owned byte");
    zero_resized[0] = 0x42;
    eos_rust_free(zero_resized);
}

} // namespace

int main() {
    test_zero_size_allocations_are_owned_and_freeable();
    test_calloc_rejects_32_bit_product_overflow();
    test_supported_alignments_are_honored();
    test_realloc_preserves_the_existing_prefix();
    test_successful_operations_preserve_errno();
    test_native_allocation_failures_preserve_contracts();
    return EXIT_SUCCESS;
}
