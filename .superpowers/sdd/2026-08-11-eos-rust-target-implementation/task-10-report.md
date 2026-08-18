# Task 10 Report — EOS Process Services

## Outcome

Task 10 adds the fixed process ABI and its host and MARTOS-SMP 14.0.39 ports: spawn, blocking
wait, try-wait, kill, close, argument copying, stdio bridging, descriptor pinning and
close-on-exec filtering. The accepted 113-symbol ABI grows by exactly the five requested calls
to 118. Public values contain only fixed-width compatibility identities, integers, counted
arrays, and compatibility pointers; no MARTOS application, thread, stdio, or status structure
crosses the public boundary.

The implementation uses an EOS worker thread around the pinned SDK's application load/run/unload
primitives and never calls `fork`, `exec`, `os_system_run_string`, or `os_thread_wait`. It never
mutates the global environment or process cwd around a spawn. MARTOS-SMP 14.0.39 supports the
proven stdin/stdout callback bridge and inherited stderr. An explicit environment, cwd, stderr
redirection, or arbitrary inheritable descriptor request is rejected with compatibility
`ENOTSUP` because this SDK cannot isolate or faithfully route those options. The host port
implements the complete future-port state machine so all of those common-layer paths remain
deterministically exercised.

The implementation is included in the commit containing this report with exact subject
`runtime: add EOS process services`; the handoff records its resulting hash. The starting base
is `212e5a5826f08a6a84c07e31246cc27b89f83f7f`.

## Public contract

The public contract is deliberately independent of native layouts:

- `eos_rust_process_t` is a nonzero, monotonic, non-reused 32-bit identity.
- `eos_rust_process_status` is 32 bytes, aligned to four bytes, with `kind` at offset 0,
  signed `code` at offset 4, and six reserved words at offset 8.
- `eos_rust_spawn_request` is exactly the approved counted request: 96 bytes on x86-64 and 68
  bytes on ARM. On ARM it is aligned to four bytes with `argv` at 4, `envp` at 12, `cwd` at 20,
  `stdin_fd` at 24, `flags` at 36, and `reserved` at 40.
- Stdio descriptor `-1` means inherit compatibility descriptor 0, 1, or 2. Flags and all seven
  reserved request words must be zero.
- `wait` returns a cached status and is repeatable. `try_wait` returns 0 while running, 1 with a
  completed status, and -1 on error. `close` immediately invalidates the public identity without
  waiting and the worker self-reaps its private record. A delivered kill reports
  `EOS_RUST_PROCESS_TERMINATED` with code 1; ordinary completion reports
  `EOS_RUST_PROCESS_EXITED` and the child return code.
- The five new exports are `eos_rust_spawn`, `eos_rust_process_wait`,
  `eos_rust_process_try_wait`, `eos_rust_process_kill`, and `eos_rust_process_close`.

The Rust 1.97.1 Unix process consumer was audited for the later Task 11 adapter: it needs owned
program/argv/env/cwd input, three stdio choices, a stable child identity, try/blocking wait,
kill, and repeat-safe cleanup. Task 10 supplies those stable primitives only; it does not modify
Rust libc declarations or PAL code.

## TDD RED evidence

The independent process and process-redirection tests were authored and linked before any
production process source existed.

1. The first `process_test` build failed at link with exit status 2. The unresolved production
   entry points were `eos_rust_spawn`, `eos_rust_process_wait`,
   `eos_rust_process_try_wait`, `eos_rust_process_kill`, and
   `eos_rust_process_close`; its independent host observation/live-record hooks were unresolved
   as well.
2. The redirection test initially exposed a test-only missing private include. After correcting
   that test harness, its independent link RED failed with exit status 2 and unresolved
   `eos_rust_spawn`, `eos_rust_process_wait`, `eos_rust_process_close`, and the inherited-fd
   observation hooks. No production source was edited before these two corrected link REDs.
3. The MARTOS process contract test then failed at link with exit status 2 because the target
   option gate and exact kill-mapping contract did not exist.

Every later defect or missing failure seam was returned to RED before its fix:

- synchronous-record creation fault coverage failed to link with the undefined create-failure
  hook (exit 2);
- the first real target redirection mapping test failed to compile with implicit declarations of
  its missing shared mapping functions (exit 2);
- block-fixture reuse failed at runtime with exit 1 and
  `a reused block fixture must not inherit the prior release state`;
- descriptor close/reuse while a child held redirection pins failed at runtime with exit 1 and
  `child pins did not survive parent close/reuse`;
- recoverable kill-failure coverage failed to link with the undefined kill-failure hook (exit 2);
- irreversible unload-failure coverage failed to link with the undefined unload-failure hook
  (exit 2);
- expanded MARTOS enumeration/count/status-allocation/unload coverage failed to compile with
  implicit declarations of the missing generic enumeration and cleanup-policy functions
  (exit 2);
- `process_tsan` reported a race between host-fixture reset and late process-record destruction;
  the live-record interval was corrected to cover allocation through final free;
- the first strict O0 and O3 builds both stopped under `-Werror` on the same test-only
  missing-field-initializer warning; explicit zero initialization made both strict matrices
  green.

Systematic debugging separated these product/test defects from two environment facts. The
managed sandbox denies host socket creation with `EACCES`, so the unchanged full binaries were
run with the established scoped elevation. On the resumed final audit, `cmake` was absent from
`PATH`; existing build metadata identified `/tmp/eos-cmake-runtime/bin/cmake`. Two rejected
configure attempts then honestly identified the exact pinned interface names
`EOS_RUST_PORT=martos_14_0_39` and `EOS_SDK_ROOT`; neither attempt reached compilation.

## State, ownership, and concurrency design

One scheduler-aware private runtime lock protects the process registry. A record starts with
three explicit references: registry visibility, worker ownership, and creator handoff. Public
calls acquire temporary references. `close` removes the registry reference exactly once, the
creator drops its reference after publication or rollback, and the worker drops its reference
after publishing completion. Destruction occurs only on the final reference and releases all
owned strings, descriptor pins, synchronization state, and the record. Cleanup/free/destroy
failures that cannot be rolled back fail fast, consistent with the existing ABI ownership
policy.

The creator locks the per-record completion object before creating the worker. The worker must
publish a separate load result before spawn can return; successful spawn therefore means the
ELF loaded, while later run failure is observable through wait. Completion and the cached child
status are published under that same synchronization object. Atomic completion state supplies
the lock-free lifetime observation required by the host race fixture, while all status and kill
fields remain protected by the completion lock.

Program, argv, environment entries, and cwd are copied before the worker starts. The request
counts are stored before copying so any partially constructed vector is correctly freed during
rollback. No borrowed caller storage survives spawn.

Each stdio descriptor and each inheritable descriptor is pinned by incrementing the underlying
fd object's reference, not by retaining only a slot number. Snapshotting occurs under the fd
table lock, skips descriptors below 3, filters `CLOEXEC`, and excludes explicit stdio
redirections. Thus a parent close and slot reuse cannot retarget or destroy a child's pinned
object. Pins are released exactly once on worker completion or pre-worker rollback. The direct
bridge supports file, pipe, socket, console, and null fd object kinds without exposing their
native values.

Kill takes a temporary record reference and coordinates each target attempt with completion.
The MARTOS adapter snapshots live thread pointers, obtains each live application name, and calls
`os_thread_delete` only when that name is a complete exact match for this record's unique
`eos.rust.%08x` application name. Prefixes, suffixes, decoys, missing/deleted threads, and the
completion-vs-kill race are explicitly covered. A successful native delete marks the record
terminated; a race in which no matching live thread remains retries until the worker publishes
completion, whose ordinary outcome wins.

## Authoritative MARTOS-SMP 14.0.39 evidence

The target implementation is based on the pinned SDK at
`/home/dev/code/gpt-test/lib/martos-smp-14.0.39`, principally
`martos/inc/martos_smp.h` and the shipped `martos_smp_14.0.39.elf`:

- `OS_APP_MAX_ARG_COUNT` is 10 and `OS_APP_MAX_ARG_LENGTH` is 128.
- `os_app_load_elf(const char *appName, const char *elfFile, uint32 features,
  uint32 domainId)`, `os_app_run(const char *appName, const char *entryPointName,
  os_app_context *cntxt)`, and `os_app_unload(const char *appName, bool silent)` are the
  application lifecycle primitives.
- `os_app_context` contains signed `argc`, `argv[10]`, `returnValue`, a
  `const os_stdio_redirection *`, an `os_thread *`, and an `os_console *`.
- `os_stdio_redirection` provides byte read/write functions, read/write end callbacks, and
  separate parameters. The real port maps compatibility stdin to `read`, stdout to `write`,
  and returns the documented native OK/end-of-object/device-error statuses from exact one-byte
  compatibility results.
- Header and binary inspection showed that `os_app_run` is synchronous from this caller's
  perspective and its context thread is not safely available for asynchronous kill until run
  returns. This rules out keeping one unverified context pointer as a process handle.
- `os_thread_get_count(uint32 *)`,
  `os_thread_get_status(os_thread_status *, uint32, uint32 *)`,
  `os_thread_get_app_name(os_thread *, char *, uint32)`, and
  `os_thread_delete(os_thread *)` support a fresh live snapshot followed by exact app-name
  validation. `OBJECT_NOT_FOUND` from name/delete is the documented completion race.
- Binary inspection of `os_app_run` confirmed that native error-byte output does not share the
  provided stdout callback, apart from native newline behavior. Therefore explicit stderr
  redirection cannot be claimed and is rejected; inherited stderr remains supported.
- Header text says unload can fail when application threads remain. Disassembly around
  `os_app_unload`/its recursive part showed a global load lock, live-thread count/status checks,
  allocation, and destructive teardown. A failed unload can leave a resident loaded app, so it
  is an irreversible cleanup failure and the real port fails fast. Load/run status remains
  recoverable and is reported by spawn/wait respectively.

The private deterministic MARTOS contract is called by the real port, not duplicated in its
test. It pins unsupported-option decisions, byte redirection mappings, exact matching,
count/status snapshot capacity, first and second allocation failures, snapshot failure,
not-found races, delete errors, successful deletion, release failure fail-fast, and unload
failure fail-fast.

## Host test coverage

The host port provides deterministic programs for missing ELF, ordinary exit, argument capture,
blocking execution, copied-argument blocking, stdout, stderr, and run error. Its hooks cover
allocation, synchronization creation/wait, worker creation, kill, and unload failures plus
argv/env/cwd and inherited-fd observations.

The final tests cover request pointer/count/flag/reserved validation; deep copies of arguments,
environment, and cwd; load vs run failure; normal and child exit; try and blocking wait; kill;
multiple concurrent waiters; repeated wait and close; monotonic stale-handle rejection; close of
a running child and self-reap; block-state reuse; kill failure followed by a successful wait;
allocation/sync/thread/wait rollback; inherited/null/piped stdin/stdout/stderr; explicit stdio
exclusion; `CLOEXEC` filtering; descriptor ABA while a blocked child owns pins; loader rollback
EOF; and redirection read/write failures. ThreadSanitizer instruments both the process test and
the compatibility archive.

## Final verification

All final-state commands were run after the last production and test edit.

- Normal host configure/build plus elevated serial CTest in `build/eos-abi-host`: 43/43 passed
  in 84.73 seconds.
- Strict Debug host configure/build plus elevated serial CTest in `build/eos-abi-task10-o0`,
  with C and C++ flags `-O0 -Wall -Wextra -Werror -pedantic`: 43/43 passed in 78.77 seconds.
- Strict Release host configure/build plus elevated serial CTest in
  `build/eos-abi-task10-o3`, with C and C++ flags
  `-O3 -Wall -Wextra -Werror -pedantic`: 43/43 passed in 143.89 seconds.
- `process_tsan` passed without a race report in all three matrices.
- Fresh real MARTOS configure/build:

  ```text
  /tmp/eos-cmake-runtime/bin/cmake -S src/tools/eos-abi \
    -B build/eos-abi-task10-martos-final -DBUILD_TESTING=OFF \
    -DEOS_RUST_PORT=martos_14_0_39 \
    -DEOS_SDK_ROOT=/home/dev/code/gpt-test/lib/martos-smp-14.0.39 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS='-Wall -Wextra -Werror' \
    -DCMAKE_CXX_FLAGS='-Wall -Wextra -Werror'
  /tmp/eos-cmake-runtime/bin/cmake --build \
    build/eos-abi-task10-martos-final --parallel 2
  ```

  Result: `libeos_rust_abi.a` built successfully, exit 0.

- `tests/check_exports.py` passed independently for the O3 host and fresh MARTOS archives.
  `nm -g --defined-only | sort -u | wc -l` reported exactly 118 for each archive, matching the
  118-line manifest.
- The MARTOS undefined scan found no global `errno`, `__errno_location`, `pthread_*`,
  fork/vfork/exec/spawn/waitpid, setenv/unsetenv/getenv/chdir mutation, `os_system_run_string`,
  or `os_thread_wait`. Required target dependencies include the audited app lifecycle,
  thread count/status/name/delete, allocation, synchronization, and stdio functions.
- Defined-symbol scans of both production archives found no host or MARTOS test seam.
- A fresh direct `BUILD_TESTING=OFF` consumer configured, compiled, and linked only through
  `EOS::RustABI`; exit 0.
- The ARM command was:

  ```text
  /usr/bin/clang --target=armv7a-none-eabi -mcpu=cortex-a9 \
    -mfloat-abi=softfp -mfpu=vfpv3 -fPIC -std=c11 \
    -Wall -Wextra -Werror -pedantic -Isrc/tools/eos-abi/include \
    -c src/tools/eos-abi/tests/arm_layout_probe.c \
    -o /tmp/eos-task10-arm-layout.o
  /usr/bin/llvm-readobj-21 --file-header --arch-specific \
    /tmp/eos-task10-arm-layout.o
  ```

  Result: exit 0; ELF32 little-endian ARM relocatable, ARM v7 Application profile, Thumb-2,
  VFPv3. All process layout static assertions passed.
- The explicit 17-file Task 10 path set passed `git diff --no-ext-diff --check` with no output.

## Changed scope

Six new files:

- `src/tools/eos-abi/src/eos_process.c`
- `src/tools/eos-abi/src/eos_process.h`
- `src/tools/eos-abi/ports/martos_14_0_39/eos_port_martos_process_contract.h`
- `src/tools/eos-abi/tests/process_test.cpp`
- `src/tools/eos-abi/tests/process_redirection_test.cpp`
- `src/tools/eos-abi/tests/martos_process_contract_test.c`

Eleven modified files:

- `src/tools/eos-abi/CMakeLists.txt`
- `src/tools/eos-abi/include/eos_rust_abi.h`
- `src/tools/eos-abi/src/eos_abi.c`
- `src/tools/eos-abi/src/eos_port.h`
- `src/tools/eos-abi/src/eos_fd_table.c`
- `src/tools/eos-abi/src/eos_fd_table.h`
- `src/tools/eos-abi/ports/host/eos_port_host.c`
- `src/tools/eos-abi/ports/martos_14_0_39/eos_port_martos.c`
- `src/tools/eos-abi/tests/abi_contract_test.cpp`
- `src/tools/eos-abi/tests/arm_layout_probe.c`
- `src/tools/eos-abi/tests/expected-exports-v1.txt`

The ignored report is force-added with those files. Every new index entry is forced to mode
100644 on DrvFS. No progress ledger, Rust libc binding, Rust PAL, capability publication, board
deployment, or Task 11+ surface is changed.

## Self-review and remaining concerns

Self-review retraced every record reference from allocation to final destruction; creator and
worker rollback; load/completion ordering; status and errno precedence; wait/kill/close races;
identity exhaustion; every descriptor pin and rollback; close/reuse ABA; target capability
gates; exact kill-name matching; snapshot allocation/release; unload fail-fast; common/native
type separation; test-seam exclusion; export/undefined contracts; ARM/x86 layout; and the exact
Task 10 path set. No further defect was found.

Deliberate current-port limitations remain: MARTOS-SMP 14.0.39 cannot safely isolate a per-child
environment or cwd, cannot route explicit stderr through the application stdout callback, and
cannot inherit arbitrary compatibility descriptors into the native application. Those requests
return `ENOTSUP` rather than mutating process-global state or pretending that redirection
succeeded. Target app execution still requires device-level validation on the XC7Z030/XC7Z045
board families; this task verifies the real pinned SDK compile and deterministic primitive
mapping but does not deploy to hardware.

## Fix Round 1 — inherited native stderr identity

Independent review found a Critical false-success path: `stderr_fd == -1` promises to inherit
compatibility fd 2, but `dup2` can replace that slot, while the pinned MARTOS application path
does not invoke the stdout redirection callback for native error-byte output. The finding was
verified against the fd table, MARTOS port, header, and binary evidence before editing.

The new real fd-table/process test saves fd 2 with compatibility `dup`, creates a compatibility
pipe, replaces fd 2 with its writer using compatibility `dup2`, selects the real MARTOS process
capability set in the host executable, and attempts an ordinary `stderr_fd == -1` spawn. It
always restores fd 2 from the saved descriptor before asserting the result, then proves the
restored original stderr is accepted and closes every fixture fd. Independent MARTOS mapping
coverage requires the real port's shared capability function to publish only native-stderr
inheritance.

RED evidence:

- `process_redirection_test` first failed to link with undefined
  `eos_host_test_use_martos_process_capabilities` (exit 2).
- `martos_process_contract_test` independently failed to compile with implicit declaration of
  `eos_martos_process_capabilities` (exit 2).
- After adding only the test selector and real target capability mapping, the unchanged common
  spawn logic ran the real fd-table fixture and failed with exit 1:
  `MARTOS must reject inherited stderr after compatibility fd 2 is remapped`.

The fix adds a private native-stderr capability bit. Host production retains full callback
stderr support. MARTOS publishes only native stderr. After fd2 is pinned, common code accepts
that native-only capability only when the pin is the console object whose native stream word is
exactly 2; a pipe writer or stdout console remapped into slot 2 is not representable and returns
compatibility `ENOTSUP` (45). This uses only private fd/port state and does not change the public
ABI. A diagnostic rerun exposed that the first test expectation had incorrectly used host errno
95; the observed compatibility result was correctly 45, so the test literal was corrected and
the temporary diagnostic removed.

Focused GREEN: strict O3 rebuilt `process_redirection_test`,
`martos_process_contract_test`, and `process_test`; all three executables returned exit 0.

## Fix Round 1 — partial kill delivery

Independent review found an Important result-precedence defect. The MARTOS exact-name helper can
delete one matching live thread, increment `matched`, then receive a real error deleting a later
exact match. Common process code previously remembered delivery only when the overall adapter
status was OK. It correctly returned the later kill error, but a subsequent wait could therefore
report ordinary exit or run failure instead of the termination already delivered.

The requested target case was added with two exact names, a successful first delete, and status
55 from the second. Against the pre-fix code it returned status 55 with `matched == 1` and both
delete calls observed, so this target-only half was honestly a characterization GREEN rather
than a RED: the target helper already preserved both facts. The actual defect was at the common
integration boundary. The real process test initially failed to link with undefined
`eos_host_test_fail_process_kill_after_match` (exit 2). After adding only a deterministic host
adapter seam that delivers a kill, wakes the blocked child, sets `matched == 1`, and then returns
native status 25, `process_test` failed at runtime with exit 1:
`wait lost a delivered termination after a later kill error`.

The one-line common fix records `kill_delivered` whenever `matched` is nonzero, independent of
the overall adapter status. `eos_rust_process_kill` still maps and returns the later status 25 as
compatibility I/O error, while completion and wait observe the protected delivery bit and return
`EOS_RUST_PROCESS_TERMINATED`, code 1. Not-found name/delete races still leave matched zero;
completion wins. A complete no-match operation still retries until completion. A failure before
any delete still reports the error and leaves the child waitable.

Focused GREEN: strict O3 rebuilt and ran `martos_process_contract_test`, `process_test`,
`process_redirection_test`, and fully instrumented `process_tsan`; all returned exit 0 with no
ThreadSanitizer report.

## Fix Round 1 — concurrent host process records

Independent review found an Important host-adapter defect. The host fake kept one global name,
release/kill state, active flag, copied request, and inherited-fd vector. A second live load
cleared and then overwrote the first record. Killing the first unique name could no longer match
and common kill would retry indefinitely.

The new real process test creates four `CLOEXEC` pipes and starts two simultaneous
`/host/block-copy3` children. They have literal distinct argv (`child-one`/`child-two`),
environment (`CHILD=one`/`CHILD=two`), cwd (`/work/one`/`/work/two`), and stdin/stdout pipe
objects containing `one` and `two`. It verifies both keyed observations coexist, core and host
counts are two, kills/waits/closes the first while the second remains running, then independently
kills/waits/closes the second, reads the correct bytes from both output pipes, closes all eight
fixture descriptors, and observes zero core live records, host active records, and host loaded
records. The same test source is compiled into fully instrumented `process_tsan`.

RED evidence:

- The first build failed at link with exit 2 and unresolved keyed started/active/argc/envc/
  argument/environment/cwd plus active-count and record-count hooks.
- After adding only minimal keyed observation hooks over the old singleton, the executable
  reached the real two-child state and failed deterministically with exit 1:
  `second live process clobbered the first host adapter record`. It stopped before issuing the
  old indefinitely retrying kill.

The host adapter now owns a guarded fixed-capacity table of 64 private records keyed by the
unique internal application name. Load claims and zeros one non-live slot and assigns a monotonic
observation sequence. Run resolves that exact live slot and owns its started/active,
release/killed, argv/env/cwd, and inherited-descriptor observations. Kill resolves only the exact
live name and signals only that record. Unload marks that record non-live; immutable observation
bytes remain available until the slot is safely reused by a later load or the test fixture is
reset. Legacy no-argument hooks select the latest sequence for existing single-child tests;
new identity hooks map the public monotonic identity to its unique internal name under the same
guard. Count hooks scan live/active slots under the guard. No public ABI changed.

Focused GREEN: strict O3 rebuilt and ran `process_test`, `process_redirection_test`,
`martos_process_contract_test`, the production host archive, and fully instrumented
`process_tsan`; every command returned exit 0 and TSan emitted no race report.

The reviewer also reported a Minor missing proof that caller argv/env/cwd storage may be
mutated/freed immediately after spawn. Per the five-round SDD workflow this Minor is deliberately
deferred and controller-owned in the progress ledger; this fix round does not edit that ledger.

## Fix Round 1 final verification and commits

All full matrices below were freshly configured after the three blocking fixes:

- normal Release host build in `build/eos-abi-task10-fix1-normal`: 43/43 passed, 0 failed,
  128.52 seconds;
- strict Debug host build in `build/eos-abi-task10-fix1-o0`, with C and C++ flags
  `-O0 -Wall -Wextra -Werror -pedantic`: 43/43 passed, 0 failed, 166.89 seconds;
- strict Release host build in `build/eos-abi-task10-fix1-o3`, with C and C++ flags
  `-O3 -Wall -Wextra -Werror -pedantic`: 43/43 passed, 0 failed, 145.56 seconds.

Every matrix passed `process`, `process_redirection`, `martos_process_contract`, and the expanded
fully instrumented `process_tsan`; no race report occurred. The full socket tests used the same
scoped sandbox elevation recorded earlier.

The real pinned target command was freshly configured in
`build/eos-abi-task10-fix1-martos` with `BUILD_TESTING=OFF`,
`EOS_RUST_PORT=martos_14_0_39`, SDK root
`/home/dev/code/gpt-test/lib/martos-smp-14.0.39`, Release, and C flags
`-Wall -Wextra -Werror`. It built the archive successfully with exit 0. CMake noted only that
`CMAKE_CXX_FLAGS` was unused because the target archive is C-only.

The independent export checker passed for the fresh strict O3 host and MARTOS archives; direct
`nm` counts were exactly 118/118. The MARTOS forbidden-undefined scan remained empty for global
errno, pthread/process creation/wait, environment/cwd mutation, command strings, and
`os_thread_wait`. The required audited app load/run/unload, thread count/status/name/delete, and
stdio symbols were present. Production test-seam scans were empty. A fresh direct
`BUILD_TESTING=OFF` consumer configured, compiled, and linked with exit 0. The ARM Cortex-A9
softfp/PIC fixed-layout probe compiled with exit 0 and remained ELF32 little-endian ARM,
relocatable, v7 Application profile, Thumb-2, VFPv3.

Fix commits:

- `14b2d97f1716eb984802d611ee15f9c6ab876d15` —
  `fix: validate inherited MARTOS stderr`;
- `7db6b8259f0961b09a78ffa834ab672e6cbcffde` —
  `fix: preserve partial kill delivery`;
- `9be0dcee7d360e8c91d3e031149275e5fa3abd63` —
  `fix: isolate concurrent host processes`.

The exact Task 10 source/report diff check produced no output. All seven Task 10-created files
remain mode 100644 in `HEAD`. The only unrelated worktree status is the controller-owned
`progress.md` Minor-review line; it is intentionally not staged or committed by this task.
