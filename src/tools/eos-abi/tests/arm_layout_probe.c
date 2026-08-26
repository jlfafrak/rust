#include "eos_rust_abi.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(eos_rust_timespec) == 16, "timespec size");
_Static_assert(_Alignof(eos_rust_timespec) == 8, "timespec alignment");
_Static_assert(offsetof(eos_rust_timespec, tv_nsec) == 8,
               "timespec nanoseconds offset");
_Static_assert(sizeof(eos_rust_pthread_mutex) == 16, "mutex size");
_Static_assert(_Alignof(eos_rust_pthread_mutex) == 4, "mutex alignment");
_Static_assert(offsetof(eos_rust_pthread_mutex, words) == 0,
               "mutex words offset");
_Static_assert(sizeof(eos_rust_pthread_mutexattr) == 8,
               "mutex attr size");
_Static_assert(_Alignof(eos_rust_pthread_mutexattr) == 4,
               "mutex attr alignment");
_Static_assert(offsetof(eos_rust_pthread_mutexattr, words) == 0,
               "mutex attr words offset");
_Static_assert(sizeof(eos_rust_pthread_cond) == 16, "condition size");
_Static_assert(_Alignof(eos_rust_pthread_cond) == 4,
               "condition alignment");
_Static_assert(offsetof(eos_rust_pthread_cond, words) == 0,
               "condition words offset");
_Static_assert(sizeof(eos_rust_pthread_condattr) == 8,
               "condition attr size");
_Static_assert(_Alignof(eos_rust_pthread_condattr) == 4,
               "condition attr alignment");
_Static_assert(offsetof(eos_rust_pthread_condattr, words) == 0,
               "condition attr words offset");
_Static_assert(sizeof(eos_rust_pthread_rwlock) == 16, "rwlock size");
_Static_assert(_Alignof(eos_rust_pthread_rwlock) == 4, "rwlock alignment");
_Static_assert(offsetof(eos_rust_pthread_rwlock, words) == 0,
               "rwlock words offset");
_Static_assert(sizeof(eos_rust_pthread_once_t) == 8, "once size");
_Static_assert(_Alignof(eos_rust_pthread_once_t) == 4, "once alignment");
_Static_assert(offsetof(eos_rust_pthread_once_t, words) == 0,
               "once words offset");
_Static_assert(sizeof(eos_rust_sockaddr) == 16, "sockaddr size");
_Static_assert(_Alignof(eos_rust_sockaddr) == 2, "sockaddr alignment");
_Static_assert(sizeof(eos_rust_sockaddr_in) == 16, "IPv4 sockaddr size");
_Static_assert(offsetof(eos_rust_sockaddr_in, sin_addr) == 4,
               "IPv4 address offset");
_Static_assert(sizeof(eos_rust_sockaddr_in6) == 28, "IPv6 sockaddr size");
_Static_assert(offsetof(eos_rust_sockaddr_in6, sin6_addr) == 8,
               "IPv6 address offset");
_Static_assert(offsetof(eos_rust_sockaddr_in6, sin6_scope_id) == 24,
               "IPv6 scope offset");
_Static_assert(sizeof(eos_rust_sockaddr_storage) == 32,
               "sockaddr storage size");
_Static_assert(_Alignof(eos_rust_sockaddr_storage) == 4,
               "sockaddr storage alignment");
_Static_assert(sizeof(eos_rust_timeval) == 16, "timeval size");
_Static_assert(_Alignof(eos_rust_timeval) == 8, "timeval alignment");
_Static_assert(sizeof(eos_rust_pollfd) == 8, "pollfd size");
_Static_assert(_Alignof(eos_rust_pollfd) == 4, "pollfd alignment");
_Static_assert(offsetof(eos_rust_pollfd, revents) == 6,
               "pollfd result offset");
_Static_assert(sizeof(eos_rust_addrinfo) == 32, "addrinfo ARM size");
_Static_assert(_Alignof(eos_rust_addrinfo) == 4,
               "addrinfo ARM alignment");
_Static_assert(offsetof(eos_rust_addrinfo, ai_flags) == 0,
               "addrinfo flags offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_family) == 4,
               "addrinfo family offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_socktype) == 8,
               "addrinfo socktype offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_protocol) == 12,
               "addrinfo protocol offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_addrlen) == 16,
               "addrinfo address length offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_addr) == 20,
               "addrinfo address pointer offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_canonname) == 24,
               "addrinfo canonical pointer offset");
_Static_assert(offsetof(eos_rust_addrinfo, ai_next) == 28,
               "addrinfo next pointer offset");
_Static_assert(sizeof(eos_rust_process_t) == 4, "process identity size");
_Static_assert(sizeof(eos_rust_process_status) == 32,
               "process status size");
_Static_assert(_Alignof(eos_rust_process_status) == 4,
               "process status alignment");
_Static_assert(offsetof(eos_rust_process_status, code) == 4,
               "process status code offset");
_Static_assert(offsetof(eos_rust_process_status, reserved) == 8,
               "process status reserved offset");
_Static_assert(sizeof(eos_rust_spawn_request) == 68,
               "spawn request ARM size");
_Static_assert(_Alignof(eos_rust_spawn_request) == 4,
               "spawn request ARM alignment");
_Static_assert(offsetof(eos_rust_spawn_request, argv) == 4,
               "spawn argv offset");
_Static_assert(offsetof(eos_rust_spawn_request, envp) == 12,
               "spawn envp offset");
_Static_assert(offsetof(eos_rust_spawn_request, cwd) == 20,
               "spawn cwd offset");
_Static_assert(offsetof(eos_rust_spawn_request, stdin_fd) == 24,
               "spawn stdin offset");
_Static_assert(offsetof(eos_rust_spawn_request, flags) == 36,
               "spawn flags offset");
_Static_assert(offsetof(eos_rust_spawn_request, reserved) == 40,
               "spawn reserved offset");

eos_rust_pthread_mutex eos_arm_layout_probe;
