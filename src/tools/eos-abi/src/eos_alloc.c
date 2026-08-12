#include <stdint.h>
#include <string.h>

#define EOS_RUST_MIN_ALLOCATION UINT32_C(1)
#define EOS_RUST_MIN_ALIGNMENT UINT32_C(4)
#define EOS_RUST_MAX_ALIGNMENT UINT32_C(4096)

static uint32_t eos_allocation_size(uint32_t byte_count) {
    return byte_count == 0 ? EOS_RUST_MIN_ALLOCATION : byte_count;
}

static void eos_allocation_record_failure(int32_t status,
                                          const char *operation) {
    const eos_error_result error =
        eos_error_from_port_status_impl(status, operation);
    *eos_tls_errno_location() =
        error.kind == EOS_ERROR_ERRNO ? error.error_number : EOS_ERRNO_IO;
}

void *eos_rust_malloc(uint32_t byte_count) {
    void *memory = NULL;
    const int32_t status =
        eos_port_memory_alloc(eos_allocation_size(byte_count), &memory);
    if (status != EOS_PORT_STATUS_OK) {
        eos_allocation_record_failure(status, "memory.allocate");
        return NULL;
    }
    return memory;
}

void *eos_rust_calloc(uint32_t element_count, uint32_t element_size) {
    uint32_t byte_count;
    void *memory;

    if (element_count != 0 &&
        element_size > UINT32_MAX / element_count) {
        *eos_tls_errno_location() = EOS_ERRNO_NO_MEMORY;
        return NULL;
    }
    byte_count = element_count * element_size;
    memory = eos_rust_malloc(byte_count);
    if (memory != NULL) {
        (void)memset(memory, 0, (size_t)eos_allocation_size(byte_count));
    }
    return memory;
}

void *eos_rust_realloc(void *memory, uint32_t byte_count) {
    void *resized = memory;
    int32_t status;

    if (memory == NULL) {
        return eos_rust_malloc(byte_count);
    }
    status = eos_port_memory_realloc(eos_allocation_size(byte_count), &resized);
    if (status != EOS_PORT_STATUS_OK) {
        eos_allocation_record_failure(status, "memory.reallocate");
        return NULL;
    }
    return resized;
}

int32_t eos_rust_posix_memalign(void **memory,
                                uint32_t alignment,
                                uint32_t byte_count) {
    int32_t status;
    eos_error_result error;

    if (memory == NULL) {
        return EOS_ERRNO_INVALID;
    }
    *memory = NULL;
    if (alignment < EOS_RUST_MIN_ALIGNMENT ||
        alignment > EOS_RUST_MAX_ALIGNMENT ||
        (alignment & (alignment - UINT32_C(1))) != 0) {
        return EOS_ERRNO_INVALID;
    }

    status = eos_port_memory_alloc_aligned(eos_allocation_size(byte_count),
                                           alignment,
                                           memory);
    if (status == EOS_PORT_STATUS_OK) {
        return 0;
    }
    *memory = NULL;
    error = eos_error_from_port_status_impl(status, "memory.allocate_aligned");
    return error.kind == EOS_ERROR_ERRNO ? error.error_number : EOS_ERRNO_IO;
}

void eos_rust_free(void *memory) {
    int32_t status;
    if (memory == NULL) {
        return;
    }
    status = eos_port_memory_free(memory);
    if (status != EOS_PORT_STATUS_OK) {
        eos_allocation_record_failure(status, "memory.free");
    }
}
