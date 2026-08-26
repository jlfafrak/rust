#include "eos_rust_abi.h"

#include <stddef.h>
#include <stdint.h>

typedef struct eos_rust_iovec_layout {
    void *iov_base;
    uint32_t iov_len;
} eos_rust_iovec_layout;

#define EMIT(name, value)                                                        \
    const unsigned char eos_layout_##name[(value) + 1] __attribute__((used)) = {0}
#define LAYOUT(name, type)                                                       \
    EMIT(sizeof_##name, sizeof(type));                                           \
    EMIT(alignof_##name, _Alignof(type))
#define FIELD(name, type, field) EMIT(offsetof_##name##_##field, offsetof(type, field))

LAYOUT(timespec, eos_rust_timespec);
FIELD(timespec, eos_rust_timespec, tv_sec);
FIELD(timespec, eos_rust_timespec, tv_nsec);
LAYOUT(timeval, eos_rust_timeval);
FIELD(timeval, eos_rust_timeval, tv_sec);
FIELD(timeval, eos_rust_timeval, tv_usec);

LAYOUT(stat, eos_rust_stat);
FIELD(stat, eos_rust_stat, st_dev);
FIELD(stat, eos_rust_stat, st_ino);
FIELD(stat, eos_rust_stat, st_mode);
FIELD(stat, eos_rust_stat, st_nlink);
FIELD(stat, eos_rust_stat, st_uid);
FIELD(stat, eos_rust_stat, st_gid);
FIELD(stat, eos_rust_stat, st_size);
EMIT(offsetof_stat_st_atime, offsetof(eos_rust_stat, st_atime_sec));
FIELD(stat, eos_rust_stat, st_atime_nsec);
EMIT(offsetof_stat_st_mtime, offsetof(eos_rust_stat, st_mtime_sec));
FIELD(stat, eos_rust_stat, st_mtime_nsec);
EMIT(offsetof_stat_st_ctime, offsetof(eos_rust_stat, st_ctime_sec));
FIELD(stat, eos_rust_stat, st_ctime_nsec);
FIELD(stat, eos_rust_stat, st_blocks);
FIELD(stat, eos_rust_stat, st_blksize);
FIELD(stat, eos_rust_stat, reserved);

LAYOUT(dirent, eos_rust_dirent);
FIELD(dirent, eos_rust_dirent, d_ino);
FIELD(dirent, eos_rust_dirent, d_type);
FIELD(dirent, eos_rust_dirent, d_name_length);
FIELD(dirent, eos_rust_dirent, d_name);
LAYOUT(iovec, eos_rust_iovec_layout);
FIELD(iovec, eos_rust_iovec_layout, iov_base);
FIELD(iovec, eos_rust_iovec_layout, iov_len);

LAYOUT(pthread_attr_t, eos_rust_pthread_attr);
LAYOUT(pthread_mutex_t, eos_rust_pthread_mutex);
LAYOUT(pthread_mutexattr_t, eos_rust_pthread_mutexattr);
LAYOUT(pthread_cond_t, eos_rust_pthread_cond);
LAYOUT(pthread_condattr_t, eos_rust_pthread_condattr);
LAYOUT(pthread_rwlock_t, eos_rust_pthread_rwlock);
LAYOUT(pthread_once_t, eos_rust_pthread_once_t);

LAYOUT(in_addr, eos_rust_in_addr);
FIELD(in_addr, eos_rust_in_addr, s_addr);
LAYOUT(in6_addr, eos_rust_in6_addr);
FIELD(in6_addr, eos_rust_in6_addr, s6_addr);
LAYOUT(sockaddr, eos_rust_sockaddr);
FIELD(sockaddr, eos_rust_sockaddr, sa_family);
FIELD(sockaddr, eos_rust_sockaddr, sa_data);
LAYOUT(sockaddr_in, eos_rust_sockaddr_in);
FIELD(sockaddr_in, eos_rust_sockaddr_in, sin_family);
FIELD(sockaddr_in, eos_rust_sockaddr_in, sin_port);
FIELD(sockaddr_in, eos_rust_sockaddr_in, sin_addr);
FIELD(sockaddr_in, eos_rust_sockaddr_in, sin_zero);
LAYOUT(sockaddr_in6, eos_rust_sockaddr_in6);
FIELD(sockaddr_in6, eos_rust_sockaddr_in6, sin6_family);
FIELD(sockaddr_in6, eos_rust_sockaddr_in6, sin6_port);
FIELD(sockaddr_in6, eos_rust_sockaddr_in6, sin6_flowinfo);
FIELD(sockaddr_in6, eos_rust_sockaddr_in6, sin6_addr);
FIELD(sockaddr_in6, eos_rust_sockaddr_in6, sin6_scope_id);
LAYOUT(sockaddr_storage, eos_rust_sockaddr_storage);
FIELD(sockaddr_storage, eos_rust_sockaddr_storage, ss_family);
FIELD(sockaddr_storage, eos_rust_sockaddr_storage, ss_data);
FIELD(sockaddr_storage, eos_rust_sockaddr_storage, ss_align);

LAYOUT(pollfd, eos_rust_pollfd);
FIELD(pollfd, eos_rust_pollfd, fd);
FIELD(pollfd, eos_rust_pollfd, events);
FIELD(pollfd, eos_rust_pollfd, revents);
LAYOUT(addrinfo, eos_rust_addrinfo);
FIELD(addrinfo, eos_rust_addrinfo, ai_flags);
FIELD(addrinfo, eos_rust_addrinfo, ai_family);
FIELD(addrinfo, eos_rust_addrinfo, ai_socktype);
FIELD(addrinfo, eos_rust_addrinfo, ai_protocol);
FIELD(addrinfo, eos_rust_addrinfo, ai_addrlen);
FIELD(addrinfo, eos_rust_addrinfo, ai_addr);
FIELD(addrinfo, eos_rust_addrinfo, ai_canonname);
FIELD(addrinfo, eos_rust_addrinfo, ai_next);

LAYOUT(spawn_request, eos_rust_spawn_request);
FIELD(spawn_request, eos_rust_spawn_request, program);
FIELD(spawn_request, eos_rust_spawn_request, argv);
FIELD(spawn_request, eos_rust_spawn_request, argc);
FIELD(spawn_request, eos_rust_spawn_request, envp);
FIELD(spawn_request, eos_rust_spawn_request, envc);
FIELD(spawn_request, eos_rust_spawn_request, cwd);
FIELD(spawn_request, eos_rust_spawn_request, stdin_fd);
FIELD(spawn_request, eos_rust_spawn_request, stdout_fd);
FIELD(spawn_request, eos_rust_spawn_request, stderr_fd);
FIELD(spawn_request, eos_rust_spawn_request, flags);
FIELD(spawn_request, eos_rust_spawn_request, reserved);
LAYOUT(process_status, eos_rust_process_status);
FIELD(process_status, eos_rust_process_status, kind);
FIELD(process_status, eos_rust_process_status, code);
FIELD(process_status, eos_rust_process_status, reserved);
