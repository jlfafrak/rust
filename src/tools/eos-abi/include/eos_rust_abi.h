#ifndef EOS_RUST_ABI_H
#define EOS_RUST_ABI_H

#include "eos_rust_abi_version.h"

#include <stdint.h>

#if defined(_WIN32)
#  if defined(EOS_RUST_ABI_BUILD_SHARED)
#    define EOS_RUST_EXPORT __declspec(dllexport)
#  elif defined(EOS_RUST_ABI_USE_SHARED)
#    define EOS_RUST_EXPORT __declspec(dllimport)
#  else
#    define EOS_RUST_EXPORT
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define EOS_RUST_EXPORT __attribute__((visibility("default")))
#else
#  define EOS_RUST_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

EOS_RUST_EXPORT uint32_t eos_rust_abi_version(void);

/*
 * Returns zero when this library implements the requested ABI major and at
 * least minimum_minor. On failure, returns -1 and records EPROTONOSUPPORT for
 * a major mismatch or ENOTSUP for an unavailable minor.
 */
EOS_RUST_EXPORT int32_t eos_rust_abi_require(uint32_t major,
                                            uint32_t minimum_minor);

EOS_RUST_EXPORT int32_t *eos_rust_errno_location(void);

#ifdef __cplusplus
}
#endif

#endif
