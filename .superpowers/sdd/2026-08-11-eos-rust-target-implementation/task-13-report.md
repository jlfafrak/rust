# Task 13 Report — EOS `std::process` Completion and Validation

## Status

Task 13 completes the EOS process deliverables pulled forward by Task 12 and validates the
Rust/native boundary without inventing POSIX process services. The required process-smoke crate
now type-checks against fresh EOS `std` metadata. The exact no-bypass EOS `std` check and the
ordinary non-EOS host `std` check pass with warnings denied. The exact EOS `std` artifact build
compiles all target sources and stops only because `eos-rust-link` is not installed; that wrapper
remains Task 15 work.

The Task 12 backend already had the correct EOS route, counted spawn request, stable handle,
wait/try-wait/kill/close calls, status caching, and Unsupported process replacement. The Task 13
gap audit found two real remaining source defects in addition to the missing smoke/validation:

- `ExitStatus` retained only the status kind and code, silently discarding the six reserved words
  in the fixed 32-byte ABI record.
- EOS inherited the Unix `ExitStatusExt` raw-status adapter even though its infallible `i32`
  interface cannot losslessly represent the EOS kind, full signed code, and reserved words. The
  adapter truncated normal exit codes to eight bits and mapped termination to an invented bare
  integer which `from_raw` then reinterpreted as a normal exit.

## TDD RED and GREEN

The independent smoke RED was run before its files existed:

```text
cargo metadata --no-deps --format-version 1 \
  --manifest-path tests/eos/apps/process-smoke/Cargo.toml
error: manifest path .../process-smoke/Cargo.toml does not exist
exit 101
```

The new smoke configures piped stdout, explicit owned arguments/environment/cwd, spawns, polls,
kills on demand, waits repeatedly, polls after wait, repeats kill after cached completion, and
decodes success/code. Its helper returns only `Child`; the complete `Command` and the owned Rust
program/argv/env/cwd inputs leave scope immediately after synchronous spawn. This is explicit
Rust-level lifetime evidence that no Rust request owner is retained by the child. The native Task
10 contract remains responsible for copying those borrowed bytes and pinning child descriptors
before `eos_rust_spawn` returns; no native ABI change was made.

For the fixed status record, a compile invariant was added before the storage correction. The
exact EOS `std` check then failed with E0080 because the old Rust status was 8 bytes and the native
record is 32 bytes. `ExitStatus` now directly contains `libc::eos_rust_process_status`. Equality
compares kind, code, and every reserved word; Debug prints all of them; the size invariant remains
as regression coverage. A normal exit exposes its full signed code, while termination has no exit
code and is never reported as a Unix signal.

A focused cfg audit was added before changing the raw adapter and failed because neither Unix
implementation excluded EOS. GREEN adds only `#[cfg(not(target_os = "eos"))]` to the
`ExitStatus` and `ExitStatusError` implementations, retaining the trait and both implementations
on ordinary Unix targets. A direct EOS compile probe now fails with E0599 on
`ExitStatus::from_raw`; an ordinary host probe compiles. This deliberate EOS API limitation is
documented on the trait: the API has no `Result`/Unsupported path and its `i32` cannot represent
the fixed EOS record honestly.

The first GREEN std attempt exposed one denied dead-code warning: the removed raw adapter was the
only consumer of the internal EOS `signal`, `core_dumped`, `stopped_signal`, and `continued`
helpers. Those methods were removed rather than warning-suppressed. The public portable
`ExitStatus::{success,code}` behavior remains exact; EOS exposes no signal-shaped status API.

## Ownership, process state, and status audit

- `Command::spawn` validates embedded NULs and rejects uid/gid/groups/process-group/chroot/setsid/
  pre-exec attributes as Unsupported before native spawn.
- `setup_io` creates parent and child pipe endpoints. The counted request borrows the owned
  program, `CStringArray` argv/env, optional cwd, and child descriptors only for the synchronous
  native call. On failure normal Rust control flow drops both sides. On success the code now
  explicitly drops child-side descriptors only after the native contract has copied strings and
  pinned descriptor objects; returned parent pipe endpoints remain owned by `Child`.
- `argc` and `envc` use checked `u32` conversions. Program, argv, envp, and cwd use the reviewed
  fixed pointers/counts; flags and all seven request reserved words are zero.
- A successful spawn returns the stable nonzero Task 10 process identity. `Process::drop` closes
  it exactly once; irreversible close failure follows the established EOS fail-fast policy.
- `wait` and `try_wait` cache the complete fixed record, making Rust-level repeats independent of
  handle reuse. Native wait/try-wait remain repeatable as verified by Task 10. Kill after cached
  completion is an idempotent success; live kill routes only through `eos_rust_process_kill`.
- A native loader failure returns `-1` from `eos_rust_spawn`, so Rust returns
  `io::Error::last_os_error()` directly from `spawn`. Later child/run failure remains a wait
  status, preserving loader failure versus child failure.
- Only `EOS_RUST_PROCESS_EXITED` and `EOS_RUST_PROCESS_TERMINATED` are accepted. EXITED code zero
  is success; other EXITED codes are failures with `Some(code)`. TERMINATED is a failure with
  `None` from `code()` and no invented Unix signal or raw wait value. Reserved result words are
  retained without rejection or erasure.
- `CommandExt::exec` returns Unsupported without changing the process image. No EOS libc
  fork/vfork/exec/waitpid/signal API was added.

## Verification

All final-state source checks below were run after the status/raw-adapter correction.

- `./x check library/std --target armv7a-unknown-eos-eabi`: final post-format exit 0, no
  warnings, 2:18.
- `./x check library/std --target x86_64-unknown-linux-gnu`: final post-format exit 0, 1:50. This proves the two
  ordinary Unix `ExitStatusExt` implementations remain available and compile.
- Fresh process-smoke metadata compile with stage1 rustc, target
  `armv7a-unknown-eos-eabi`, dependency path
  `build/x86_64-unknown-linux-gnu/stage1-std/armv7a-unknown-eos-eabi/dist/deps`,
  `libstd-bc0ddd2c2a05fcf2.rmeta`, and matching
  `libpanic_unwind-43bdd812624db0b9.rmeta`: exit 0; output
  `/tmp/eos-task13-process-smoke-final.rmeta`.
- Direct negative EOS raw-status metadata probe: expected E0599 because
  `ExitStatus::from_raw` is unavailable. An ordinary host raw-status probe: exit 0.
- `python3 -m unittest tests.eos.host.test_pal_cfg_scope -v`: 9/9 passed, including the new raw
  adapter cfg/preservation test and all existing EOS/non-EOS PAL guards.
- Strict Task 10 O3 native targeted CTest in `build/eos-abi-task10-fix1-o3`: process,
  process_redirection, MARTOS process contract, and fully instrumented process TSan all passed,
  4/4 with no sanitizer report.
- `./x build library/core library/compiler-builtins --target armv7a-unknown-eos-eabi`: exit 0,
  2:06.
- `python3 tests/eos/abi/compare_layouts.py /tmp/eos-task13-layout`: exit 0, matched 111 ARM
  C/Rust layout facts. Its first attempt honestly stopped before compilation because the failed
  std link had left no installed target core; the exact core build above supplied that artifact.
- The stable manifest remains exactly 120 exports and contains all five process services. Static
  scans find no fork/vfork/exec/waitpid/kill libc call in the EOS backend or EOS libc module.
- Scoped `git diff --check` passes. Final staged modes and diff are recorded at commit time.

## Exact Task 15 endpoints

The exact required artifact build:

```text
./x build library/std --target armv7a-unknown-eos-eabi
error: linker `eos-rust-link` not found
note: No such file or directory (os error 2)
exit 1 after compiling the target std sources (3:28)
```

The literal process-smoke Cargo command reports E0463 because the failed std artifact link cannot
install EOS `std` into the stage1 sysroot. Direct fresh metadata proves the smoke source/API. An
additional diagnostic Cargo attempt with check-only `.rmeta` reached code generation and correctly
could not continue because that metadata has no optimized MIR; it is not represented as a link
result. Task 15 must provide `eos-rust-link`, let bootstrap finish/install the EOS sysroot, and
then rerun the literal Cargo link against the SDK's prebuilt `libeos_rust_abi`.

No linker wrapper, temporary fake linker, host linker substitution, weakened dynamic linking, or
Task 14+ code was added.

## Files, commit, and remaining concerns

Modified:

- `library/std/src/sys/process/unix/eos.rs`
- `library/std/src/os/unix/process.rs`
- `tests/eos/host/test_pal_cfg_scope.py`

Created:

- `tests/eos/apps/process-smoke/.gitignore`
- `tests/eos/apps/process-smoke/Cargo.toml`
- `tests/eos/apps/process-smoke/src/main.rs`
- this report

These files are committed with the required subject `library: add EOS process backend`; the
handoff supplies the commit hash. The progress ledger is intentionally unchanged.

Remaining concerns are explicit: actual process execution still needs the release board matrix;
MARTOS-SMP 14.0.39 continues to reject explicit per-child environment/cwd, explicit stderr
redirection, and arbitrary descriptor inheritance with ENOTSUP; and EOS does not implement the
Unix raw wait-status extension because its fixed record cannot fit that interface. Task 15 is the
only build/link dependency for installing std and linking the process smoke with the SDK shim.
