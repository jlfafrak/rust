#ifndef EOS_ERROR_H
#define EOS_ERROR_H

#include <stdint.h>

/* Stable errno values from the EOS v1 libc ABI, independent of the build host. */
typedef enum eos_errno_value {
    EOS_ERRNO_NO_ENTRY = 2,
    EOS_ERRNO_NO_PROCESS = 3,
    EOS_ERRNO_INTERRUPTED = 4,
    EOS_ERRNO_IO = 5,
    EOS_ERRNO_BAD_DESCRIPTOR = 9,
    EOS_ERRNO_DEADLOCK = 11,
    EOS_ERRNO_NO_MEMORY = 12,
    EOS_ERRNO_ACCESS = 13,
    EOS_ERRNO_FAULT = 14,
    EOS_ERRNO_BUSY = 16,
    EOS_ERRNO_EXISTS = 17,
    EOS_ERRNO_NOT_DIRECTORY = 20,
    EOS_ERRNO_IS_DIRECTORY = 21,
    EOS_ERRNO_INVALID = 22,
    EOS_ERRNO_TOO_MANY_OPEN_FILES = 24,
    EOS_ERRNO_NOT_TTY = 25,
    EOS_ERRNO_NO_SPACE = 28,
    EOS_ERRNO_ILLEGAL_SEEK = 29,
    EOS_ERRNO_READ_ONLY_FS = 30,
    EOS_ERRNO_PIPE = 32,
    EOS_ERRNO_RANGE = 34,
    EOS_ERRNO_WOULD_BLOCK = 35,
    EOS_ERRNO_IN_PROGRESS = 36,
    EOS_ERRNO_ALREADY = 37,
    EOS_ERRNO_NOT_SOCKET = 38,
    EOS_ERRNO_DESTINATION_REQUIRED = 39,
    EOS_ERRNO_MESSAGE_SIZE = 40,
    EOS_ERRNO_PROTOCOL_TYPE = 41,
    EOS_ERRNO_NO_PROTOCOL_OPTION = 42,
    EOS_ERRNO_PROTOCOL_NOT_SUPPORTED = 43,
    EOS_ERRNO_NOT_SUPPORTED = 45,
    EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED = 47,
    EOS_ERRNO_ADDRESS_IN_USE = 48,
    EOS_ERRNO_ADDRESS_NOT_AVAILABLE = 49,
    EOS_ERRNO_NETWORK_DOWN = 50,
    EOS_ERRNO_NETWORK_UNREACHABLE = 51,
    EOS_ERRNO_NETWORK_RESET = 52,
    EOS_ERRNO_CONNECTION_ABORTED = 53,
    EOS_ERRNO_CONNECTION_RESET = 54,
    EOS_ERRNO_NO_BUFFERS = 55,
    EOS_ERRNO_IS_CONNECTED = 56,
    EOS_ERRNO_NOT_CONNECTED = 57,
    EOS_ERRNO_SHUTDOWN = 58,
    EOS_ERRNO_TIMED_OUT = 60,
    EOS_ERRNO_CONNECTION_REFUSED = 61,
    EOS_ERRNO_NAME_TOO_LONG = 63,
    EOS_ERRNO_HOST_DOWN = 64,
    EOS_ERRNO_HOST_UNREACHABLE = 65,
    EOS_ERRNO_NOT_EMPTY = 66,
    EOS_ERRNO_OVERFLOW = 84,
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
typedef void (*eos_error_debug_interleave_hook)(void *context);

eos_error_result eos_error_from_port_status_for_operation(int32_t status,
                                                          const char *operation);
void eos_error_debug_clear(void);
eos_error_debug_record eos_error_debug_last_record(void);
void eos_error_debug_set_interleave_hook(eos_error_debug_interleave_hook hook,
                                         void *context);
#  endif

#  ifdef __cplusplus
}
#  endif
#endif

#endif
