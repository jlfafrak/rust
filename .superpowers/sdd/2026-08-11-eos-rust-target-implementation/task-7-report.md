# Task 7 Report: Threads, Reserved TLS Root, and Destructors

## Status and scope

Task 7 adds the fixed-width EOS thread and library-managed TLS surface required by the future
Rust Unix PAL. The implementation is contained in the common runtime, private host/MARTOS ports,
and three behavioral test sources. It does not add Task 8 public synchronization/time services,
networking, process services, or Rust PAL bindings. The production ABI grows from exactly 50 to
exactly 66 exports.

## TDD evidence

The public declarations, CMake targets, and `thread_test.cpp`, `tls_test.cpp`, and
`tls_destructor_test.cpp` were added before the production thread/TLS sources. Each compiled
independently and failed at link time with exit 2:

- `thread_test`: unresolved thread create/join/detach/self/equal, attributes, naming, yield, and
  deterministic host thread seams;
- `tls_test`: unresolved key create/delete/get/set, errno-root isolation, slot, allocation, and
  failure seams;
- `tls_destructor_test`: unresolved thread/key cleanup, four-pass destructor, and record-reaping
  services.

After the first implementation, all three focused tests passed. Later focused RED/GREEN cycles
covered optimizer definite-initialization, production-only host seam isolation, a deterministic
detach-after-completion interleaving, a malformed native create that starts then returns failure,
in-flight destructor ownership across concurrent key deletion, detached test-object lifetime, and
strict-build test-only helper scoping.

## Stable public ABI

The public layouts are:

```c
typedef uint32_t eos_rust_thread_t;
typedef uint32_t eos_rust_tls_key_t;
typedef struct eos_rust_pthread_attr { uint32_t words[4]; }
    eos_rust_pthread_attr;
```

The 16 exported operations are create, join, detach, self, equal; attribute init, destroy,
get-stack-size, and set-stack-size; yield; get/set name; and key create, delete, get-specific, and
set-specific. Static assertions in production and tests establish 4-byte thread/key values and a
16-byte, 4-aligned attribute with `words` at offset zero. No native `pthread_t`, `os_thread *`, or
SDK structure crosses the public ABI.

Pthread-shaped functions return zero or a pinned positive EOS errno and preserve compatibility
errno. Invalid/stale harvested thread handles return `ESRCH=3`, self-join returns `EDEADLK=11`,
and a second join/detach claim on a live record returns `EINVAL=22`. An invalid key returns
`EINVAL`; pointer-returning `getspecific` returns null and writes that error to compatibility
errno, while a valid null value leaves errno unchanged. `yield` honestly returns `ENOTSUP=45`.

All-zero and initialized attributes both select the 4096-byte default/minimum stack. Stack values
must be at least 4096 and 8-byte aligned. Destroyed/malformed attributes are rejected. Names have
at most 63 bytes plus null; short get buffers receive a terminated prefix and `ERANGE=34`.
MARTOS 14.0.39 has no native rename operation, so names after creation are explicitly
compatibility-visible only.

## Thread registry and native lifecycle

Public identities are monotonic, nonzero 32-bit values and are never reused. After issuing
`UINT32_MAX`, registration fails deterministically instead of wrapping. A heap-backed registry is
protected by the scheduler-aware thread-registry lock. Each record tracks explicit registry,
child, and temporary join-operation references. `in_registry` makes registry-reference release
idempotent across detach/completion interleavings. Record sync destruction and memory release are
outside the registry lock and fail fast if an irreversible cleanup step fails.

Creation allocates and publishes the complete compatibility record before the auto-start native
call. The native port receives no output pointer or termination callback, so the child never
depends on creator publication of a native handle. A deterministic fake and host seam execute the
child through completion before `os_thread_create` returns and prove the public record remains
joinable. A normal native-create failure, for which no child ran, removes both registry and child
ownership and leaves caller output zero. A deliberately malformed fake that starts/completes the
child and then returns failure is detected from completion publication and fails fast rather than
rolling back live state.

Exactly one join or detach transitions a joinable record. Join acquires operation ownership,
waits on the compatibility completion sync, reads the exact pointer result, and harvests the
record. A failed wait rolls the claim back. Detached records are removed either by detach after
completion or by the child after detach; the explicit registry owner prevents a double drop.
External/main threads can receive monotonic identities, but their permanent records cannot be
reaped because MARTOS exposes no safe interception of external-thread termination.

The implementation deliberately does not retain or use an `os_thread *`, call `os_thread_wait`,
call `os_thread_delete`, or install a native termination callback. Authoritative shipped-kernel
ELF evidence requires this deviation from the initial prose plan:

- `os_thread_entry` at `0x142288` invokes the user entry, sets the EXIT event, and calls
  `vTaskDelete` on normal return;
- deletion hook `os_thread_cleanup` at `0x142058` frees the native stack, announces an optional
  termination callback, and queues the native record in a 1000-tick limbo;
- `os_system_handle_limbo_threads` later frees that record.

Consequently a native pointer retained for late join/wait would be unsafe. The compatibility
record publishes completion before returning into that native auto-reap path. The target mapping
test checks exact `OS_THREAD_DEFAULT_FEATURES`, name/start/argument/stack, `OS_PRIO_DEFAULT=200`,
null output and termination pointers, synchronous auto-start, and zero native wait/delete/delay
calls.

## Reserved slot-7 TLS root and errno

`EOS_RUST_TLS_SLOT` is permanently 7. The real SDK build statically asserts
`OS_THREAD_USER_TLS_CNT > 7`; the mapping contract proves current-thread get/set calls pass a null
native thread pointer and slot 7 exactly. The SDK binary appears to accept index 8 despite an
advertised count of 8, but the compatibility layer deliberately relies only on the documented
slot 7.

One lazily allocated, zeroed per-thread root owns compatibility errno, public thread identity,
dynamic key/value state, destructor pass state, and four reserved runtime words. Every internal
errno write now resolves through this root; the MARTOS bootstrap process-global errno cell was
removed. Root get, allocation, install, clear, or free failures are fail-fast with a direct port
diagnostic because the errno pointer ABI cannot safely report root-construction failure.

Rust-created thread exit order is: invoke the start routine; run key destructors; clear slot 7;
free the root; publish the exact result and completion; then return to the native wrapper for
kernel auto-reaping. The root and errno remain valid during callbacks. Main and ordinary external
EOS threads may lazily allocate a root, but MARTOS supplies no safe external-thread termination
hook, so those roots and any associated public identity record persist. Host emulation uses
private pthread keys without a host key destructor to match this target limitation rather than
overstating cleanup coverage.

## TLS keys and destructor lifecycle

Keys are monotonic nonzero tokens and are never reused. Deleted registry records remain as
identity history, making capacity heap-limited and preventing ABA. Deleted values immediately
become inaccessible and are discarded when their thread root is cleaned; key deletion never runs
a destructor. `setspecific(NULL)` clears a value, and allocation failure leaves an old/null value
unchanged. Validation and per-root value access occur under the key-registry lock so concurrent
deletion cannot expose stale values.

Cleanup makes at most four passes. Each pass snapshots the highest existing key identity, then
selects non-null values in ascending identity order. A selected value is cleared before its
callback and receives explicit in-flight callback ownership before the registry lock is released.
Thus deletion after selection marks the key inactive but cannot invalidate the copied callback;
the acquired callback runs once and releases ownership afterward. Deletion before selection
skips the callback. A destructor-created key is outside the current pass snapshot and becomes
eligible on the next pass. Reinstallation during the fourth callback is discarded with no fifth
call. No runtime lock is held while user code executes.

Tests cover ordered callbacks, clearing before callback, reinstallation, deletion/creation from a
callback, exactly four passes, result publication after destructor completion, detached cleanup,
and deterministic concurrent deletion after callback selection.

## Verification

- Focused behavioral thread/TLS/destructor and MARTOS mapping: 4/4 passed.
- Fully instrumented focused TSan thread/TLS/destructor: 3/3 passed.
- Fresh normal Release host configure/build/CTest: 28/28 passed.
- Fresh strict `-O0 -Wall -Wextra -Werror -pedantic` C/C++ build/CTest: 28/28 passed.
- Fresh Release `-O3 -Wall -Wextra -Werror -pedantic` C/C++ build/CTest: 28/28 passed.
- Real MARTOS `BUILD_TESTING=OFF` Release build against
  `/home/dev/code/gpt-test/lib/martos-smp-14.0.39` with `-Wall -Wextra -Werror`: passed.
- Host and MARTOS exact export checks: 66/66 each, with no production test seam.
- MARTOS undefined audit: only expected native/runtime services; no global `errno`,
  `__errno_location`, `os_thread_wait`, `os_thread_delete`, `os_delay`, or `pthread_*`.
- Direct `BUILD_TESTING=OFF` consumer linking only `EOS::RustABI`: passed.
- ARM Cortex-A9 softfp layout probe: passed and emitted ELF32 little-endian ARM EABI5 `REL`.
- `git diff --check`: no diagnostics.

## Self-review and limitations

Self-review traced every record reference and state transition, auto-start/create-failure path,
completion/detach window, allocation/lock/wait/cleanup failure, TLS root transition, key deletion
race, callback pass boundary, public status, fixed layout, export, target mapping, and native
undefined. Common code contains no native pthread/MARTOS type or native handle. Host pthread
references remain private to the host port, and MARTOS `os_*` references remain private to its
port. No Task 8+ public API or private seam appears in production archives.

Known limitations are deliberate and public: native diagnostic names do not change after create;
yield is unsupported; external/main-thread roots and identity records persist; deleted key
metadata is retained to guarantee nonreuse; and deployment to hardware remains manual. The commit
containing this report is the single `runtime: add EOS threads and library TLS` Task 7 commit; its
hash is recorded in the controller handoff because a committed file cannot contain its own stable
cryptographic commit hash.

## Fix Round 1: auto-start publication ownership and strict attributes

Independent review of `c4ddbe8347231270caa8902b4a3ca352ab0cc2ff` found two issues and
returned a not-ready verdict. Both received deterministic RED tests before production changes:

- an auto-started child calling `detach(self)` and completing before native create returned
  produced `auto-start self-detach destroyed the record before create returned`; the child could
  drop both the registry and child references, free the record, and leave the creator reading
  `record->identity` after free;
- a table of partially zero, reserved-word, destroyed, invalid-magic, zero-stack, and misaligned
  attribute representations produced `malformed attr getstacksize must fail` because any
  `words[0] == 0` had been treated as the default while ignoring the remaining words.

Creation now publishes records with three explicit owners: registry, child, and creator call. The
creator owner remains held across the entire native auto-start call and until the status/output
decision is complete under the registry lock. A child may therefore detach itself, drop the
registry reference, complete, and drop the child reference without freeing the record before
create returns. On success the creator copies the identity while locked, drops its reference, and
destroys only if self-detach already left it as the final owner. On a conforming create failure,
the rollback drops registry, never-started child, and creator ownership exactly once. The existing
fail-fast policy for a malformed native implementation that starts then returns failure remains.
An adjacent auto-start `join(self)` test confirms `EDEADLK` is returned safely and the record stays
joinable for its creator.

Attributes now have exactly two accepted representations: all four words zero, or the internal
live magic plus a minimum/aligned stack and zero reserved words. Get, set, destroy, and create all
use the same validator. Every partially zero representation, nonzero reserved word, destroyed
magic, invalid magic, zero/subminimum/misaligned live stack, and malformed create is rejected with
`EINVAL`; rejected create outputs remain zero.

Fresh post-fix verification:

- focused thread/TLS/destructor, MARTOS contract, and fully instrumented TSan set: 7/7;
- normal Release host suite: 28/28;
- strict `-O0 -Wall -Wextra -Werror -pedantic` C/C++ suite: 28/28;
- strict Release `-O3 -Wall -Wextra -Werror -pedantic` C/C++ suite: 28/28;
- actual MARTOS 14.0.39 strict SDK build, exact 66-symbol host/MARTOS exports, forbidden native
  undefined audit, direct consumer, and ARM EABI5 relocatable layout probe: passed.

Self-review re-traced every reference count for ordinary completion, join, detach-before/after
completion, self-detach during create, self-join during create, normal create failure, and
start-then-fail abort. It also checked every public attribute operation against the shared exact
representation validator. Fix Round 1 is committed separately as
`runtime: close EOS thread publication races`; its hash is recorded in the controller handoff.
