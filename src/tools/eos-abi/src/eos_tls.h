#ifndef EOS_TLS_H
#define EOS_TLS_H

#include <stdint.h>

#define EOS_RUST_TLS_SLOT UINT32_C(7)
#define EOS_RUST_TLS_DESTRUCTOR_PASSES UINT32_C(4)

static int32_t *eos_tls_errno_location(void);
static void eos_tls_set_thread_identity(uint32_t identity);
static uint32_t eos_tls_thread_identity(void);
static void eos_tls_cleanup_current(void);

#endif
