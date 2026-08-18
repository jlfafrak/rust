#ifndef EOS_PROCESS_H
#define EOS_PROCESS_H

#include <stdint.h>

#ifdef EOS_RUST_HOST_TEST
#  ifdef __cplusplus
extern "C" {
#  endif
uint32_t eos_process_test_live_records(void);
#  ifdef __cplusplus
}
#  endif
#endif

#endif
