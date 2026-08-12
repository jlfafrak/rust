#ifndef EOS_RUST_ABI_H
#define EOS_RUST_ABI_H

#include "eos_rust_abi_version.h"

#include <stdint.h>

typedef int32_t eos_rust_fd_t;

typedef struct eos_rust_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    int64_t st_atime_sec;
    int64_t st_atime_nsec;
    int64_t st_mtime_sec;
    int64_t st_mtime_nsec;
    int64_t st_ctime_sec;
    int64_t st_ctime_nsec;
    uint64_t st_blocks;
    uint32_t st_blksize;
    uint32_t reserved[7];
} eos_rust_stat;

#define EOS_RUST_PATH_MAX UINT32_C(256)
#define EOS_RUST_NAME_MAX UINT32_C(63)

typedef struct eos_rust_dirent {
    uint64_t d_ino;
    uint32_t d_type;
    uint32_t d_name_length;
    char d_name[64];
} eos_rust_dirent;

/*
 * Paths and copied directory names include their terminating null within the
 * fixed capacities. Directory iteration returns 1 for an entry, 0 at EOF,
 * and -1 on failure. eos_rust_stat and eos_rust_dirent are stable v1 layouts;
 * they never contain a native EOS or host structure.
 */

#define EOS_RUST_DT_UNKNOWN UINT32_C(0)
#define EOS_RUST_DT_DIR UINT32_C(4)
#define EOS_RUST_DT_REG UINT32_C(8)

#define EOS_RUST_O_RDONLY UINT32_C(0)
#define EOS_RUST_O_WRONLY UINT32_C(1)
#define EOS_RUST_O_RDWR UINT32_C(2)
#define EOS_RUST_O_ACCMODE UINT32_C(3)
#define EOS_RUST_O_CREAT UINT32_C(0x40)
#define EOS_RUST_O_EXCL UINT32_C(0x80)
#define EOS_RUST_O_TRUNC UINT32_C(0x200)
#define EOS_RUST_O_APPEND UINT32_C(0x400)
#define EOS_RUST_O_NONBLOCK UINT32_C(0x800)
#define EOS_RUST_O_CLOEXEC UINT32_C(0x80000)

#define EOS_RUST_F_DUPFD INT32_C(0)
#define EOS_RUST_F_GETFD INT32_C(1)
#define EOS_RUST_F_SETFD INT32_C(2)
#define EOS_RUST_F_GETFL INT32_C(3)
#define EOS_RUST_F_SETFL INT32_C(4)
#define EOS_RUST_F_DUPFD_CLOEXEC INT32_C(1030)
#define EOS_RUST_FD_CLOEXEC INT32_C(1)

#define EOS_RUST_SEEK_SET INT32_C(0)
#define EOS_RUST_SEEK_CUR INT32_C(1)
#define EOS_RUST_SEEK_END INT32_C(2)

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
 * File operations use nonnegative byte counts or offsets for success and -1
 * with compatibility errno for failure. A partial transfer outranks a later
 * native error. realpath/getcwd return zero on success; a short buffer is
 * terminated, contains the prefix that fits, and reports ERANGE. gethostname
 * follows the same termination rule and reports ENAMETOOLONG on truncation.
 */
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_open(const char *path,
                                             uint32_t flags,
                                             uint32_t mode);
EOS_RUST_EXPORT int32_t eos_rust_close(eos_rust_fd_t descriptor);
EOS_RUST_EXPORT int32_t eos_rust_read(eos_rust_fd_t descriptor,
                                      void *buffer,
                                      uint32_t byte_count);
EOS_RUST_EXPORT int32_t eos_rust_write(eos_rust_fd_t descriptor,
                                       const void *buffer,
                                       uint32_t byte_count);
EOS_RUST_EXPORT int32_t eos_rust_pread(eos_rust_fd_t descriptor,
                                       void *buffer,
                                       uint32_t byte_count,
                                       int64_t offset);
EOS_RUST_EXPORT int32_t eos_rust_pwrite(eos_rust_fd_t descriptor,
                                        const void *buffer,
                                        uint32_t byte_count,
                                        int64_t offset);
EOS_RUST_EXPORT int64_t eos_rust_lseek(eos_rust_fd_t descriptor,
                                       int64_t offset,
                                       int32_t origin);
EOS_RUST_EXPORT int32_t eos_rust_fsync(eos_rust_fd_t descriptor);
EOS_RUST_EXPORT int32_t eos_rust_fstat(eos_rust_fd_t descriptor,
                                       eos_rust_stat *metadata);
EOS_RUST_EXPORT int32_t eos_rust_stat_path(const char *path,
                                           eos_rust_stat *metadata);
EOS_RUST_EXPORT int32_t eos_rust_lstat(const char *path,
                                       eos_rust_stat *metadata);
EOS_RUST_EXPORT int32_t eos_rust_mkdir(const char *path, uint32_t mode);
EOS_RUST_EXPORT int32_t eos_rust_unlink(const char *path);
EOS_RUST_EXPORT int32_t eos_rust_rmdir(const char *path);
EOS_RUST_EXPORT int32_t eos_rust_rename(const char *old_path,
                                        const char *new_path);
EOS_RUST_EXPORT int32_t eos_rust_realpath(const char *path,
                                          char *resolved,
                                          uint32_t capacity);
EOS_RUST_EXPORT int32_t eos_rust_getcwd(char *buffer, uint32_t capacity);
EOS_RUST_EXPORT int32_t eos_rust_chdir(const char *path);
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_opendir(const char *path);
EOS_RUST_EXPORT int32_t eos_rust_readdir(eos_rust_fd_t directory,
                                         eos_rust_dirent *entry);
EOS_RUST_EXPORT int32_t eos_rust_closedir(eos_rust_fd_t directory);
EOS_RUST_EXPORT int32_t eos_rust_fcntl(eos_rust_fd_t descriptor,
                                       int32_t command,
                                       int32_t argument);
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_dup(eos_rust_fd_t descriptor);
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_dup2(eos_rust_fd_t old_descriptor,
                                            eos_rust_fd_t new_descriptor);
EOS_RUST_EXPORT int32_t eos_rust_pipe(eos_rust_fd_t descriptors[2],
                                      uint32_t flags);
EOS_RUST_EXPORT int32_t eos_rust_isatty(eos_rust_fd_t descriptor);
EOS_RUST_EXPORT int32_t eos_rust_gethostname(char *name, uint32_t capacity);
EOS_RUST_EXPORT int32_t eos_rust_symlink(const char *target,
                                         const char *link_path);
EOS_RUST_EXPORT int32_t eos_rust_link(const char *old_path,
                                      const char *new_path);
EOS_RUST_EXPORT int32_t eos_rust_chown(const char *path,
                                       uint32_t owner,
                                       uint32_t group);
EOS_RUST_EXPORT int32_t eos_rust_lchown(const char *path,
                                        uint32_t owner,
                                        uint32_t group);
EOS_RUST_EXPORT int32_t eos_rust_fchown(eos_rust_fd_t descriptor,
                                        uint32_t owner,
                                        uint32_t group);
EOS_RUST_EXPORT int32_t eos_rust_chmod(const char *path, uint32_t mode);
EOS_RUST_EXPORT int32_t eos_rust_fchmod(eos_rust_fd_t descriptor,
                                        uint32_t mode);

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
