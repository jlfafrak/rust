#ifndef EOS_ERROR_H
#define EOS_ERROR_H

#include <stdint.h>

typedef enum eos_error_kind {
    EOS_ERROR_NONE = 0,
    EOS_ERROR_ERRNO = 1,
    EOS_ERROR_END_OF_OBJECT = 2,
} eos_error_kind;

typedef struct eos_error_result {
    eos_error_kind kind;
    int32_t error_number;
} eos_error_result;

#ifdef EOS_RUST_TESTING
#  ifdef __cplusplus
extern "C" {
#  endif

eos_error_result eos_error_from_port_status(int32_t status);

#  ifdef __cplusplus
}
#  endif
#endif

#endif
