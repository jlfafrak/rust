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

#if defined(__cplusplus)
#  define EOS_RUST_NORETURN [[noreturn]]
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define EOS_RUST_NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
#  define EOS_RUST_NORETURN __attribute__((noreturn))
#else
#  define EOS_RUST_NORETURN
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t eos_rust_fd_t;

EOS_RUST_EXPORT uint32_t eos_rust_abi_version(void);

/*
 * Returns zero when this library implements the requested ABI major and at
 * least minimum_minor. On failure, returns -1 and records EPROTONOSUPPORT for
 * a major mismatch or ENOTSUP for an unavailable minor.
 */
EOS_RUST_EXPORT int32_t eos_rust_abi_require(uint32_t major,
                                            uint32_t minimum_minor);

EOS_RUST_EXPORT int32_t *eos_rust_errno_location(void);

/*
 * Allocation byte counts are permanently 32-bit. A zero byte request is
 * normalized to one owned byte. calloc rejects a product above UINT32_MAX,
 * and supported power-of-two alignments are 4 through 4096 bytes. Allocation
 * services are not callable from ISR context.
 */
EOS_RUST_EXPORT void *eos_rust_malloc(uint32_t byte_count);
EOS_RUST_EXPORT void *eos_rust_calloc(uint32_t element_count,
                                      uint32_t element_size);
EOS_RUST_EXPORT void *eos_rust_realloc(void *memory, uint32_t byte_count);
EOS_RUST_EXPORT int32_t eos_rust_posix_memalign(void **memory,
                                                uint32_t alignment,
                                                uint32_t byte_count);
EOS_RUST_EXPORT void eos_rust_free(void *memory);

EOS_RUST_EXPORT EOS_RUST_NORETURN void eos_rust_abort(void);
EOS_RUST_EXPORT EOS_RUST_NORETURN void eos_rust_exit(int32_t status);

/*
 * Initializes process runtime state and console-backed descriptors 0, 1, and
 * 2. Calls are concurrency-safe and idempotent. Failure emits a native direct
 * diagnostic and aborts; this operation is not callable from ISR context.
 */
EOS_RUST_EXPORT void eos_rust_runtime_init(int32_t argc,
                                           const char *const *argv);

/*
 * Returned environment strings and vectors are immutable snapshots owned by
 * this library and remain valid until process termination. Call
 * eos_rust_environ again after a successful update to observe the new vector.
 * Values are limited to 127 bytes plus the terminating null. These environment
 * operations are not callable from ISR context.
 */
EOS_RUST_EXPORT char *eos_rust_getenv(const char *name);
EOS_RUST_EXPORT int32_t eos_rust_setenv(const char *name,
                                        const char *value,
                                        int32_t overwrite);
EOS_RUST_EXPORT int32_t eos_rust_unsetenv(const char *name);
EOS_RUST_EXPORT char **eos_rust_environ(void);

/*
 * Produces process- and request-diversified keys for hash-table state. This
 * service is not callable from ISR context. A null output pointer is ignored;
 * every call still advances the process state.
 */
EOS_RUST_EXPORT void eos_rust_hash_seed(uint64_t *key0, uint64_t *key1);

#ifdef __cplusplus
}
#endif

#endif
