#ifndef EOS_ERROR_H
#define EOS_ERROR_H

#include <stdint.h>

/* Stable errno values from the EOS v1 libc ABI, independent of the build host. */
typedef enum eos_errno_value {
    EOS_ERRNO_NO_ENTRY = 2,
    EOS_ERRNO_IO = 5,
    EOS_ERRNO_NO_MEMORY = 12,
    EOS_ERRNO_ACCESS = 13,
    EOS_ERRNO_BUSY = 16,
    EOS_ERRNO_EXISTS = 17,
    EOS_ERRNO_INVALID = 22,
    EOS_ERRNO_READ_ONLY_FS = 30,
    EOS_ERRNO_WOULD_BLOCK = 35,
    EOS_ERRNO_PROTOCOL_NOT_SUPPORTED = 43,
    EOS_ERRNO_NOT_SUPPORTED = 45,
    EOS_ERRNO_TIMED_OUT = 60,
} eos_errno_value;

typedef enum eos_error_kind {
    EOS_ERROR_NONE = 0,
    EOS_ERROR_ERRNO = 1,
    EOS_ERROR_END_OF_OBJECT = 2,
} eos_error_kind;

typedef struct eos_error_result {
    eos_error_kind kind;
    int32_t error_number;
} eos_error_result;

#ifdef EOS_RUST_DEBUG_ERRORS
#define EOS_ERROR_OPERATION_CAPACITY 64

typedef struct eos_error_debug_record {
    int32_t status;
    uint8_t valid;
    char operation[EOS_ERROR_OPERATION_CAPACITY];
} eos_error_debug_record;
#endif

#ifdef EOS_RUST_TESTING
#  ifdef __cplusplus
extern "C" {
#  endif

eos_error_result eos_error_from_port_status(int32_t status);

#  ifdef EOS_RUST_DEBUG_ERRORS
eos_error_result eos_error_from_port_status_for_operation(int32_t status,
                                                          const char *operation);
void eos_error_debug_clear(void);
eos_error_debug_record eos_error_debug_last_record(void);
#  endif

#  ifdef __cplusplus
}
#  endif
#endif

#endif
