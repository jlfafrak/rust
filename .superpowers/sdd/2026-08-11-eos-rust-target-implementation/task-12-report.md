# Task 12 Report — EOS Unix PAL and Runtime/Random Hooks

## Status

The EOS Unix PAL type-checks cleanly on Rust 1.97.1. The exact no-bypass command
`./x check library/std --target armv7a-unknown-eos-eabi` passes, the std-smoke
crate type-checks against the freshly produced EOS metadata, and the native host,
strict O0, strict O3, MARTOS, export, consumer, ARM, layout, target-cfg, and
assembly regressions pass.

The exact `./x build library/std --target armv7a-unknown-eos-eabi` does not link:
after compiling all target crates it stops only because `eos-rust-link` is not
installed. The approved plan assigns that linker wrapper to Task 15. The literal
plan command `./x test library/std --target armv7a-unknown-eos-eabi --no-run` is
not valid bootstrap syntax in Rust 1.97.1; the documented equivalent, `--run
never`, also reaches the target link and stops only at the missing Task 15
wrapper. Neither result is represented as passing in this report.

The starting base is `edec4e3d0e273e511b72fef7efb91909b6a52385`. The sources
and this report are committed with the required subject `library: port the Unix
PAL to EOS`; the handoff records the resulting commit hash.

## Approved boundary corrections

Three explicit 2026-08-18 rulings were applied:

1. Task 12 owns the formerly deferred exact `library/std` check and must solve
   its failures through honest EOS PAL routing, not invented libc names or a
   borrowed OS cfg.
2. The unreleased v1 hash boundary is corrected consistently to
   `int32_t eos_rust_hash_seed(uint64_t *, uint64_t *)` / Rust `-> c_int`.
   Success is zero; a lock failure returns `-1`, stores the existing mapped EOS
   `EBUSY` value 16, preserves both outputs, and does not advance the sequence.
   No export was added for this signature correction.
3. Bootstrap may list only `armv7a-unknown-eos-eabi` in
   `STAGE0_MISSING_TARGETS` and must exclude EOS from `use_host_linker` so the
   target specification's `eos-rust-link` is respected. This is the only
   bootstrap/file-list expansion. The wrapper itself remains Task 15 work.

The plan also authorizes exactly two new fixed-width native services in this
task: `eos_rust_runtime_cleanup(void)` and `eos_rust_cpu_count(void) -> uint32_t`.
They grow the stable manifest from 118 to exactly 120 exports. No other service
was added.

## Native service design and TDD

### Runtime cleanup ownership

The authoritative Task 7 TLS implementation owns a per-thread compatibility
root in reserved native TLS slot 7. `eos_rust_runtime_cleanup` is therefore a
terminal, current-thread-only, idempotent operation which calls the existing
`eos_tls_cleanup_current` implementation:

- it runs registered pthread/Rust TLS destructors for the current root;
- it frees the current root's value records;
- it clears reserved slot 7 before freeing the root;
- a second call observes an empty slot and returns without rerunning destructors;
- reserved-slot read/clear failure or value/root free failure emits the existing
  direct diagnostic and aborts under the established irreversible-failure
  policy.

It deliberately does not reset immutable environment snapshots, close standard
or arbitrary descriptors, or reclaim process-wide fd/process/synchronization
registries. Those are application-lifetime resources; broad cleanup would race
other threads/readers and expand the reviewed contract. Standard descriptors
created by runtime initialization remain available until EOS application
teardown.

Before production implementation, `runtime_services_test.cpp` failed to link on
the absent cleanup and CPU-count services. The GREEN test proves one destructor
call, cleanup idempotence, and fail-fast behavior for injected TLS read, clear,
and free failures. The child-process diagnostics in the passing test are the
expected abort paths. The test also requires the host service to return exactly
two.

### CPU count

Both supported XC7Z030 and XC7Z045 processors have two Cortex-A9 cores. The host
port returns fixed `2`; the MARTOS port calls authoritative
`os_core_get_count()`. A new MARTOS contract header statically requires
`OS_MAX_CORE_CNT == 2`, and its independent contract test proves one call to the
native source. `available_parallelism` accepts only the required value two and
otherwise returns `UNKNOWN_THREAD_COUNT` rather than reporting a false count.

### Hash-seed error result

The hash RED attempted to compare the previous `void` result to `-1`, proving
that lock failure could not be observed. The header, common implementation,
vendored libc binding, native tests, signature/link audits, and Rust random
consumer now agree on the corrected result type. The fault test proves `-1`, EOS
errno 16, unchanged outputs, and unchanged next sequence. Lock-release failure
continues to abort under the pre-existing fail-fast policy. The random policy
file is the literal body required by the brief: secure-byte requests panic, and
HashMap seeding asserts native success.

## Compiler RED to GREEN

The std-smoke crate and cfg/static audits were added before PAL implementation.
The initial staged check reported 212 errors in
`/tmp/eos-task12-std-check.log`. Its first groups were missing EOS Unix
`fs`/`raw` modules, signal and directory APIs, unsupported `openat`/`ftruncate`,
AF_UNIX structures, missing random/thread/current-exe routes, pointer-valued
directory assumptions, and `usize` versus fixed `u32` call shapes.

Compiler iterations were reduced without adding libc fiction:

- check 1: 212 error lines;
- check 2: 183 error lines after the first EOS modules and call-shape routes;
- check 3: seven actual type/name errors plus denied warnings (nine `error`
  lines), limited to passwd/home fallback, type inference, and lints;
- check 4: no type/name errors, only four denied dead-code warnings;
- check 5: three `off64_t` errors exposed a misplaced cfg on a similar import;
- check 6: exit 0 under the temporary diagnostic-only target-sanity bypass;
- final exact no-bypass check: exit 0, including a fresh post-self-review rerun
  after correcting `open` argument scope.

The self-review found one genuine non-EOS ABI regression before commit: the EOS
fixed-signature `open` adaptation had changed the mode cast for all Unix targets,
despite the source's documented variadic integer-promotion requirement. A new
cfg regression first failed on the missing non-EOS `c_int` route. The production
code now keeps `c_int` for Unix variadic `open64` and uses `mode_t` only for the
fixed-signature `eos_rust_open`; the regression and exact EOS std check then
passed.

## PAL routing and rationale

- EOS has a private Unix platform module for the metadata/raw aliases needed by
  portable Unix extension traits. The stable stat record has no special-device
  identifier, so `st_rdev()` returns zero explicitly.
- Unix args use EOS-owned atomics with the existing argc/argv semantics.
  Environment iteration routes through stable `eos_rust_environ`; environment
  mutation continues through the existing stable bindings.
- errno links directly to `eos_rust_errno_location`. Error-kind decoding lists
  only errno constants present in the reviewed EOS libc surface; detailed text
  falls back honestly to `EOS error N` because v1 exposes no strerror service.
- PAL initialization requires ABI 1.0 and invokes runtime initialization before
  any other EOS PAL service. Standard descriptors are sanitized through stable
  `open`/`fcntl` shapes. POSIX signal setup and broken-pipe tracking are omitted
  because EOS exposes no signal-handler capability. PAL cleanup invokes the
  terminal native cleanup after `stack_overflow::cleanup`.
- libc supplies pure-Rust fixed `sysconf` values for page size 4096, minimum
  thread stack 4096, hostname maximum 63, and two online processors; unknown
  names set `ENOTSUP` and return `-1`. The binding audit permits exactly this one
  pure-Rust public function while still requiring 120/120 explicit extern links.
- Allocation, fd, positioned I/O, socket, hostname, getcwd, pipe, and pthread
  call sites perform checked or bounded conversions to the reviewed fixed-width
  ABI. EOS uses the default scalar fallback for vectored I/O; no `readv` or
  `writev` symbol was invented.
- File and directory operations use the stable descriptor-valued directory
  interface and caller-owned `dirent`, fixed-buffer realpath, stable fcntl/open,
  and the common non-`openat` recursive removal fallback.
- TCP/IP retains the existing Unix socket implementation with EOS count types.
  The public Unix-domain socket extension module is excluded because v1 has no
  AF_UNIX capability. Nonblocking state routes through fcntl, not ioctl.
- Pipes use the stable two-argument service with `O_CLOEXEC`. Thread creation,
  join/detach, synchronization, and TLS remain on existing pthread-backed Unix
  paths. EOS stack sizes are checked as `u32`, rounded to a 4096-byte page after
  `EINVAL`, thread names are truncated to 63 bytes plus NUL, and yielding uses
  the stable pthread service.
- The EOS process adapter builds the reviewed fixed-width spawn request and
  routes spawn/wait/try-wait/kill/close through the stable services. Closing a
  native process handle is irreversible ownership release, so Drop aborts on a
  close failure rather than silently leaking it.
- The exact separate random module implements the approved panic/HashMap policy
  and is never folded into the Linux random implementation.

The cfg audit covers every touched std/PAL Rust file, rejects any cfg attribute
that combines EOS with Linux, scans Linux-only source trees for EOS, requires
comments naming the stable special-purpose services, checks the literal random
policy, and preserves non-EOS variadic-open promotion.

## Explicit unsupported decisions

The v1 boundary does not expose secure random bytes, symbolic-link reads, file
truncate, file-time mutation, chroot, named pipes, current-executable lookup,
Unix-domain sockets, process replacement, process groups/signals, uid/gid/group
or chroot/setsid process attributes, or pre-exec closures. Fallible Rust APIs
return `Unsupported`; secure random and the infallible current/parent process-ID
entry points panic rather than return invented values. Home-directory fallback
returns `None` when `HOME` is absent. These are capability statements, not
successful stubs.

## Bootstrap/link diagnosis

The first exact no-bypass build, logged at
`/tmp/eos-task12-std-build-exact.log`, stopped at bootstrap target sanity because
stage 0 predates the new built-in EOS target. A focused regression was RED before
adding only EOS to `STAGE0_MISSING_TARGETS`, then GREEN.

The next exact build, `/tmp/eos-task12-std-build-exact-2.log`, compiled ARM
objects but selected host `cc`, producing `relocations in generic ELF (EM: 40)`
and `file in wrong format`. Diagnosis showed `Builder::linker` overrides the
target-spec linker whenever `use_host_linker` returns true. A second focused
regression was RED before excluding EOS from that override, then GREEN. No
bootstrap alias or wrapper was added.

After the correction, `/tmp/eos-task12-std-build-exact-3.log` compiled all
required target crates and stopped solely with:

```text
error: linker `eos-rust-link` not found
  = note: No such file or directory (os error 2)
```

That exact 6:51 endpoint is the approved Task 15 dependency. Disabling dylib,
using host cc, inventing a temporary wrapper, or borrowing later-task work would
make the Task 12 result misleading.

The literal `--no-run` command stops immediately with `unexpected argument
'--no-run'` in `/tmp/eos-task12-std-test-no-run-exact.log`. The Rust 1.97.1
bootstrap equivalent `--run never` reaches the EOS target build and stops only
at the same missing linker after 6:20; its log is
`/tmp/eos-task12-std-test-run-never.log`.

## Verification

Final non-conflicting results:

- Exact `./x check library/std --target armv7a-unknown-eos-eabi`: exit 0; the
  fresh post-self-review run completed in 1:17. Earlier exact GREEN evidence is
  `/tmp/eos-task12-std-check-exact-2.log`.
- Fresh stage-1 std-smoke metadata compile: exit 0 against the 11:55
  `libstd-bc0ddd2c2a05fcf2.rmeta` and matching
  `libpanic_unwind-43bdd812624db0b9.rmeta`; output is
  `/tmp/eos-task12-std-smoke-final.rmeta`. The first direct attempt without an
  explicit matching panic metadata input honestly reported E0463; adding that
  dependency resolved only artifact selection, not source behavior.
- Consolidated libc-link/provenance/toolchain-lock/PAL-cfg/bootstrap suite:
  14/14 passed in 68.583 seconds. The focused PAL audit is 5/5.
- Normal RelWithDebInfo native build: unprivileged CTest honestly failed only
  socket/poll and their TSan twins because of the sandbox; the unchanged
  elevated serial rerun passed 45/45 in 35.02 seconds at
  `/tmp/eos-task12-native-normal-ctest-elevated.log`.
- Strict Debug `-O0 -Wall -Wextra -Werror -pedantic`: build and 45/45 CTest
  passed; `/tmp/eos-task12-native-o0-{config,build,ctest}.log`.
- Strict Release `-O3 -Wall -Wextra -Werror -pedantic`: build and 45/45 CTest
  passed; `/tmp/eos-task12-native-o3-{config,build,ctest}.log`.
- The first MARTOS attempt with `-pedantic` failed solely because the pinned SDK
  `errno.h` intentionally uses GCC `#include_next`. No production source was
  changed. The established strict MARTOS flags `-O3 -Wall -Wextra -Werror`,
  `EOS_RUST_PORT=martos_14_0_39`, SDK
  `/home/dev/code/gpt-test/lib/martos-smp-14.0.39`, and `BUILD_TESTING=OFF`
  built successfully; `/tmp/eos-task12-native-martos-strict-{config,build}.log`.
- Independent export checks pass for the O3 host and MARTOS archives. The
  manifest, header, Rust extern declarations, and both archive symbol sets are
  exactly 120 unique exports. Direct runtime-services, hash-seed, and MARTOS
  runtime-contract executables exit 0.
- The MARTOS undefined scan contains no global errno/`__errno_location`,
  pthread, fork/exec/spawn/waitpid, environment/cwd mutation,
  `os_system_run_string`, or `os_thread_wait` dependency. Defined-symbol scans
  of both production archives contain no host/MARTOS test seam. Host consumer
  tests passed in all three complete native matrices.
- The ARM native header probe compiled and is ELF32 little-endian ARM,
  relocatable, Cortex-A9/v7 Application profile, Thumb-2, VFPv3;
  `/tmp/eos-task12-arm-layout-readobj.log`.
- Exact no-bypass `./x build library/core library/compiler-builtins --target
  armv7a-unknown-eos-eabi`: passed in 1:13;
  `/tmp/eos-task12-core-builtins-build.log`.
- `python3 tests/eos/abi/compare_layouts.py /tmp/eos-task12-layout`: passed,
  `matched 111 ARM layout facts`; `/tmp/eos-task12-compare-layouts-2.log`.
- `./x test tests/ui/target-cfg/eos.rs
  tests/assembly-llvm/targets/armv7a-unknown-eos-eabi.rs`: UI 1/1 and assembly
  1/1 passed in 5:59; `/tmp/eos-task12-target-tests.log`.
- Static FFI audit finds no `extern "C-unwind"` in the touched PAL boundary.
  Existing Rust TLS destructor callbacks use `extern "C"` and
  `abort_on_dtor_unwind`, preserving Rust-only unwind containment; native ELF
  TLS remains disabled by the target specification.
- `git diff --check` is clean. Final staged diff/mode checks are recorded in the
  handoff; DrvFS reports new working files as 0777, so every new source, test,
  crate file, and this report is explicitly normalized to index mode 100644.

## Source scope and concerns

Production changes are limited to the reviewed EOS Unix PAL/call sites, the EOS
libc module, the two authorized native services and corrected hash signature,
their host/MARTOS mappings and tests, and the two authorized bootstrap target
integration lines. Test additions are the std-smoke crate, PAL cfg audit,
bootstrap regressions, and independent native service contracts.

Task 12 intentionally pulled forward the narrow process routing in
`sys/process/unix/common.rs`, `sys/process/unix/mod.rs`, and
`os/unix/process.rs`, plus the new `sys/process/unix/eos.rs` backend, from the
Task 13 file list. That was required to satisfy Task 12's exact std compile gate
and the std-smoke `Command` type-check without inventing POSIX process symbols.
This does not complete or start the remaining Task 13 validation and
deliverables; they are untouched. No progress ledger is changed.

The only external completion dependency is Task 15's `eos-rust-link`: until it
exists, an exact std artifact link and `--run never` no-run test link cannot
complete. The explicit v1 unsupported capabilities above remain visible to Rust
callers. There is no request to widen the native ABI beyond 120 exports.

## Review fix — branch-local stable-service comments

Review verified that the original cfg audit checked only seven hard-coded
service/file pairs and accepted a matching comment anywhere in the file. A
controlled fixture containing the two real EOS `fcntl` branch shapes first
failed with `2 != 0 : []`, proving the audit's false negative before its
implementation changed.

The replacement derives Rust callable names and exact `eos_rust_*` link names
from the EOS libc module. It extracts block-bodied positive EOS cfg/cfg-select
regions and functions in the EOS-only process/random modules. For a positive
EOS cfg applied to a `let` statement, it associates only that statement and the
immediately following shared statement; imported libc aliases in that route are
resolved from their actual `use libc` declarations. It accepts an exact service
comment only inside or immediately adjacent to the resulting region. The exact
random body remains unchanged; its stable-service comment is checked in the
adjacent EOS module-selection arm. The special errno link-name declaration is
checked only against its adjacent source context, not the whole file.

After the control turned GREEN, the production audit honestly reported 18
missing branch-local names: four allocation services, PAL abort/fcntl, two fd
fcntl branches, fs fcntl/opendir, process kill/wait/try-wait/close/abort, and
three pthread paths. Adding only exact adjacent comments made the focused audit
pass 7/7. The full libc-link/provenance/toolchain-lock/PAL-cfg/bootstrap suite
then passed 16/16 in 62.096 seconds. A fresh exact no-bypass
`./x check library/std --target armv7a-unknown-eos-eabi` passed in 1:03, and the
std-smoke metadata check against its freshly updated
`libstd-bc0ddd2c2a05fcf2.rmeta` passed with output
`/tmp/eos-task12-review-std-smoke.rmeta`. These review changes do not alter PAL
behavior or the 120-symbol ABI.

A re-review then found that the first replacement still skipped cfg-applied
statement form when its semicolon preceded the next opening brace. An exact
fixture matching the real `open64` route—EOS `let mode`, followed by the shared
call, with its comment corrupted from `eos_rust_open` to `eos_rust_read`—was RED
with `1 != 0 : []`, proving that the audit found zero regions and violations.
The narrow fix described above models cfg-applied `let` statements plus only
their immediately following shared statement and derives `open as open64` from
the actual libc import. The control turned GREEN 1/1; the focused PAL cfg audit
passed 8/8, the consolidated host guard suite passed 17/17 in 13.557 seconds,
and a fresh exact no-bypass
`./x check library/std --target armv7a-unknown-eos-eabi` passed in 1:01. This
second review fix changes only the audit and this report; it does not change PAL
production code, native runtime behavior, or the ABI.
