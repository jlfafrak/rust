# Task 2 Report: Honest, Release-Complete EOS Board Applications

## Outcome

Task 2 makes every application named by `board-test-manifest.toml` produce debug and release
base ELF, authenticated ELF, and 40-hex GNU build-ID evidence. It also replaces the three
misleading runtime probes with self-contained behavior and produces the FFI containment artifact
from the reviewed Rust static library plus the real C caller.

The target remains exactly `armv7a-unknown-eos-eabi`; the C flags remain exactly
`-march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp`. The existing
`deny(ffi_unwind_calls)`, linker policy, validator/authentication behavior, capability inventory,
and manual hardware boundary are unchanged. No SDK build was launched.

## Independently observed RED controls

The following focused command ran before production edits:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-red-pycache \
python3 -m unittest -v \
  tests.eos.host.test_static_elves.ApplicationEvidencePolicyTests \
  tests.eos.board.test_check_results.BoardResultGateTests.test_board_policy_requires_ffi_containment_application
```

It ran five tests in 0.143s and failed 5/5 for the intended reasons:

- process had no `current_exe()`, named `/eos/rust-process-child`, and used `.env()` plus
  `.current_dir()` child overrides;
- network contained `to_socket_addrs()?` before either real numeric TCP or UDP traffic;
- unwind contained no `Backtrace::force_capture()`;
- static application coverage omitted `hello-std` and `unwind` relative to board policy; and
- board policy omitted `ffi-containment`.

This was a behavior/source-policy RED, not an SDK or infrastructure failure. After the minimal
changes, the same five tests ran in 0.081s and passed 5/5.

## Application behavior

`process` uses `std::env::current_exe()` and a `--child` mode. The child reads exactly `EOS` from
stdin, echoes it to stdout, and returns. The parent records its own argument count, environment
count, and current directory; spawns itself without per-child env/cwd overrides; verifies the
piped echo; exercises `try_wait`; conditionally kills a still-running child; and verifies that a
repeated `wait` returns the same status. Its output uses stable, labeled evidence lines.

`network` binds a numeric IPv4 loopback TCP listener, accepts on a local server thread, and
verifies an `EOS`/`TCP` fixed-byte exchange. Two numeric loopback UDP sockets then verify an
`EOS`/`UDP` datagram exchange. Only after both transports succeed does a separate
`("localhost", 7).to_socket_addrs()` probe require the stable EOS `Unsupported` error.

`unwind` retains `#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]` and the existing
TLS-cleanup abort mode. Before the catch/drop probe, it calls `Backtrace::force_capture()`,
requires `BacktraceStatus::Captured`, requires nonempty formatted content, and prints that
content as board evidence.

## Real C-to-Rust containment ELF

`build_ffi_containment_application(...) -> tuple[Path, Path, str]` copies and builds the reviewed
`ffi-containment` staticlib with the reviewed SDK. It separately compiles
`tests/eos/abi/ffi_caller.c` using the reviewed complete GCC 14.3.1 closure and the exact pinned
softfp target flags, then final-links the C object and Rust archive through `eos-rust-link` with
`-Wl,--build-id=sha1`.

The final ELF goes through the same shared retention helper as Cargo executables: packaged
validator, `.symtab` requirement, GNU build-ID extraction, base retention, authentication,
authenticated validation, exact byte-for-byte trailer reconstruction, and retained build-ID
recheck. Independent `readelf` inspection of `ffi-containment-release.elf` reports ELF32 ARM,
`ET_DYN`, flags `0x5000200, Version5 EABI, soft-float ABI`, and global symbols for both C `main`
and Rust `eos_ffi_containment_probe`.

## Complete retained evidence

The board/static application set is exactly:

```text
hello-std filesystem threads-tls network process ffi-abi unwind ffi-containment
```

Each application has debug and release `.elf`, `.auth.elf`, and `.build-id` files under
`/tmp/eos-final-task2-artifacts/static-tests`. The retained IDs are:

```text
hello-std-debug          3f4199acec58e905b93c72024418f4b9d13c76d3
hello-std-release        004a76f7b8aeb8f91bdfa1b3aec4bc75b1f2bf01
filesystem-debug         b07845c55a1370f785ede3b333fd0c4cbce678f2
filesystem-release       9ac825e8c390aaeb3d4f2df723957f9dad3d4978
threads-tls-debug        d88280ba6448b1aa809ad991a9f6423ab9bc487d
threads-tls-release      759d011a58c698ea81112b075c06d95ce682a926
network-debug            2abf8540a1e6b5f2d3ad289b1d16dcc3cade0a55
network-release          381033e10acdb7f3ee8b44e5076b0e769b72af68
process-debug            9d9d128656b0ab9971520e501b195bc4d2ceae4f
process-release          bd29887bcbda4586c4b898b83db9ee1462ec6570
ffi-abi-debug            368db8c4f772af43a693b9c93fad6941d64d858b
ffi-abi-release          0cabcd77fb281e0951d9bb1aace6f060a058b515
unwind-debug             458abd96ee8376be585d4f456dc4c0fd51de45b6
unwind-release           c6ec3d216f6680d4595adfac1713e62a9364be6f
ffi-containment-debug    dc2e3e074a0131518ea70250c59b777d6f007727
ffi-containment-release  69e0c7e56c65e88f0b75364362e6a1fdce3d235f
```

`ffi-abi` remains separate mixed-ABI evidence; it was not substituted for the real C caller.

## GREEN verification

The required final command, run after the last production and test edit, was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-task2-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves tests.eos.board.test_check_results
```

It ran 38 tests in 150.090s and passed with zero failures or skips. This includes all sixteen
debug/release final application links plus release identity, softfp negative controls, exact
board-policy enforcement, authentication, trailer, symbol-table, and build-ID checks.

Additional checks:

- `python3 -Wall -Werror -m py_compile` passed for both modified Python modules;
- `tests.eos.host.test_ffi_unwind_policy` ran 19 tests in 19.425s: OK, with three existing
  host-Rust-compiler-dependent skips; and
- path-scoped `git diff --check` passed for every authorized source/report path.

The environment has no `rustfmt` executable. All three Rust sources nevertheless compiled in
both debug and release with the reviewed SDK, and their final ELFs passed the strict gate.

## Files, scope, commit, and concerns

Product/test changes are confined to:

- `tests/eos/apps/process/src/main.rs`
- `tests/eos/apps/network/src/main.rs`
- `tests/eos/apps/unwind/src/main.rs`
- `tests/eos/host/test_static_elves.py`
- `tests/eos/board/board-test-manifest.toml`
- `tests/eos/board/test_check_results.py`

The existing `ffi-containment` Cargo manifest/library and `tests/eos/abi/ffi_caller.c` were
retained unchanged. This report and the Task 17 follow-up are evidence-only documentation. The
product commit is recorded below after creation with subject
`test: make EOS board probes release-complete`.

No builder, installer, layout, CI entrypoint/workflow, ledger, release manifest, target, linker,
validator, authentication, capability, backtrace gitlink, or Task 3+ file changed. Hardware
loading and on-target execution remain manual, so the retained static artifacts are complete but
actual XC7Z030/XC7Z045 observations still belong to the later manual board gate.

Commit: `6fa0501ea2fa488dcf1c0d024ce7de271f0d7dab`

## Evidence-only follow-up commit

The requested documentation follow-up uses subject
`docs: record final EOS application evidence` and exactly these two Git mode `100644` paths:

- `.superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-17-report.md`
- `.superpowers/sdd/2026-08-21-eos-final-release-integration/task-2-report.md`

Before staging, path-scoped whitespace checks passed for the tracked Task 17 report and the
ignored/untracked Task 2 report. The subsequent cached audit is required to confirm the same
two-path scope, `100644` modes, and clean diff before the docs-only commit is created.

## Fix Round 1 — hermetic compiler closure and mutation-complete evidence

### Review findings and root cause

Final review reported 0 Critical, 2 Important, and 0 Minor findings. Both Important findings were
reproduced against the real gate before production edits.

The C caller compile called `run()` without `env`, so Python inherited the runner environment.
`arm_gcc()` likewise queried `--version` and `-print-prog-name=cc1` in the ambient environment
and only checked that the reported cc1 existed. Consequently GCC search variables could select
an unreviewed frontend or header even though the compiler tree itself passed its cryptographic
identity check.

The application-realism controls also checked broad source tokens/order rather than the complete
evidence operations. The common retention helper required `.symtab` but did not require the C
entry point and Rust containment export in the final symbol table, so a C-only final link could
leave the Rust archive unextracted and still be retained/authenticated.

### Independent RED controls

The exact focused RED command was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-fix1-red-pycache \
python3 -m unittest -v \
  tests.eos.host.test_static_elves.ApplicationEvidencePolicyTests.test_application_gate_rejects_removed_udp_verification \
  tests.eos.host.test_static_elves.ApplicationEvidencePolicyTests.test_application_gate_rejects_incomplete_backtrace_evidence \
  tests.eos.host.test_static_elves.StaticElfGateTests.test_ffi_caller_ignores_hostile_compiler_environment \
  tests.eos.host.test_static_elves.StaticElfGateTests.test_ffi_gate_rejects_final_elf_without_extracted_rust_symbol
```

It ran four tests in 15.256s and failed 4/4 for the intended reasons:

- removing the UDP receive comparison encountered no application evidence verifier;
- replacing backtrace formatting, nonempty-content enforcement, or evidence printing encountered
  no application evidence verifier;
- injected `GCC_EXEC_PREFIX`, `COMPILER_PATH`, `CPATH`, `C_INCLUDE_PATH`,
  `CPLUS_INCLUDE_PATH`, and `OBJC_INCLUDE_PATH` influenced the real C build and made it fail; and
- a real final link from a C-only `main` plus the Rust archive was accepted because no assertion
  required `eos_ffi_containment_probe` to have been extracted.

### Minimal fixes and focused GREEN

The gate now creates a minimal compiler environment containing only `LANG=C`, `LC_ALL=C`, and
the platform default `PATH`. Both GCC identity/cc1 probes and every controlled C compile or
relocatable GCC link use it. The cc1 query must now return an absolute, executable regular file
that resolves beneath the cryptographically reviewed ARM GNU root. On the reviewed closure it
resolves to:

```text
/home/dev/code/arm-toolchain-build/custom-arm-libs/libexec/gcc/arm-none-eabi/14.3.1/cc1
```

The hostile control supplies an executable cc1 that would write a marker and a poisoned
`stdint.h` that would stop compilation. The real FFI build completes while the marker remains
absent, proving neither hostile frontend execution nor hostile header use.

The application evidence verifier now requires both fixed UDP send/receive directions, exact
payload comparisons, and the stable UDP evidence line. It separately requires forced backtrace
capture, formatting, captured status, nonempty content, and printing. Mutation fixtures delete
or replace each required operation and are rejected before artifact acceptance.

For `ffi-containment`, the common retention helper now parses the final ELF symbol table and
requires both global `main` and global `eos_ffi_containment_probe`. The real C-only negative
final link is rejected for the missing Rust export; the reviewed C caller extracts the archive
and retains both symbols.

The exact focused command above, with
`PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-fix1-green-pycache`, then ran 4/4 in 81.542s: `OK`.

### Complete GREEN and strict checks

The required combined command was rerun unchanged from Task 2:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-task2-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves tests.eos.board.test_check_results
```

It ran 42 tests in 350.518s and passed with zero failures and zero skips. Python 3.14
`-Wall -Werror -m py_compile tests/eos/host/test_static_elves.py` passed. Path-scoped
`git diff --check` passed for the gate and reports.

The Fix Round 1 product/evidence delta is confined to `tests/eos/host/test_static_elves.py`, this
report, and the Task 17 evidence follow-up. No application source, ABI fixture, board policy,
SDK, linker, validator, authentication, capability, CI, ledger, backtrace gitlink, hardware, or
Task 3+ file changed. The release artifact remains an ARM ELF32 `ET_DYN` EABI5 soft-float PIE;
independent final `readelf -sW` output contains both required global symbols. Hardware execution
remains the manual release boundary.

Fix Round 1 commit subject: `test: harden EOS application evidence gates`
