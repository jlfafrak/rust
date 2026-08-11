#include "martos_smp.h"

#include <stdint.h>

/* Guard the numeric translation table against drift in the pinned SDK. */
_Static_assert(OS_STS_OK == 0, "unexpected OS_STS_OK value");
_Static_assert(OS_STS_INVALID_PARAM1 == 1, "unexpected OS_STS_INVALID_PARAM1 value");
_Static_assert(OS_STS_INVALID_PARAM2 == 2, "unexpected OS_STS_INVALID_PARAM2 value");
_Static_assert(OS_STS_INVALID_PARAM3 == 3, "unexpected OS_STS_INVALID_PARAM3 value");
_Static_assert(OS_STS_INVALID_PARAM4 == 4, "unexpected OS_STS_INVALID_PARAM4 value");
_Static_assert(OS_STS_INVALID_PARAM5 == 5, "unexpected OS_STS_INVALID_PARAM5 value");
_Static_assert(OS_STS_INVALID_PARAM6 == 6, "unexpected OS_STS_INVALID_PARAM6 value");
_Static_assert(OS_STS_INVALID_PARAM7 == 7, "unexpected OS_STS_INVALID_PARAM7 value");
_Static_assert(OS_STS_INVALID_PARAM8 == 8, "unexpected OS_STS_INVALID_PARAM8 value");
_Static_assert(OS_STS_INVALID_PARAM9 == 9, "unexpected OS_STS_INVALID_PARAM9 value");
_Static_assert(OS_STS_INVALID_PARAM10 == 10, "unexpected OS_STS_INVALID_PARAM10 value");
_Static_assert(OS_STS_INVALID_OBJECT_TYPE == 11,
               "unexpected OS_STS_INVALID_OBJECT_TYPE value");
_Static_assert(OS_STS_OBJECT_NOT_FOUND == 12,
               "unexpected OS_STS_OBJECT_NOT_FOUND value");
_Static_assert(OS_STS_OBJECT_EXISTS == 13, "unexpected OS_STS_OBJECT_EXISTS value");
_Static_assert(OS_STS_NOT_CALLABLE_FROM_ISR == 14,
               "unexpected OS_STS_NOT_CALLABLE_FROM_ISR value");
_Static_assert(OS_STS_ALLOC_ERROR == 15, "unexpected OS_STS_ALLOC_ERROR value");
_Static_assert(OS_STS_INSUFFICIENT_ACL == 16,
               "unexpected OS_STS_INSUFFICIENT_ACL value");
_Static_assert(OS_STS_OBJECT_IN_USE == 17, "unexpected OS_STS_OBJECT_IN_USE value");
_Static_assert(OS_STS_OBJECT_IS_READ_ONLY == 18,
               "unexpected OS_STS_OBJECT_IS_READ_ONLY value");
_Static_assert(OS_STS_TIMEOUT_EXPIRED == 19,
               "unexpected OS_STS_TIMEOUT_EXPIRED value");
_Static_assert(OS_STS_MUTEX_WAS_NOT_LOCKED == 20,
               "unexpected OS_STS_MUTEX_WAS_NOT_LOCKED value");
_Static_assert(OS_STS_WOULD_BLOCK_FROM_ISR == 21,
               "unexpected OS_STS_WOULD_BLOCK_FROM_ISR value");
_Static_assert(OS_STS_OBJECT_WAS_NOT_TAKEN == 22,
               "unexpected OS_STS_OBJECT_WAS_NOT_TAKEN value");
_Static_assert(OS_STS_MEM_MISALIGNMENT == 23,
               "unexpected OS_STS_MEM_MISALIGNMENT value");
_Static_assert(OS_STS_SYSTEM_NOT_INITIALIZED == 24,
               "unexpected OS_STS_SYSTEM_NOT_INITIALIZED value");
_Static_assert(OS_STS_DEVICE_ERROR == 25, "unexpected OS_STS_DEVICE_ERROR value");
_Static_assert(OS_STS_DEVICE_READ_ERROR == 26,
               "unexpected OS_STS_DEVICE_READ_ERROR value");
_Static_assert(OS_STS_DEVICE_WRITE_ERROR == 27,
               "unexpected OS_STS_DEVICE_WRITE_ERROR value");
_Static_assert(OS_STS_DEVICE_ERASE_ERROR == 28,
               "unexpected OS_STS_DEVICE_ERASE_ERROR value");
_Static_assert(OS_STS_PARTITION_ERROR == 29,
               "unexpected OS_STS_PARTITION_ERROR value");
_Static_assert(OS_STS_INVALID_HASH == 30, "unexpected OS_STS_INVALID_HASH value");
_Static_assert(OS_STS_THREAD_NOT_STARTED == 31,
               "unexpected OS_STS_THREAD_NOT_STARTED value");
_Static_assert(OS_STS_END_OF_OBJECT == 32, "unexpected OS_STS_END_OF_OBJECT value");
_Static_assert(OS_STS_SYMBOL_ERROR == 33, "unexpected OS_STS_SYMBOL_ERROR value");
_Static_assert(OS_STS_PARSE_ERROR == 34, "unexpected OS_STS_PARSE_ERROR value");
_Static_assert(OS_STS_COUNT == 35, "unexpected OS_STS_COUNT value");

/*
 * This bootstrap cell keeps the MARTOS-native dependency inside this port.
 * The TLS task replaces it with the reserved EOS user-TLS slot 7 adapter.
 */
static int32_t eos_martos_bootstrap_errno;

static int32_t *eos_port_errno_location(void) {
    return &eos_martos_bootstrap_errno;
}
