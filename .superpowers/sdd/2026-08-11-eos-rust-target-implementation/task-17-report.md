# Task 17 Report — ABI, Static-ELF, and CI Release Gates

## Status and boundary

Task 17 adds representative EOS standard-library applications, a bidirectional C/Rust softfp
fixture, static ABI and final-ELF release gates, a single comprehensive CI entrypoint, and a
workflow that invokes only that entrypoint and retains the reviewed SDK and static evidence.

The outer repository started at exact commit
`951721dedc2fad6366e10f7393aaddef21e14646` on `codex/eos-rust-target`. The nested backtrace
gitlink and checked submodule remain exactly
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`. The implementation changes exactly the fifteen
Task 17 paths named in the brief: five `Cargo.toml`/`src/main.rs` application pairs, two softfp
fixture sources, the Python static gate, executable CI entrypoint, and EOS workflow. It does not
change a Task 18 file, controller progress ledger, target definition, wrapper, validator,
authentication packager, relocation policy, ABI allowlist, PAL, unwind, or capability behavior.

## Representative application coverage

Each application is an independent edition-2024 Cargo workspace using the built-in
`armv7a-unknown-eos-eabi` target and the established `restricted_std` crate attribute:

- `filesystem` exercises directory creation/removal, create/open, write/read, seek, metadata,
  directory enumeration, rename, and file removal.
- `threads-tls` exercises `thread_local!`, `OnceLock`, `Arc`, `Barrier`, `Mutex`, `Condvar`, thread
  spawn/join, worker TLS isolation, and synchronization.
- `network` exercises TCP listener/stream setup, DNS resolution, timeout/nonblocking controls,
  stream I/O, UDP bind/connect/send/receive, and socket address inspection.
- `process` exercises command arguments, environment, working directory, piped standard I/O,
  spawn, `try_wait`, kill, and wait.
- `ffi-abi` exercises a C-layout small enum and mixed `i32`/`f32`/`f64` aggregate through an
  `extern "C"` function pointer, argument passing, and aggregate return.

The gate copies every application to an isolated temporary workspace and supplies isolated
`HOME` and `CARGO_HOME` state. It builds both debug and release with the named stable Cargo and
precompiled target sysroot from the immutable reviewed Task 16 SDK. No application build writes
Cargo state into its repository fixture.

## Bidirectional softfp ABI gate

`tests/eos/abi/softfp.c` and `softfp.rs` both pass and return an integer, small C-layout enum,
`f32`, `f64`, and mixed aggregate. C calls both Rust exports and Rust calls both C exports. The C
fixture also uses an explicit `vaddq_f32` NEON operation, while both sides perform scalar VFP
arithmetic.

The C soft object uses the exact approved flags:

```text
-march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3
-mfloat-abi=softfp -fPIC -ffunction-sections -fdata-sections -O2
```

The Rust object uses the built-in `armv7a-unknown-eos-eabi` target, the reviewed SDK sysroot,
PIC, optimization, and aborting object-only emission. `readelf -hA` proves both objects are EABI5
and neither declares `Tag_ABI_VFP_args`; both declare VFPv3 and NEONv1. `objdump -dr` proves C
contains scalar `vadd.f32`, scalar `vadd.f64`, vector `vadd.f32 q...`, and relocations to both Rust
functions. Rust contains scalar `vadd.f32`/`vadd.f64` and relocations to both C functions. The
soft objects merge successfully with `arm-none-eabi-gcc -r`, covering compatible C-layout
aggregate call and return sequences in both directions.

The independent negative control compiles the same C source with `-mfloat-abi=hard`. It declares
`Tag_ABI_VFP_args: VFP registers`, the common attribute checker rejects it as hard-float, and a
relocatable merge with the Rust object exits 1 with:

```text
uses VFP register arguments, .../softfp-rust.o does not
```

The reviewed SDK's packaged compiler driver identifies as 14.3.1 but its staged subset does not
contain `cc1`, and it does not package `objdump`. The gate therefore fails closed unless
`EOS_ARM_GNU_CC` points to the complete reviewed ARM GNU 14.3.1 compiler and
`EOS_ARM_GNU_OBJDUMP` points to reviewed binutils 2.44; both versions and the compiler closure are
checked before use. The SDK remains read-only and was not rebuilt or modified.

## Final ELF, authentication, and symbolization gates

All ten application/profile combinations are linked with an explicit SHA-1 GNU build ID. For
each base ELF, the gate:

1. runs the packaged `eos-elf-validate`;
2. independently requires `.symtab` and a 40-hex GNU build ID;
3. retains the unstripped `.elf` and matching `.build-id` file;
4. runs the packaged `eos-auth-package`;
5. validates the authenticated ELF with `--allow-auth-trailer`; and
6. independently reconstructs and byte-compares the exact 129-byte marker, SHA-256 hex digest,
   and newline trailer against the base ELF.

The final artifacts contain ten base/authenticated/build-ID triplets. The build IDs are:

```text
ffi-abi-debug       ec31812dc78b97d4da5269bfefd02874b5c93eac
ffi-abi-release     0cabcd77fb281e0951d9bb1aace6f060a058b515
filesystem-debug    37b51157ef49551c72c476537e4a8a45866bccc9
filesystem-release  9ac825e8c390aaeb3d4f2df723957f9dad3d4978
network-debug       bd7c03d82373920ebb504c7b815e836e146ced40
network-release     6f6aebd60153a518becece25607a7f06efa5aecc
process-debug       5f8853e61c9e9b08ba3c44ece1d206c79832bd6c
process-release     b24906c805bdde354ad3b05aa60d1c1048aa831e
threads-tls-debug   30c9831f99329d8bca9e17695797b672544ae515
threads-tls-release 759d011a58c698ea81112b075c06d95ce682a926
```

Independent `readelf -hW` inspection of all five release ELFs reports `ET_DYN` PIE and flags
`0x5000200, Version5 EABI, soft-float ABI`. The retained static evidence also includes the C,
Rust, and merged softfp objects, both disassemblies, both attribute dumps, and the existing
111-fact C/Rust layout comparison outputs.

## CI entrypoint and workflow

`tests/eos/run-ci.sh` is executable and fails closed before doing work unless the reviewed SDK,
complete ARM compiler, and ARM objdump are explicit and usable. It records SDK manifests,
provenance, and tool versions, creates an isolated bootstrap configuration, and runs:

- the EOS release-lock test;
- stage-1 Rust target tests;
- stage-1 host `library/test` EOS status tests;
- the native EOS ABI CMake build and all CTests;
- custom libc link and source-provenance audits;
- bootstrap target, Unix PAL cfg-scope, and FFI/unwind policy scans;
- all SDK builder/installer/source contract tests;
- the stage-1 EOS target sysroot build;
- the 111-fact C/Rust ABI layout comparison; and
- application cross-links, softfp comparison, hard-float negative control, ELF validation,
  authentication, build-ID retention, and CI/workflow policy tests.

The workflow has read-only repository permissions, a self-hosted EOS Rust runner, one and only
one `run:` command (`tests/eos/run-ci.sh`), and two artifact uploads: the reviewed SDK and the
static-test/evidence root. Static policy tests require exactly that shape and reject credentials,
board/deployment/transfer terms, and literal network addresses. There are no hardware loading,
credentials, board addresses, transfers, or deployment actions in Task 17.

## Independent RED / GREEN evidence

Strict focused cycles used real scripts and artifacts rather than implementation substring
checks, except for the workflow's required static policy:

- Running the application gate before adding fixtures produced ten missing-application subtest
  failures. After adding the five isolated apps, all ten real debug/release cross-links reached
  the ELF gate.
- Those ten builds then independently failed with `ELF has no GNU build ID`. Adding only the
  controlled `-Wl,--build-id=sha1` link argument made build-ID extraction and retention GREEN.
- The softfp test first failed on the missing C/Rust fixture. The first real C build then failed
  because the packaged subset could not execute `cc1`; requiring the complete reviewed compiler
  made compilation GREEN without changing the SDK.
- Attribute checking initially expected a positive soft flag on relocatable objects. Real GNU
  objects correctly showed EABI5 base PCS with no hard-float declaration; the final ELF carries
  the positive `0x200` soft-float flag. The checker now requires EABI5 and rejects either the
  hard-float flag or `Tag_ABI_VFP_args` on objects.
- The first call-site check missed the valid optimized `R_ARM_JUMP24` tail call. Accepting the ARM
  call/tail-call relocation family made the real bidirectional call gate GREEN.
- The first NEON check failed because scalar source arithmetic was scalarized. The explicit
  `vaddq_f32` fixture produced the required vector instruction and the softfp gate passed.
- Both CI policy tests were RED while `run-ci.sh` and the workflow were absent. The executable
  fail-closed entrypoint and one-command/two-upload workflow made them GREEN.
- The first integrated `library/test` invocation used the EOS bootstrap config, produced an ARM
  test executable, and failed on the host with `Exec format error`. Running this host test without
  the cross config made its two EOS classification tests GREEN while the separate sysroot build
  retained the cross-target configuration.
- The first full entrypoint exposed that the SDK-first `PATH` made source host audits select a
  compiler without host `std`. Pinning the already-built source stage-1 rustc for those audits
  made libc 6/6 and policy 30/30 GREEN.
- The next full entrypoint reached SDK test 76 and failed only because the real exported
  `EOS_RUST_SDK_ROOT` overrode the packaged-script-relative default under test. Unsetting that
  variable only around the isolated SDK unit suite made the formerly failing test and all 76 SDK
  tests GREEN; the outer release gate continues to require and export the reviewed root.

The final focused command was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-focused-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-focused-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves
Ran 4 tests in 49.706s ... OK
```

## Final verification

The exact required full entrypoint was run once on the final implementation:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-final2-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-final2-pycache tests/eos/run-ci.sh
```

It exited 0 and ended with `EOS CI gates passed`. Its layer results were:

```text
release lock                       1/1 passed
compiler/rustc_target EOS tests    2/2 passed (326 filtered)
library/test EOS tests             2/2 passed (58 filtered)
native EOS ABI CTest              45/45 passed
libc link/provenance audits         6/6 passed
bootstrap/PAL/unwind policy        30/30 passed
SDK contract suite                 76/76 passed in 300.179s
EOS target sysroot build           passed
C/Rust layout comparison           matched 111 ARM layout facts
static ELF/ABI/CI suite             4/4 passed in 54.142s
```

The CMake runtime used for native evidence is the reviewed Kitware 3.31.10 runtime supplied for
this task; both `cmake` and `ctest` report 3.31.10. Additional final syntax checks passed for
`bash -n tests/eos/run-ci.sh` and Python 3.14 `py_compile` with warnings promoted to errors. The
commit is `9446f771dae4a7511801ab2287ff457fe3ffa814` with required subject
`ci: gate EOS ABI sysroot and ELF artifacts`.

## Self-review and remaining concerns

The final review checks exact file scope, executable index mode for `run-ci.sh`, workflow command
count and uploads, no Task 18 vocabulary/actions, no application build output under the new
fixtures, no progress-ledger delta, `git diff --check`, exact base ancestry, clean untracked state,
and the unchanged backtrace gitlink/checkout.

- Task 17 does not load hardware. Board loading, credentials, transfer, addresses, deployment,
  and on-target runtime execution remain manual Task 18/release gates.
- CI requires the complete reviewed ARM GNU compiler and objdump paths because the immutable
  packaged Task 16 SDK intentionally lacks a C compiler closure and packaged objdump. The gate
  checks exact versions and fails closed rather than substituting arbitrary tools.
- Bootstrap emits its existing missing-`change-id` notice and an absent optional `llvm-strip`
  notice; neither affects the successful target sysroot or release gates.

## Fix Round 1 — superseding ABI and release-identity evidence

This section supersedes the original report's claims about softfp sequence comparison, attribute
strictness, reviewed-tool identity, focused test count, final static-suite count, and final
verification command. All unaffected Task 17 implementation and evidence above remains valid.
Fix Round 1 responds to the independent NOT APPROVED review without changing the Task 17
application fixtures, workflow, Task 15 behavior, Task 18 scope, controller ledger, or backtrace.

### Review findings reproduced

Each review finding received a real negative or mutation control before its production fix:

- Reordering the C `eos_mixed` fields to `f32`, `i32`, `f64` still compiled, retained all symbol
  relocations and VFP/NEON checks, and merged with the Rust object through `gcc -r`. The new test
  expected an `aggregate ABI` rejection but RED reported `AssertionError not raised` after
  0.955s. This confirmed that the old gate compared only file attributes and call existence.
- Real ARMv6/VFP and ARMv7-A/VFPv3-without-NEON objects were both accepted by the old attribute
  helper. The two subtests were RED with `AssertionError not raised`. Independently removing all
  scalar `f32` arithmetic and all scalar `f64` arithmetic from both real compiled fixtures also
  remained green under the old combined FP regex; both new controls were RED. The combined
  command ran three tests with four expected failures in 1.861s.
- Replacing the controlled `-mtune=cortex-a9` tuple with `-mtune=cortex-a8` still passed the old
  build because its C compile command duplicated hard-coded flags and did not validate GCC's
  selected target options. The new control was RED with `AssertionError not raised` in 2.119s.
- A same-name executable fake SDK satisfied the old `sdk_root()` shape check; the identity test
  was RED because no rejection occurred. The first full-root ARM mutation-control setup exposed a
  cross-device hard-link test-fixture error before reaching production behavior. Moving only that
  temporary clone beside the reviewed ARM root fixed the test setup; changing an unrelated
  `arm-none-eabi-strings` byte then produced the valid RED: the old `arm_gcc()` accepted the
  mutated same-version installation and `AssertionError not raised` in 0.053s.
- A hard-linked clone of the real SDK with only `LICENSE-MIT` replaced was passed to the real CI
  entrypoint under a deliberately incomplete command path. The old entrypoint reached its later
  command check and failed with `required EOS CI command is unavailable: cmake`; the new test was
  RED because the required earlier `reviewed Task 16 SDK tree identity mismatch` was absent. This
  proves the regression test executes the entrypoint rather than inspecting shell source.

### Cryptographic Task 16 identity binding

The gate now implements the exact Task 16 builder's `sha256-tree-v1` algorithm over relative
directories, regular-file contents, and symlink targets. Before executing any SDK or ARM GNU
tool, `run-ci.sh` invokes the Python identity gate, which requires:

```text
reviewed SDK tree  309cd5d682c09dfd65e1ff0f2c0ee5d87e390543bcdfd09af52b49f8327c5060
full ARM GNU tree  a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d
EOS input identity 5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910
algorithm          sha256-tree-v1
```

The SDK fingerprint is the independently recomputed complete 116-file/757-MiB reviewed Task 16
package identity. The full external 1.8-GiB ARM GNU identity exactly equals the
`arm_gnu_installed` digest recorded by the SDK's Task 16 release manifest. The gate also requires
the exact release version/toolchain/target/layout, exact EOS input digest, and exact Rust fork,
upstream Rust, libc, and backtrace revisions. GCC and objdump must resolve to their canonical
names beneath that one hashed ARM root. Shape, executable bits, `cc1`, and version checks remain
as defense in depth after cryptographic verification.

Direct final evidence was:

```text
verified Task 16 SDK 309cd5d682c09dfd65e1ff0f2c0ee5d87e390543bcdfd09af52b49f8327c5060
and ARM GNU a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d
at /home/dev/code/arm-toolchain-build/custom-arm-libs
```

The fake SDK, complete SDK mutation, and unrelated full ARM-root mutation controls all reject
before selected tools run. The complete reviewed trees continue to pass. The identity-focused
GREEN command ran 3/3 tests in 2.153s.

### Actual C/Rust aggregate sequence comparison

The gate now parses the pinned `objdump -dr` bodies of C and Rust `accept` and `return` exports.
It normalizes C's entry `sp` offsets and Rust's frame-pointer offsets to the same AAPCS entry
frame, then compares each independently extracted sequence to the other language and to the
approved literal softfp sequence:

```text
accept registers: r0=i32, r1=small enum, r2=f32
accept stack:     +0=f64, +8=mixed.i32, +12=mixed.f32, +16=mixed.f64
return registers: r0=aggregate sret, r1=i32, r2=small enum, r3=f32
return stack:     +0=f64, +8=mixed.i32, +12=mixed.f32, +16=mixed.f64
sret stores:      r0+0=i32, r0+4=f32, r0+8=f64
```

Both C-to-Rust and Rust-to-C caller relocations remain required for `accept` and `return`; the
new callee comparison verifies the register/stack contract consumed in both directions rather
than treating successful relocatable linking as signature evidence. The swapped-field real C
object is now rejected, while the original object pair and hard-float rejection remain GREEN.
The focused original/mutation comparison passed 2/2 in 2.253s. The normalized sequence is
retained as `static-tests/softfp-abi-sequence.json` alongside both objects and disassemblies.

### Exact attributes, instructions, and tuning

Both object attribute streams must now contain exact ARMv7, Application profile, ARM ISA,
Thumb-2, VFPv3, and NEONv1 tags, with no hard-float flag or `Tag_ABI_VFP_args`. The controlled C
object must identify CPU `7-A`; Rust must identify `cortex-a9`. GCC's real
`-Q --help=target` resolution must report `armv7-a+simd`, `cortex-a9`, `neon-vfpv3`, and
`softfp`; this output is retained as `static-tests/softfp-c.target-options.txt`. The compile and
relocatable-link commands now share the one checked flag tuple, eliminating the prior duplicated
compile flags.

The disassembly gate separately requires scalar `f32` operations using S registers and scalar
`f64` operations using D registers from both languages. It still independently requires C's
explicit NEON Q-register vector add. ARMv6, missing-NEON, cortex-a8, missing-scalar-f32, and
missing-scalar-f64 controls all reject. The four focused attribute/instruction/tuning tests passed
4/4 in 2.217s.

Final retained attributes show, for both C and Rust:

```text
Tag_CPU_arch: v7
Tag_CPU_arch_profile: Application
Tag_ARM_ISA_use: Yes
Tag_THUMB_ISA_use: Thumb-2
Tag_FP_arch: VFPv3
Tag_Advanced_SIMD_arch: NEONv1
```

Neither contains `Tag_ABI_VFP_args`; the separate hard-float control still contains
`Tag_ABI_VFP_args: VFP registers` and fails to merge with the Rust softfp object.

### Fix Round 1 focused and full verification

After the independent RED/GREEN cycles, the complete expanded module ran:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-fix1-focused3-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix1-focused3-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves
Ran 12 tests in 51.672s ... OK
```

The exact required full entrypoint was then rerun on the final Fix Round 1 implementation:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-fix1-final-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix1-final-pycache tests/eos/run-ci.sh
```

It exited 0, printed both verified tree identities before the tool tests, and ended with
`EOS CI gates passed`. Exact layer results were:

```text
release identity                   SDK + full ARM GNU sha256-tree-v1 passed
release lock                       1/1 passed
compiler/rustc_target EOS tests    2/2 passed (326 filtered)
library/test EOS tests             2/2 passed (58 filtered)
native EOS ABI CTest              45/45 passed
libc link/provenance audits         6/6 passed in 15.269s
bootstrap/PAL/unwind policy        30/30 passed in 23.672s
SDK contract suite                 76/76 passed in 272.842s
EOS target sysroot build           passed
C/Rust layout comparison           matched 111 ARM layout facts
static ELF/ABI/identity/CI suite   12/12 passed in 52.765s
```

Final syntax checks used Python 3.14 `-Wall -Werror -m py_compile` with an external bytecode
cache and `bash -n`. The final Fix Round 1 delta is confined to
`tests/eos/host/test_static_elves.py` (mode `100644`) and `tests/eos/run-ci.sh` (mode `100755`).
The workflow remains the already-reviewed one-command/two-upload definition. The separate fix
commit is `42a521909326724a9fb9294ade1133ad8d753189` with subject
`ci: harden EOS ABI and release identity gates`.

### Fix Round 1 self-review and concerns

The final audit rechecks the two-path delta, modes, no Task 18/controller-ledger change, empty
untracked status, no generated application targets, cleanup of all same-filesystem hard-link
mutation controls, `git diff --check`, exact parent chain, and unchanged backtrace gitlink and
checkout.

- The complete SDK fingerprint intentionally binds CI to this exact reviewed Task 16 output;
  replacing it with a newly rebuilt package, even from nominally identical versions, requires an
  explicit review and fingerprint update rather than silently generating release evidence.
- Full-tree hashing reads about 2.6 GiB at CI preflight. On the reviewed host it completes in
  roughly two seconds and is cached within each Python process; this is the cost of detecting
  unrelated same-version mutations anywhere in either supplied tree.
- Hardware execution remains manual Task 18 scope. No credentials, board addresses, transfers,
  deployment, or hardware loading were added.

## Fix Round 2 — trusted runner Python before release preflight

This section supersedes Fix Round 1's entrypoint-ordering conclusion. The cryptographic release
identity policy and all ABI/ELF gates remain unchanged; Fix Round 2 closes the remaining path
ordering bypass identified by re-review.

### Reproduction and root cause

Two independent mutation controls run the real `tests/eos/run-ci.sh` with a deliberately minimal
initial `PATH` containing known runner `bash`, `dirname`, `readlink`, and `python3` links. Each
control hard-links a same-shape SDK copy, safely unlinks and mutates only the copy's Rust license,
and installs a harmless executable `python3` shim that writes a temporary marker and exits 0.
One shim is placed in the substituted SDK's `bin`; the other is placed in the configured CMake
bin. Before the fix, `run-ci.sh` prepended those directories and then resolved unqualified
`python3`, so both markers were written before the reviewed tree mismatch could be reported.

The exact RED command was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix2-red-pycache \
python3 -m unittest -v \
  tests.eos.host.test_static_elves.CiEntryPointPolicyTests.test_ci_entrypoint_rejects_mutated_sdk_before_running_tools \
  tests.eos.host.test_static_elves.CiEntryPointPolicyTests.test_ci_entrypoint_rejects_mutation_before_cmake_python_can_run
```

It failed 2/2 in 0.052s with the intended independent assertions:

```text
AssertionError: True is not false : SDK python3 ran before identity verification
AssertionError: True is not false : CMake python3 ran before identity verification
```

### Minimal fix and GREEN evidence

The entrypoint now resolves `python3` from the runner's initial environment with the Bash
`command` builtin, canonicalizes it with the initial environment's `readlink`, requires the
resolved path to be an executable regular file, and executes it to require Python 3.11+ plus
`tomllib`. On this runner the captured executable is `/usr/bin/python3.14`. Release identity is
then verified with that absolute interpreter before either the configured CMake directory or
the SDK `bin` is added to `PATH`. Every later Python invocation also uses the same captured
absolute executable.

The preflight audit found only the absolute shebang `/usr/bin/env` resolving runner `bash`, Bash
builtins, the initial environment's `dirname` and `readlink`, and the captured absolute Python
before identity verification. No configured SDK/CMake command is resolvable through a modified
`PATH` before the identity gate succeeds.

Rerunning the exact two-control command with
`PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix2-green-pycache` passed 2/2 in 1.214s. Both marker
assertions remained absent and both processes rejected first with
`reviewed Task 16 SDK tree identity mismatch`. `bash -n tests/eos/run-ci.sh` and Python 3.14
`-Wall -Werror -m py_compile tests/eos/host/test_static_elves.py` also passed.

The expanded focused gate then passed:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-fix2-focused-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix2-focused-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves
Ran 13 tests in 54.061s ... OK
```

### Final full CI verification

The exact final entrypoint command was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task17-fix2-final-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-task17-fix2-final-pycache tests/eos/run-ci.sh
```

It exited 0, verified SDK identity
`309cd5d682c09dfd65e1ff0f2c0ee5d87e390543bcdfd09af52b49f8327c5060` and full ARM GNU
identity `a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d` before the tool gates,
and ended with `EOS CI gates passed`. Exact layer results were:

```text
release lock                        1/1 passed
compiler/rustc_target EOS tests     2/2 passed (326 filtered)
library/test EOS tests              2/2 passed (58 filtered)
native EOS ABI CTest               45/45 passed
libc link/provenance audits          6/6 passed in 13.667s
bootstrap/PAL/unwind policy         30/30 passed in 24.078s
SDK contract suite                  76/76 passed in 246.004s
EOS target sysroot build            passed
C/Rust layout comparison            matched 111 ARM layout facts
static ELF/ABI/identity/CI suite    13/13 passed in 56.644s
```

### Fix Round 2 scope and self-review

The production/test delta is confined to `tests/eos/run-ci.sh` (mode `100755`) and
`tests/eos/host/test_static_elves.py` (mode `100644`). The report is durable SDD evidence and is
not part of the tracked product commit. No workflow, SDK, ABI, target, wrapper, installer,
Task 18, hardware, deployment, credential, or progress-ledger behavior changed. The separate
Fix Round 2 commit is `65ec6ffcb028e029489a54b803e7ce9d563e7e87` with subject
`ci: trust runner Python before EOS preflight`.

Final self-review verifies the two-path tracked scope, exact file modes, `git diff --check`, a
clean worktree including untracked state after commit, parent chain through Fix Round 1 and the
original Task 17 commit, and unchanged backtrace gitlink/checkout
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`. No concerns remain; the full-tree hashes retain the
already documented preflight I/O cost, and hardware execution remains manual Task 18 scope.
