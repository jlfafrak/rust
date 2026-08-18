#ifndef EOS_RUST_ABI_H
#define EOS_RUST_ABI_H

#include "eos_rust_abi_version.h"

#include <stdint.h>

typedef int32_t eos_rust_fd_t;
typedef uint32_t eos_rust_thread_t;
typedef uint32_t eos_rust_tls_key_t;
typedef uint32_t eos_rust_process_t;

#define EOS_RUST_PROCESS_API_DEFINED UINT32_C(1)
#define EOS_RUST_PROCESS_EXITED UINT32_C(1)
#define EOS_RUST_PROCESS_TERMINATED UINT32_C(2)

typedef struct eos_rust_spawn_request {
    const char *program;
    const char *const *argv;
    uint32_t argc;
    const char *const *envp;
    uint32_t envc;
    const char *cwd;
    eos_rust_fd_t stdin_fd;
    eos_rust_fd_t stdout_fd;
    eos_rust_fd_t stderr_fd;
    uint32_t flags;
    uint32_t reserved[7];
} eos_rust_spawn_request;

typedef struct eos_rust_process_status {
    uint32_t kind;
    int32_t code;
    uint32_t reserved[6];
} eos_rust_process_status;

typedef struct eos_rust_pthread_attr {
    uint32_t words[4];
} eos_rust_pthread_attr;

typedef struct eos_rust_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} eos_rust_timespec;

typedef struct eos_rust_pthread_mutex {
    uint32_t words[4];
} eos_rust_pthread_mutex;

typedef struct eos_rust_pthread_mutexattr {
    uint32_t words[2];
} eos_rust_pthread_mutexattr;

typedef struct eos_rust_pthread_cond {
    uint32_t words[4];
} eos_rust_pthread_cond;

typedef struct eos_rust_pthread_condattr {
    uint32_t words[2];
} eos_rust_pthread_condattr;

typedef struct eos_rust_pthread_rwlock {
    uint32_t words[4];
} eos_rust_pthread_rwlock;

typedef struct eos_rust_pthread_once {
    uint32_t words[2];
} eos_rust_pthread_once_t;

typedef uint32_t eos_rust_socklen_t;

typedef struct eos_rust_in_addr {
    uint32_t s_addr;
} eos_rust_in_addr;

typedef struct eos_rust_in6_addr {
    uint8_t s6_addr[16];
} eos_rust_in6_addr;

typedef struct eos_rust_sockaddr {
    uint16_t sa_family;
    uint8_t sa_data[14];
} eos_rust_sockaddr;

typedef struct eos_rust_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    eos_rust_in_addr sin_addr;
    uint8_t sin_zero[8];
} eos_rust_sockaddr_in;

typedef struct eos_rust_sockaddr_in6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    eos_rust_in6_addr sin6_addr;
    uint32_t sin6_scope_id;
} eos_rust_sockaddr_in6;

typedef struct eos_rust_sockaddr_storage {
    uint16_t ss_family;
    uint8_t ss_data[26];
    uint32_t ss_align;
} eos_rust_sockaddr_storage;

typedef struct eos_rust_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
} eos_rust_timeval;

typedef struct eos_rust_pollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
} eos_rust_pollfd;

typedef struct eos_rust_addrinfo {
    int32_t ai_flags;
    int32_t ai_family;
    int32_t ai_socktype;
    int32_t ai_protocol;
    uint32_t ai_addrlen;
    eos_rust_sockaddr *ai_addr;
    char *ai_canonname;
    struct eos_rust_addrinfo *ai_next;
} eos_rust_addrinfo;

#define EOS_RUST_PTHREAD_STACK_MIN UINT32_C(4096)
#define EOS_RUST_PTHREAD_NAME_MAX UINT32_C(63)
#define EOS_RUST_CLOCK_REALTIME INT32_C(0)
#define EOS_RUST_CLOCK_MONOTONIC INT32_C(1)
#define EOS_RUST_PTHREAD_MUTEX_NORMAL INT32_C(0)
#define EOS_RUST_PTHREAD_MUTEX_RECURSIVE INT32_C(1)
#define EOS_RUST_PTHREAD_ONCE_INIT {{UINT32_C(0), UINT32_C(0)}}

/* Permanent EOS socket ABI values; these are not native MARTOS constants. */
#define EOS_RUST_AF_UNSPEC INT32_C(0)
#define EOS_RUST_AF_INET INT32_C(2)
#define EOS_RUST_AF_INET6 INT32_C(10)
#define EOS_RUST_SOCK_STREAM INT32_C(1)
#define EOS_RUST_SOCK_DGRAM INT32_C(2)
#define EOS_RUST_IPPROTO_IP INT32_C(0)
#define EOS_RUST_IPPROTO_TCP INT32_C(6)
#define EOS_RUST_IPPROTO_UDP INT32_C(17)
#define EOS_RUST_IPPROTO_IPV6 INT32_C(41)
#define EOS_RUST_SHUT_RD INT32_C(0)
#define EOS_RUST_SHUT_WR INT32_C(1)
#define EOS_RUST_SHUT_RDWR INT32_C(2)
#define EOS_RUST_MSG_OOB INT32_C(0x1)
#define EOS_RUST_MSG_PEEK INT32_C(0x2)
#define EOS_RUST_MSG_DONTROUTE INT32_C(0x4)
#define EOS_RUST_MSG_DONTWAIT INT32_C(0x40)
#define EOS_RUST_MSG_NOSIGNAL INT32_C(0x4000)

#define EOS_RUST_SOL_SOCKET INT32_C(1)
#define EOS_RUST_SO_REUSEADDR INT32_C(2)
#define EOS_RUST_SO_ERROR INT32_C(4)
#define EOS_RUST_SO_BROADCAST INT32_C(6)
#define EOS_RUST_SO_SNDBUF INT32_C(7)
#define EOS_RUST_SO_RCVBUF INT32_C(8)
#define EOS_RUST_SO_KEEPALIVE INT32_C(9)
#define EOS_RUST_SO_LINGER INT32_C(13)
#define EOS_RUST_SO_RCVTIMEO INT32_C(20)
#define EOS_RUST_SO_SNDTIMEO INT32_C(21)
#define EOS_RUST_TCP_NODELAY INT32_C(1)
#define EOS_RUST_IP_TTL INT32_C(2)
#define EOS_RUST_IP_MULTICAST_TTL INT32_C(33)
#define EOS_RUST_IP_MULTICAST_LOOP INT32_C(34)
#define EOS_RUST_IP_ADD_MEMBERSHIP INT32_C(35)
#define EOS_RUST_IP_DROP_MEMBERSHIP INT32_C(36)
#define EOS_RUST_IPV6_MULTICAST_LOOP INT32_C(19)
#define EOS_RUST_IPV6_ADD_MEMBERSHIP INT32_C(20)
#define EOS_RUST_IPV6_DROP_MEMBERSHIP INT32_C(21)
#define EOS_RUST_IPV6_V6ONLY INT32_C(26)

#define EOS_RUST_POLLIN INT16_C(0x001)
#define EOS_RUST_POLLPRI INT16_C(0x002)
#define EOS_RUST_POLLOUT INT16_C(0x004)
#define EOS_RUST_POLLERR INT16_C(0x008)
#define EOS_RUST_POLLHUP INT16_C(0x010)
#define EOS_RUST_POLLNVAL INT16_C(0x020)

#define EOS_RUST_AI_PASSIVE INT32_C(0x001)
#define EOS_RUST_AI_CANONNAME INT32_C(0x002)
#define EOS_RUST_AI_NUMERICHOST INT32_C(0x004)
#define EOS_RUST_AI_V4MAPPED INT32_C(0x008)
#define EOS_RUST_AI_ALL INT32_C(0x010)
#define EOS_RUST_AI_ADDRCONFIG INT32_C(0x020)
#define EOS_RUST_AI_NUMERICSERV INT32_C(0x400)
#define EOS_RUST_EAI_BADFLAGS INT32_C(-1)
#define EOS_RUST_EAI_NONAME INT32_C(-2)
#define EOS_RUST_EAI_AGAIN INT32_C(-3)
#define EOS_RUST_EAI_FAIL INT32_C(-4)
#define EOS_RUST_EAI_FAMILY INT32_C(-6)
#define EOS_RUST_EAI_SOCKTYPE INT32_C(-7)
#define EOS_RUST_EAI_SERVICE INT32_C(-8)
#define EOS_RUST_EAI_MEMORY INT32_C(-10)
#define EOS_RUST_EAI_SYSTEM INT32_C(-11)
#define EOS_RUST_EAI_OVERFLOW INT32_C(-12)

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
 * Performs terminal cleanup for compatibility state owned by the calling
 * thread. Calls are idempotent. This does not reset process-wide runtime
 * state, close descriptors, or reclaim process-lifetime registries. Cleanup
 * failures after state release emit a direct diagnostic and abort.
 */
EOS_RUST_EXPORT void eos_rust_runtime_cleanup(void);

/* Returns the number of processor cores available to this EOS application. */
EOS_RUST_EXPORT uint32_t eos_rust_cpu_count(void);

/*
 * File operations use nonnegative byte counts or offsets for success and -1
 * with compatibility errno for failure. Byte counts above INT32_MAX are
 * rejected with EINVAL before descriptor or buffer access, so every transfer
 * result remains representable. A partial transfer outranks a later native
 * error. realpath/getcwd return zero on success; a short buffer is
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
 * Process identities and statuses are fixed-width compatibility values; no
 * native application, thread, or wait structure crosses this boundary.
 * A stdio descriptor of -1 inherits descriptor 0, 1, or 2 respectively.
 * Spawn copies arguments, environment, and cwd before returning. Wait is
 * repeatable, try_wait returns 1 when complete and 0 while running, and close
 * invalidates the identity without waiting. Current MARTOS-SMP 14.0.39
 * supports isolated stdin/stdout bridging and its original native stderr, but returns
 * ENOTSUP for an explicit environment, cwd, stderr redirection, or arbitrary
 * non-CLOEXEC descriptor inheritance.
 */
EOS_RUST_EXPORT int32_t eos_rust_spawn(const eos_rust_spawn_request *request,
                                       eos_rust_process_t *process);
EOS_RUST_EXPORT int32_t eos_rust_process_wait(
    eos_rust_process_t process, eos_rust_process_status *status);
EOS_RUST_EXPORT int32_t eos_rust_process_try_wait(
    eos_rust_process_t process, eos_rust_process_status *status);
EOS_RUST_EXPORT int32_t eos_rust_process_kill(eos_rust_process_t process);
EOS_RUST_EXPORT int32_t eos_rust_process_close(eos_rust_process_t process);

/*
 * Thread and TLS calls use copyable, monotonic nonzero 32-bit identities.
 * Native thread pointers never cross this ABI. Pthread-shaped failures are
 * returned directly as positive EOS errno values and do not change
 * compatibility errno. An all-zero attribute is the valid default (4096-byte
 * stack); destroyed attributes and stack sizes below 4096 are EINVAL.
 *
 * Thread names are compatibility-visible names of at most 63 bytes plus null.
 * MARTOS 14.0.39 cannot rename an existing native diagnostic thread. getname
 * always terminates a nonzero-capacity buffer; truncation returns ERANGE.
 * Invalid/stale handles return ESRCH, self-join returns EDEADLK, and a second
 * join/detach claim on a still-live record returns EINVAL.
 */
EOS_RUST_EXPORT int32_t eos_rust_pthread_create(
    eos_rust_thread_t *thread,
    const eos_rust_pthread_attr *attribute,
    void *(*start_routine)(void *),
    void *argument);
EOS_RUST_EXPORT int32_t eos_rust_pthread_join(eos_rust_thread_t thread,
                                              void **result);
EOS_RUST_EXPORT int32_t eos_rust_pthread_detach(eos_rust_thread_t thread);
EOS_RUST_EXPORT eos_rust_thread_t eos_rust_pthread_self(void);
EOS_RUST_EXPORT int32_t eos_rust_pthread_equal(eos_rust_thread_t left,
                                               eos_rust_thread_t right);
EOS_RUST_EXPORT int32_t eos_rust_pthread_attr_init(
    eos_rust_pthread_attr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_attr_destroy(
    eos_rust_pthread_attr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_attr_getstacksize(
    const eos_rust_pthread_attr *attribute,
    uint32_t *stack_size);
EOS_RUST_EXPORT int32_t eos_rust_pthread_attr_setstacksize(
    eos_rust_pthread_attr *attribute,
    uint32_t stack_size);
EOS_RUST_EXPORT int32_t eos_rust_pthread_yield(void);
EOS_RUST_EXPORT int32_t eos_rust_pthread_getname_np(eos_rust_thread_t thread,
                                                    char *name,
                                                    uint32_t capacity);
EOS_RUST_EXPORT int32_t eos_rust_pthread_setname_np(eos_rust_thread_t thread,
                                                    const char *name);

/*
 * TLS keys are monotonic and never reused. Deleted keys return EINVAL and
 * never invoke their destructor. Destructors run in ascending key order for
 * at most four passes; POSIX itself does not promise this ordering.
 * Invalid getspecific returns null and sets compatibility errno to EINVAL;
 * valid null values do not change it. Other calls return errors directly.
 * TLS roots for threads not created by eos_rust_pthread_create (including the
 * main thread) cannot be reclaimed automatically on MARTOS 14.0.39 because
 * EOS exposes no safe external-thread termination interception hook.
 */
EOS_RUST_EXPORT int32_t eos_rust_pthread_key_create(
    eos_rust_tls_key_t *key,
    void (*destructor)(void *));
EOS_RUST_EXPORT int32_t eos_rust_pthread_key_delete(eos_rust_tls_key_t key);
EOS_RUST_EXPORT void *eos_rust_pthread_getspecific(eos_rust_tls_key_t key);
EOS_RUST_EXPORT int32_t eos_rust_pthread_setspecific(eos_rust_tls_key_t key,
                                                     const void *value);

/*
 * Synchronization values are fixed-layout registry identities, never native
 * objects. Their all-zero representations are lazy process-private
 * initializers. Pthread-shaped failures are returned as positive EOS errno
 * values and preserve compatibility errno. Condition deadlines are absolute
 * CLOCK_MONOTONIC values. Initialized objects must not be copied or moved.
 */
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutexattr_init(
    eos_rust_pthread_mutexattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutexattr_destroy(
    eos_rust_pthread_mutexattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutexattr_settype(
    eos_rust_pthread_mutexattr *attribute,
    int32_t type);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutex_init(
    eos_rust_pthread_mutex *mutex,
    const eos_rust_pthread_mutexattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutex_destroy(
    eos_rust_pthread_mutex *mutex);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutex_lock(
    eos_rust_pthread_mutex *mutex);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutex_trylock(
    eos_rust_pthread_mutex *mutex);
EOS_RUST_EXPORT int32_t eos_rust_pthread_mutex_unlock(
    eos_rust_pthread_mutex *mutex);

EOS_RUST_EXPORT int32_t eos_rust_pthread_condattr_init(
    eos_rust_pthread_condattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_condattr_destroy(
    eos_rust_pthread_condattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_condattr_setclock(
    eos_rust_pthread_condattr *attribute,
    int32_t clock_id);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_init(
    eos_rust_pthread_cond *condition,
    const eos_rust_pthread_condattr *attribute);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_destroy(
    eos_rust_pthread_cond *condition);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_signal(
    eos_rust_pthread_cond *condition);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_broadcast(
    eos_rust_pthread_cond *condition);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_wait(
    eos_rust_pthread_cond *condition,
    eos_rust_pthread_mutex *mutex);
EOS_RUST_EXPORT int32_t eos_rust_pthread_cond_timedwait(
    eos_rust_pthread_cond *condition,
    eos_rust_pthread_mutex *mutex,
    const eos_rust_timespec *absolute_deadline);

EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_init(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_destroy(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_rdlock(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_tryrdlock(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_wrlock(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_trywrlock(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_rwlock_unlock(
    eos_rust_pthread_rwlock *rwlock);
EOS_RUST_EXPORT int32_t eos_rust_pthread_once(
    eos_rust_pthread_once_t *control,
    void (*initialization_routine)(void));

/* POSIX-shaped clock calls return 0/-1 and set compatibility errno. */
EOS_RUST_EXPORT int32_t eos_rust_clock_gettime(
    int32_t clock_id,
    eos_rust_timespec *time);
EOS_RUST_EXPORT int32_t eos_rust_nanosleep(
    const eos_rust_timespec *requested,
    eos_rust_timespec *remaining);

/*
 * Socket byte counts and lengths are permanently 32-bit. Transfers reject a
 * count above INT32_MAX before descriptor, buffer, or native access. Socket
 * addresses contain network-byte-order ports and addresses and never expose
 * a native host or MARTOS structure.
 */
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_socket(int32_t domain,
                                               int32_t type,
                                               int32_t protocol);
EOS_RUST_EXPORT int32_t eos_rust_bind(eos_rust_fd_t descriptor,
                                      const eos_rust_sockaddr *address,
                                      eos_rust_socklen_t address_length);
EOS_RUST_EXPORT int32_t eos_rust_connect(eos_rust_fd_t descriptor,
                                         const eos_rust_sockaddr *address,
                                         eos_rust_socklen_t address_length);
EOS_RUST_EXPORT int32_t eos_rust_listen(eos_rust_fd_t descriptor,
                                        int32_t backlog);
EOS_RUST_EXPORT eos_rust_fd_t eos_rust_accept(
    eos_rust_fd_t descriptor,
    eos_rust_sockaddr *address,
    eos_rust_socklen_t *address_length);
EOS_RUST_EXPORT int32_t eos_rust_send(eos_rust_fd_t descriptor,
                                      const void *buffer,
                                      uint32_t byte_count,
                                      int32_t flags);
EOS_RUST_EXPORT int32_t eos_rust_recv(eos_rust_fd_t descriptor,
                                      void *buffer,
                                      uint32_t byte_count,
                                      int32_t flags);
EOS_RUST_EXPORT int32_t eos_rust_sendto(
    eos_rust_fd_t descriptor,
    const void *buffer,
    uint32_t byte_count,
    int32_t flags,
    const eos_rust_sockaddr *destination,
    eos_rust_socklen_t destination_length);
EOS_RUST_EXPORT int32_t eos_rust_recvfrom(
    eos_rust_fd_t descriptor,
    void *buffer,
    uint32_t byte_count,
    int32_t flags,
    eos_rust_sockaddr *source,
    eos_rust_socklen_t *source_length);
EOS_RUST_EXPORT int32_t eos_rust_shutdown(eos_rust_fd_t descriptor,
                                          int32_t how);
EOS_RUST_EXPORT int32_t eos_rust_getsockname(
    eos_rust_fd_t descriptor,
    eos_rust_sockaddr *address,
    eos_rust_socklen_t *address_length);
EOS_RUST_EXPORT int32_t eos_rust_getpeername(
    eos_rust_fd_t descriptor,
    eos_rust_sockaddr *address,
    eos_rust_socklen_t *address_length);
EOS_RUST_EXPORT int32_t eos_rust_setsockopt(eos_rust_fd_t descriptor,
                                            int32_t level,
                                            int32_t option_name,
                                            const void *option_value,
                                            eos_rust_socklen_t option_length);
EOS_RUST_EXPORT int32_t eos_rust_getsockopt(eos_rust_fd_t descriptor,
                                            int32_t level,
                                            int32_t option_name,
                                            void *option_value,
                                            eos_rust_socklen_t *option_length);
EOS_RUST_EXPORT int32_t eos_rust_poll(eos_rust_pollfd *descriptors,
                                      uint32_t descriptor_count,
                                      int32_t timeout_milliseconds);
EOS_RUST_EXPORT int32_t eos_rust_inet_pton(int32_t family,
                                           const char *source,
                                           void *destination);
EOS_RUST_EXPORT const char *eos_rust_inet_ntop(int32_t family,
                                               const void *source,
                                               char *destination,
                                               eos_rust_socklen_t capacity);
EOS_RUST_EXPORT int32_t eos_rust_getaddrinfo(
    const char *node,
    const char *service,
    const eos_rust_addrinfo *hints,
    eos_rust_addrinfo **result);
EOS_RUST_EXPORT void eos_rust_freeaddrinfo(eos_rust_addrinfo *result);
EOS_RUST_EXPORT const char *eos_rust_gai_strerror(int32_t error_code);

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
 * every successful call still advances the process state. Returns zero on
 * success or -1 with EOS errno set if the state lock cannot be acquired.
 */
EOS_RUST_EXPORT int32_t eos_rust_hash_seed(uint64_t *key0, uint64_t *key1);

#ifdef __cplusplus
}
#endif

#endif
