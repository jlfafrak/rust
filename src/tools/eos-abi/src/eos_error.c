#include "eos_error.h"

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define EOS_RUST_MAYBE_UNUSED __attribute__((unused))
#else
#define EOS_RUST_MAYBE_UNUSED
#endif

enum eos_port_status_code {
    EOS_PORT_STATUS_OK = 0,
    EOS_PORT_STATUS_INVALID_PARAM1 = 1,
    EOS_PORT_STATUS_INVALID_PARAM2 = 2,
    EOS_PORT_STATUS_INVALID_PARAM3 = 3,
    EOS_PORT_STATUS_INVALID_PARAM4 = 4,
    EOS_PORT_STATUS_INVALID_PARAM5 = 5,
    EOS_PORT_STATUS_INVALID_PARAM6 = 6,
    EOS_PORT_STATUS_INVALID_PARAM7 = 7,
    EOS_PORT_STATUS_INVALID_PARAM8 = 8,
    EOS_PORT_STATUS_INVALID_PARAM9 = 9,
    EOS_PORT_STATUS_INVALID_PARAM10 = 10,
    EOS_PORT_STATUS_INVALID_OBJECT_TYPE = 11,
    EOS_PORT_STATUS_OBJECT_NOT_FOUND = 12,
    EOS_PORT_STATUS_OBJECT_EXISTS = 13,
    EOS_PORT_STATUS_NOT_CALLABLE_FROM_ISR = 14,
    EOS_PORT_STATUS_ALLOC_ERROR = 15,
    EOS_PORT_STATUS_INSUFFICIENT_ACL = 16,
    EOS_PORT_STATUS_OBJECT_IN_USE = 17,
    EOS_PORT_STATUS_OBJECT_IS_READ_ONLY = 18,
    EOS_PORT_STATUS_TIMEOUT_EXPIRED = 19,
    EOS_PORT_STATUS_MUTEX_WAS_NOT_LOCKED = 20,
    EOS_PORT_STATUS_WOULD_BLOCK_FROM_ISR = 21,
    EOS_PORT_STATUS_OBJECT_WAS_NOT_TAKEN = 22,
    EOS_PORT_STATUS_MEM_MISALIGNMENT = 23,
    EOS_PORT_STATUS_SYSTEM_NOT_INITIALIZED = 24,
    EOS_PORT_STATUS_DEVICE_ERROR = 25,
    EOS_PORT_STATUS_DEVICE_READ_ERROR = 26,
    EOS_PORT_STATUS_DEVICE_WRITE_ERROR = 27,
    EOS_PORT_STATUS_DEVICE_ERASE_ERROR = 28,
    EOS_PORT_STATUS_PARTITION_ERROR = 29,
    EOS_PORT_STATUS_INVALID_HASH = 30,
    EOS_PORT_STATUS_THREAD_NOT_STARTED = 31,
    EOS_PORT_STATUS_END_OF_OBJECT = 32,
    EOS_PORT_STATUS_SYMBOL_ERROR = 33,
    EOS_PORT_STATUS_PARSE_ERROR = 34,
    EOS_PORT_STATUS_COUNT = 35,
};

static eos_error_result eos_error_result_make(eos_error_kind kind,
                                               int32_t error_number) {
    eos_error_result result = {kind, error_number};
    return result;
}

#ifdef EOS_RUST_DEBUG_ERRORS
static eos_error_debug_record eos_error_last_debug_record;

static void eos_error_debug_record_unknown(int32_t status,
                                           const char *operation) {
    size_t index = 0;
    eos_error_last_debug_record.status = status;
    eos_error_last_debug_record.valid = 1;
    if (operation != NULL) {
        while (operation[index] != '\0' &&
               index + 1 < EOS_ERROR_OPERATION_CAPACITY) {
            eos_error_last_debug_record.operation[index] = operation[index];
            ++index;
        }
    }
    eos_error_last_debug_record.operation[index] = '\0';
}
#else
static void eos_error_debug_record_unknown(int32_t status,
                                           const char *operation) {
    (void)status;
    (void)operation;
}
#endif

static EOS_RUST_MAYBE_UNUSED eos_error_result
eos_error_from_port_status_impl(int32_t status, const char *operation) {
    switch (status) {
    case EOS_PORT_STATUS_OK:
        return eos_error_result_make(EOS_ERROR_NONE, 0);

    case EOS_PORT_STATUS_INVALID_PARAM1:
    case EOS_PORT_STATUS_INVALID_PARAM2:
    case EOS_PORT_STATUS_INVALID_PARAM3:
    case EOS_PORT_STATUS_INVALID_PARAM4:
    case EOS_PORT_STATUS_INVALID_PARAM5:
    case EOS_PORT_STATUS_INVALID_PARAM6:
    case EOS_PORT_STATUS_INVALID_PARAM7:
    case EOS_PORT_STATUS_INVALID_PARAM8:
    case EOS_PORT_STATUS_INVALID_PARAM9:
    case EOS_PORT_STATUS_INVALID_PARAM10:
    case EOS_PORT_STATUS_INVALID_OBJECT_TYPE:
    case EOS_PORT_STATUS_MUTEX_WAS_NOT_LOCKED:
    case EOS_PORT_STATUS_OBJECT_WAS_NOT_TAKEN:
    case EOS_PORT_STATUS_MEM_MISALIGNMENT:
    case EOS_PORT_STATUS_PARSE_ERROR:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_INVALID);

    case EOS_PORT_STATUS_OBJECT_NOT_FOUND:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_NO_ENTRY);
    case EOS_PORT_STATUS_OBJECT_EXISTS:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_EXISTS);
    case EOS_PORT_STATUS_NOT_CALLABLE_FROM_ISR:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_NOT_SUPPORTED);
    case EOS_PORT_STATUS_ALLOC_ERROR:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_NO_MEMORY);
    case EOS_PORT_STATUS_INSUFFICIENT_ACL:
    case EOS_PORT_STATUS_INVALID_HASH:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_ACCESS);
    case EOS_PORT_STATUS_OBJECT_IN_USE:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_BUSY);
    case EOS_PORT_STATUS_OBJECT_IS_READ_ONLY:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_READ_ONLY_FS);
    case EOS_PORT_STATUS_TIMEOUT_EXPIRED:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_TIMED_OUT);
    case EOS_PORT_STATUS_WOULD_BLOCK_FROM_ISR:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_WOULD_BLOCK);

    case EOS_PORT_STATUS_SYSTEM_NOT_INITIALIZED:
    case EOS_PORT_STATUS_DEVICE_ERROR:
    case EOS_PORT_STATUS_DEVICE_READ_ERROR:
    case EOS_PORT_STATUS_DEVICE_WRITE_ERROR:
    case EOS_PORT_STATUS_DEVICE_ERASE_ERROR:
    case EOS_PORT_STATUS_PARTITION_ERROR:
    case EOS_PORT_STATUS_THREAD_NOT_STARTED:
    case EOS_PORT_STATUS_SYMBOL_ERROR:
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_IO);

    case EOS_PORT_STATUS_END_OF_OBJECT:
        return eos_error_result_make(EOS_ERROR_END_OF_OBJECT, 0);

    case EOS_PORT_STATUS_COUNT:
    default:
        eos_error_debug_record_unknown(status, operation);
        return eos_error_result_make(EOS_ERROR_ERRNO, EOS_ERRNO_IO);
    }
}

#ifdef EOS_RUST_TESTING
eos_error_result eos_error_from_port_status(int32_t status) {
    return eos_error_from_port_status_impl(status, NULL);
}

#ifdef EOS_RUST_DEBUG_ERRORS
eos_error_result eos_error_from_port_status_for_operation(
    int32_t status, const char *operation) {
    return eos_error_from_port_status_impl(status, operation);
}

void eos_error_debug_clear(void) {
    eos_error_last_debug_record.status = 0;
    eos_error_last_debug_record.valid = 0;
    eos_error_last_debug_record.operation[0] = '\0';
}

eos_error_debug_record eos_error_debug_last_record(void) {
    return eos_error_last_debug_record;
}
#endif
#endif
