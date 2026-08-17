# Task 8 Report — Synchronization, Parking, and Clock Services

## Outcome

Task 8 adds the fixed-layout EOS synchronization and time ABI required by the Rust 1.97.1
Unix PAL. The implementation provides normal and recursive mutexes, condition variables,
writer-preference rwlocks, process-lifetime once controls, monotonic/realtime clocks, and
relative nanosleep. Rust's generic Unix parker is supported through the mutex/condition
surface; there is deliberately no separate park export.

The accepted 66-symbol ABI grows by exactly 27 symbols to 93. Host-test seams remain absent
from production archives. No Task 9 networking or later PAL work is included.

The implementation commit containing this report is titled
`runtime: add EOS synchronization and clocks`. Its final hash is recorded in the Task 8
handoff because a commit cannot embed its own final content hash.

## TDD and debugging evidence

The synchronization and time tests were created as independent executables before their
production implementations. Each compiled and then failed at link time with exit status 2:

- `sync_test` reported unresolved mutex/condition/rwlock/once ABI symbols and host-test
  observation seams.
- `time_test` reported unresolved `eos_rust_clock_gettime`, `eos_rust_nanosleep`, deadline,
  and deterministic time seams.

The first time implementation GREEN passed. The first synchronization run exposed a status
translation error: successful native status zero was incorrectly sent through the error-only
mapper and became `EIO`. Mapping only nonzero statuses made the focused synchronization test
GREEN.

The deterministic MARTOS contract test initially failed to compile because a fake observation
object and fake delay function shared a C identifier. That was a test defect, not accepted RED
evidence; the object was renamed before the mapping assertions were accepted. The resulting
test pins every required native choice and argument.

Additional test-first RED/GREEN rounds found and fixed:

- TSan reported common-code races caused by reading public mutex/condition/rwlock words before
  taking the registry lock. Removing all three pre-lock reads made publication/destruction
  race-free.
- TSan then reported a race in the host-only last-mutex-kind observation seam. Making that seam
  atomic produced a clean fully instrumented run.
- An expanded arbitration/fault test compiled, then linked with exit status 2 on missing
  timeout-pause, writer-wait, and mutex-unlock host-test seams. Atomic test-only hooks produced
  behavioral and TSan GREEN.
- A separate spurious-wake test linked with exit status 2 on its missing host seam. The seam
  returns one deliberate success without a semaphore token; the condition waiter removes itself,
  reacquires the user mutex, and leaves accounting balanced.
- A validation mutation failed behaviorally because malformed mutex attributes were classified
  as unsupported when the requested type was also unsupported, and explicit initialization of
  malformed nonzero objects returned `EBUSY`. Validation was moved ahead of requested-mode
  classification, and mutex/condition/rwlock init now distinguishes a valid live object
  (`EBUSY`) from malformed/stale words (`EINVAL`) without mutation.

The full strict suite also exposed a pre-existing test race in Task 7 lifecycle accounting.
`pthread_join` may return after harvesting the registry record but before the child trampoline
drops its final reference and destroys the completion sync. The diagnostic was
`live 0 -> 0, sync 4 -> 7 (expected 6)`. The test now waits for that permitted deferred cleanup
before measuring failed-create accounting. Fully instrumented `thread_tsan` then passed 50
consecutive runs.

## Stable ABI and layouts

Only fixed-width fields cross the ABI:

| Type | Size | Alignment | Layout |
| --- | ---: | ---: | --- |
| `eos_rust_timespec` | 16 | 8 | signed 64-bit seconds at 0, nanoseconds at 8 |
| `eos_rust_pthread_mutex` | 16 | 4 | four `uint32_t` words at offset 0 |
| `eos_rust_pthread_mutexattr` | 8 | 4 | two `uint32_t` words at offset 0 |
| `eos_rust_pthread_cond` | 16 | 4 | four `uint32_t` words at offset 0 |
| `eos_rust_pthread_condattr` | 8 | 4 | two `uint32_t` words at offset 0 |
| `eos_rust_pthread_rwlock` | 16 | 4 | four `uint32_t` words at offset 0 |
| `eos_rust_pthread_once_t` | 8 | 4 | two `uint32_t` words at offset 0 |

The plan's tag name `eos_rust_pthread_once` cannot also be a typedef of that name in C because
typedefs and functions share the ordinary identifier namespace. The public typedef therefore
uses the justified minor adjustment `eos_rust_pthread_once_t`; the function remains
`eos_rust_pthread_once`.

Constants are fixed independently of host libc: realtime 0, monotonic 1, normal mutex 0,
recursive mutex 1, and an all-zero once initializer. Host C++, ARM soft-EABI C, and common C
compiled against the MARTOS SDK all assert size, alignment, and member offsets.

## Error contract

Pthread-shaped calls return zero or a pinned positive EOS errno directly and preserve
compatibility errno. Clock and sleep calls return 0/-1 and set compatibility errno only on
failure.

| Condition | Result |
| --- | --- |
| malformed, partial-zero, stale, wrong-owner, or invalid argument | `EINVAL` (22) |
| try operation cannot acquire, live explicit re-init, or busy destruction | `EBUSY` (16) |
| unsupported mutex type or realtime condition clock | `ENOTSUP` (45) |
| expired absolute condition deadline | `ETIMEDOUT` (60) |
| registry identity exhaustion | `EWOULDBLOCK` (35) |
| allocation failure | mapped `ENOMEM` (12) |
| other recoverable native failure | exact pinned mapping, unknown status as `EIO` (5) |
| relative-time arithmetic overflow | `EOVERFLOW` (84) |

Malformed attribute state takes precedence over classifying a requested mode as unsupported.
Operations that reject a representation do not mutate it. Native unlock, post-selection wake,
or destruction failures after irreversible state transfer abort rather than report false
recovery.

## Registry, object, and ownership model

One scheduler-aware registry lock serializes all public word reads/writes and all registry
membership changes. A single shared, monotonic, nonzero 32-bit identity source covers mutexes,
conditions, rwlocks, and once records. Identities are never reused; reaching `UINT32_MAX`
permanently exhausts allocation.

Destroyable public objects move through `zero -> live identity/magic -> dead marker`. A lazy
operation allocates a native candidate outside the registry, rechecks under the registry, and
either publishes exactly one candidate or destroys the loser. Explicit init returns `EBUSY`
only for a validated live object. Copies made before first use remain independent zero values;
initialized copies are unsupported and can never carry native pointers.

Every record has a registry owner plus explicit in-flight operation/waiter state. Destroy
removes the registry entry only when no operation, waiter, reader, writer, owner, or recursion
depth can dereference it. Native teardown and free occur after registry removal. No common code
spins or blocks while holding the global registry lock.

Mutex state records native kind, active operations, native waiters, owner identity, and recursive
depth. All-zero/null attributes select a normal `os_mutex_create`; recursive attributes select
`os_mutex_recursive_create`. A normal same-owner blocking relock has normal deadlock semantics;
recursive depth is exact. Try lock uses literal no-wait zero.

## Conditions and parker semantics

Each condition record owns a scheduler-aware native normal mutex. Every current waiter owns a
private counting semaphore and a list node. The waiter is linked before the user mutex is
released. Signal/broadcast select under the condition lock and give the selected waiter's token
while that selection is still protected. Signal selects at most one; broadcast selects exactly
the current eligible set; a future waiter cannot consume an old token.

After any success, timeout, spurious wake, or recoverable native wait failure, the waiter
arbitrates removal under the condition lock, destroys its semaphore, drops its operation owner,
and reacquires the user mutex before returning. When a signal selection races an observed
timeout, selection wins and the queued token is consumed with no-wait. The deterministic pause
seam proves that exact linearization. A failed give after selection is irreversible and aborts.

Parker tests cover predicate-protected notify-before-wait, notification during wait, exact
signal-one, current-waiter broadcast, timeout without consuming a later notification, future
waiter non-consumption, spurious wake, and the timeout/signal race.

## Rwlock and once state machines

Rwlocks use one internal mutex and separate counting semaphores for readers and writers. New
readers are admitted only when there is no active writer and no queued writer. The final reader
or writer grants exactly one queued writer first; only when no writer waits is the entire queued
reader batch granted. Waiting counts remain owned by the blocked operation and are rolled back
on recoverable wait failure. A deterministic writer-wait observation proves that a late reader
cannot pass an already queued writer. A 12-thread contention test checks reader/writer exclusion
under both normal and fully instrumented TSan builds.

Once records move through `NEW -> RUNNING(owner) -> COMPLETE`. The record is published before
the callback runs; the callback executes outside every runtime lock. Contenders wait on the
completion sync and acquire callback writes before returning. Recursive once on the same control
has normal deadlock behavior. Records and identities deliberately persist for process lifetime,
so completed controls remain unambiguous and cannot alias. User callback unwind through this C
boundary is unsupported until Task 14 containment.

## Clocks, deadlines, and nanosleep

MARTOS monotonic time is the 64-bit microsecond value from `os_timer_get_usec`. Reads and the
last-returned clamp are serialized by the time port lock, so concurrent cross-core observations
never regress. Realtime calls `os_utc_get_usec(&value, NULL)` and does not modify monotonic state.

Timespec inputs require nonnegative seconds and `0 <= nsec < 1_000_000_000`. For a positive
absolute interval, timeout ticks are `ceil(delta_seconds * rate + delta_nanoseconds * rate /
1e9)`, with checked arithmetic. Positive sub-tick values become one tick. Values saturate at
`UINT32_MAX - 1`, never the `UINT32_MAX` wait-forever sentinel. After every native timeout the
code rereads monotonic time and recomputes until the event wins or the deadline expires.

Nanosleep rounds a positive sub-microsecond tail upward, splits whole milliseconds into finite
`os_delay` chunks, and sends the remaining microseconds to `os_delay_usec`. It never passes
`UINT32_MAX` to delay. On an injected native failure it sets the mapped errno and returns a
normalized remainder derived from the monotonic deadline; success zeros a supplied remainder.

## Native mapping and boundary

The deterministic mapping contract and real MARTOS 14.0.39 compile pin:

- normal/recursive create to `os_mutex_create`/`os_mutex_recursive_create`;
- exact mutex lock timeout, unlock, and delete arguments;
- `os_sem_counting_create(semaphore, maximum, initial)`, take/give/delete;
- `OS_NO_WAIT == 0`, `OS_WAIT_FOREVER == UINT32_MAX`, and tick rate 1000;
- 64-bit `os_timer_get_usec`, `os_utc_get_usec(value, NULL)`;
- exact finite `os_delay` and `os_delay_usec` arguments.

Common sources contain no native pthread, MARTOS, `time_t`, or clock-id types. Host pthread and
clock objects remain in the host port; `os_*` types remain in the MARTOS port. The production
archive exposes none of the deterministic seams.

## Fault, race, and mutation coverage

Tests cover allocation; registry lock; native mutex create/lock/unlock/delete; waiter semaphore
create/take/give/delete; condition internal lock; rwlock allocation/lock/wait/wake/delete; time
lock; realtime; delay; and cleanup failure paths. Recoverable failures leave public state and
owners balanced. Irreversible cleanup failures are bounded child-process abort tests.

Race/mutation coverage includes 16-thread lazy mutex publication, 16-thread concurrent explicit
init for all destroyable object classes, normal-vs-recursive mapping, wrong-owner unlock, stale
objects, exact signal/broadcast sets, future-signal theft, spurious wake, deterministic
timeout/signal arbitration, writer preference, high-contention exclusion, once pre-callback
publication/completion visibility, process-lifetime retention, identity exhaustion, monotonic
source regression, concurrent non-regression, sub-tick ceiling, finite saturation, long sleep
chunking, exact failure remainder, and wait-forever-sentinel avoidance.

## Final verification

All commands below were rerun after the final validation-order fix:

- focused synchronization/time/thread/TLS/error/ABI/MARTOS mapping/TSan/export set: 14/14;
- fresh normal Release host configure/build/CTest: 32/32;
- strict Debug `-O0 -Wall -Wextra -Werror -pedantic` C/C++ build/CTest: 32/32;
- Release `-O3 -Wall -Wextra -Werror -pedantic` C/C++ build/CTest: 32/32;
- real MARTOS `BUILD_TESTING=OFF` Release build against
  `/home/dev/code/gpt-test/lib/martos-smp-14.0.39` with
  `-Wall -Wextra -Werror`: passed;
- exact host and MARTOS export checks: 93/93;
- MARTOS undefined audit: no global `errno`, `__errno_location`, `pthread_*`,
  `os_thread_wait`, or `os_thread_delete`;
- direct fresh `BUILD_TESTING=OFF` consumer linking only `EOS::RustABI`: passed;
- ARM Cortex-A9 softfp/PIC probe: ELF32 little-endian ARM, EABI5, v7-A Application,
  Thumb-2, `REL`;
- `git diff --check`: clean.

## Self-review and limitations

Self-review re-traced candidate publication, every active-operation decrement, mutex ownership
transfer, condition list/token arbitration, rwlock grant/rollback, once visibility, time overflow,
error precedence, and irreversible teardown. It also checked exact exports, native undefineds,
public/test seam separation, and file modes. DrvFS reports new files executable despite chmod;
their Git index modes are explicitly forced to 100644 before commit.

Deliberate limitations remain: condition clocks are monotonic-only; pthread cancellation is not
provided; once controls retain bounded process-lifetime metadata; callback unwind is not yet
contained; initialized pthread-shaped objects cannot be copied; no operation is ISR-callable;
and Zynq-7000 deployment remains manual. These are documented v1 choices, not silent fallbacks.
