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

eos_rust_pthread_mutex eos_arm_layout_probe;
