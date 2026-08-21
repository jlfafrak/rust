# Task 16 Report — Precompiled EOS Rust SDK and Sysroot

## Status and boundary

Task 16 adds a reproducible builder, installer, relocatable SDK layout, and ordinary-Cargo smoke
application for the precompiled `armv7a-unknown-eos-eabi` standard library. The package carries
the host `rustc`, Cargo, and rustdoc; the EOS target sysroot; the pinned ARM GNU subset and EOS
SDK inputs; the Task 15 wrapper, validator, authentication tool, linker policy, and manifests;
the native ABI archive and headers; source/license evidence; templates; and `hello-std` example.

The outer repository started at `166453dafed10c607052639cb74f70959f65f72e` on
`codex/eos-rust-target`. The nested backtrace gitlink remains exactly
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd` on `codex/task-15-eos-arm-unwind`; Task 16 does not
amend or push it. Task 15 output confinement, wrapper validation, authentication, ABI export,
softfp/PIC/PIE, and staged-root policies remain fail closed. No Task 17 file and no controller
progress ledger is changed.

## Package and install contract

`src/tools/eos-sdk/templates/config.toml` configures stage 2 for the host plus only the EOS target,
using the staged cross compiler, archiver, ranlib, and `eos-rust-link`. It sets `crt-static = false`,
enables backtrace, and uses exactly the approved standard-library features `panic-unwind`,
`backtrace`, and `backtrace-trace-only`. The Cargo template contains only:

```toml
[build]
target = "armv7a-unknown-eos-eabi"

[target.armv7a-unknown-eos-eabi]
linker = "eos-rust-link"
```

It contains no target JSON, unstable `build-std`, or FPGA/bitstream dependency.

The versioned layout manifest identifies toolchain `eos-1.97.1`, target
`armv7a-unknown-eos-eabi`, and requires:

- executable host `rustc`, Cargo, and rustdoc;
- target `core`, `alloc`, `std`, `panic_unwind`, `proc_macro`, and `test` rlibs plus shared
  `libstd`;
- the ABI archive and public headers, wrapper, validator, authentication packager, linker script,
  allowlist, lock and release manifests, source revisions, Rust licenses, templates, and example;
- resolved in-root ARM GNU and EOS SDK trees.

The installer resolves every required path beneath the SDK root, rejects missing target libraries
and symlink escapes before external execution, and invokes exactly
`rustup toolchain link eos-1.97.1 <resolved-sdk>` when rustup exists. Without rustup it prints
explicit quoted `PATH`, `RUSTC`, and `CARGO` settings rather than silently mutating another
toolchain.

## Builder, provenance, and atomicity

`build-eos-sdk` validates the release lock, Rust fork/upstream relationship, libc revision,
backtrace gitlink, input tree types and symlinks, ARM GNU 14.3.1, and EOS SDK baseline before
building. It runs the native CMake build and all 45 CTests, builds
`libeos_rust_abi.a` with `EOS_RUST_PORT=martos_14_0_39` and the pinned cross compiler, and stages
only the required ARM GNU/EOS SDK assets. The MARTOS archive is PIC and checked against the stable
native export surface.

The Rust commands are exact stage-2 bootstrap calls for the host tools and EOS target followed by
host `rustc`/Cargo dist and EOS `rust-std` dist. The complete stage2 host tools/sysroot and
stage2-tools Cargo are snapshotted into atomic staging immediately after successful `x build`,
before either dist call. This ordering is necessary because bootstrap dist may recreate stage2 and
remove tools such as rustdoc. Distribution archives are still required and SHA-256 hashed after
both exact dist commands.

The release manifest records layout/toolchain/target, SHA-256 tree hashes for the packaged ARM GNU
and EOS inputs, exact source revisions, and hashes of every produced distribution archive.
`share/source-revisions.toml` records the Rust version and fork/upstream commits, libc version and
commit, backtrace commit, ARM GNU release, and EOS SDK baseline. Rust copyright and Apache/MIT
licenses are copied into the package.

Final SDK publication is a same-parent atomic rename from a private staging directory. Incomplete
or invalid builds never publish the requested output. Every private source build, native ABI build,
and dist scratch directory is collision checked and cleaned on ordinary success or failure.

## Task 15 containment and bootstrap filesystem design

The Task 15 wrapper still requires every compiler/linker output beneath the resolved invocation
working directory. A real first attempt used an external bootstrap build directory and was
correctly rejected when rustc requested an absolute libstd output outside cwd. The Task 16 builder
therefore creates a unique, owned, non-symlink `.eos-sdk-rust-build-*` directory directly beneath
the resolved Rust source root and passes its child `rust-build` to x.py. Tests prove an external
build directory fails, every fake linker output stays beneath cwd, preexisting/colliding symlinks
are not followed, and success/failure removes the private directory. Final SDK staging remains
under the requested output parent; no wrapper containment exception or `EOS_RUST_OUTPUT_ROOT`
escape was introduced.

The Plan9-backed source filesystem exposed one bootstrap packaging exception: copyright generation
vendors dependencies beneath hard-coded `<rust-build>/tmp`, and a real host dist failed after two
hours when that heavy temporary vendor tree could not be completed there. Only during the two
dist invocations, after the exact x build, the builder redirects `<rust-build>/tmp` to a unique
real directory directly under native `/tmp`. It rejects an existing symlink, non-directory,
nonempty directory, wrong owner, non-private native directory, or changed target; it verifies the
target before and after use and guarantees cleanup. It does not redirect target outputs or weaken
the wrapper. The successful real copyright pass vendored 1,621 workspace and 29 library entries
and completed on the native scratch filesystem.

Python 3.14 defaults multiprocessing to forkserver on this host, which made bootstrap fail before
Rust compilation. The builder supplies a private `sitecustomize.py` only to x.py and selects
`fork` only when the interpreter reports it as a supported start method. Other platforms retain
their supported behavior, and the package does not depend on a controller-owned Python shim.

## Independent RED / GREEN evidence

The initial source/layout tests were authored while all Task 16 production assets were absent:

- source contracts: 3/3 RED on missing templates, manifest, builder, installer, and example;
- installer subprocess matrix: 4/4 RED because the installer did not exist;
- builder subprocess matrix: 4/4 RED because the builder did not exist.

Focused cycles then established the behavior independently:

- Python 3.14 fake-x inspection was RED because bootstrap selected forkserver; the private,
  supported-POSIX fork startup made it GREEN.
- Three output-layout tests were RED while x.py used an external build directory. Moving only the
  unique bootstrap build directory beneath the resolved source made all 13 tests then present
  GREEN without changing Task 15.
- Three dist-temp tests were RED for missing isolated scratch behavior. The implementation made
  successful redirect/cleanup, nonempty refusal, and preexisting-symlink refusal GREEN; the full
  Task 16 suite was then 15/15 GREEN.
- A fake dist that deletes stage2 rustdoc reproduced the real final-validation failure. The
  primary success test was RED on missing staged rustdoc until the post-build/pre-dist snapshot
  was introduced; that focused test and the full 15/15 suite are GREEN.
- Installer tests prove named rustup linking, exact no-rustup instructions, incomplete-sysroot
  rejection before rustup, and symlink-escape rejection.
- Builder tests prove the exact x command sequence (`build`, host `dist`, EOS `dist`), templates,
  atomic replacement, distribution and input hashes, cleanup, lock/revision checks, and required
  staged files and libraries.

## Authorized narrow bootstrap prerequisites

Full real builds exposed three exhaustiveness/restricted-std gaps outside the original Task 16
file list. Each change was separately authorized, preceded by a direct RED, and kept narrower than
an API or ABI expansion:

1. Rustdoc cfg rendering failed with E0004 because `Os::Eos` had no display arm. EOS short and
   long `target_os` rendering assertions were RED; the single alphabetized `Eos => "EOS"` arm
   made both pass. Focused `./x test --stage 1 src/librustdoc --test-args test_render_` passed
   2/2 with 129 filtered tests and no doc-test failure.
2. EOS libtest compilation failed on the restricted standard-library surface and Unix signal
   decoding. Two EOS result-classification tests were RED. Libtest now opts into
   `restricted_std` only on EOS, excludes EOS from Unix `ExitStatusExt::signal`, and uses the full
   EOS EXITED/TERMINATED status semantics. An EOS `code() == None` is reported truthfully as a
   failed terminated result without inventing a signal. Focused
   `./x test --stage 1 library/test --test-args eos_` passed exactly 2/2 with 58 filtered tests.
3. The EOS sysroot aggregator produced a single E0658 for restricted std. Adding only
   `#![cfg_attr(target_os = "eos", feature(restricted_std))]` made the focused exact
   `./x build --stage 1 --target armv7a-unknown-eos-eabi library/sysroot` pass in 1m25s. No API,
   ABI, or Cargo dependency changed.

These prerequisite corrections are included in the Task 16 exact-delta review package rather than
hidden as unrelated work.

## Real build history and recovery

Multiple complete real attempts were used as integration tests rather than bypassed:

- Rustdoc exhaustiveness, Task 15 output confinement, libtest restricted status handling, and the
  sysroot restricted-std gate each stopped the pipeline and produced the focused cycles above.
- After those corrections, exact x build passed; host dist then exposed the hard-coded Plan9
  copyright scratch failure, leading to the isolated native temp design.
- The next run completed exact x build, host dist, and EOS rust-std dist, then correctly failed
  final validation because bootstrap dist had removed stage2 rustdoc. The sequencing RED and
  post-build snapshot fixed that observed failure without weakening validation.
- A complete post-fix run reached host dist after exact x build in 48m07s but its controlling
  terminal stream was externally terminated. Atomic output remained absent. Its exact owned
  orphan build directory was type/ownership checked and removed; no broad cleanup was used.
- The final rerun is launched as a single detached process with a scoped PID and log under `/tmp`
  so conversation/tool stream lifetime cannot terminate it. The externally restored CMake/CTest
  runtime is exactly Kitware 3.31.10; its official archive matched Kitware's published SHA-256
  `3cb3dd247b6a1de2d0f4b20c6fd4326c9024e894cebc9dc8699758887e566ca7`, and both executables
  report 3.31.10. This recovery changes no production behavior.

## Final verification

The authoritative detached build was PID/SID `500910`, with log
`/tmp/eos-task16-clean3-builder.log` and requested output
`/tmp/eos-task16-clean3-sdk`. A host-namespace check after handoff found the PID absent because
the process had exited successfully: the log ends with `Build completed successfully in 0:08:38`
and `built EOS Rust SDK: /tmp/eos-task16-clean3-sdk`. The 778 MiB SDK was therefore retained as
the atomically published result and was not rebuilt. Its installed ARM GNU compiler reports
14.3.1, binutils reports 2.44, and the packaged host tools report Rust/Cargo/rustdoc 1.97.1-dev.

Deep package inspection found 482 entries and no symlinks. Recomputing the builder's
`sha256-tree-v1` algorithm over the installed input trees exactly reproduced release-manifest
hashes `e1defb09a5174e8bcf903524fd83c46706b066166d8eace093445c4a0344775e` for ARM GNU and
`5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910` for EOS. The target
sysroot contains all required rlibs and shared `libstd`; the installed example contains exactly
`Cargo.toml` and `src/main.rs`, even when the source fixture is poisoned with a lockfile and target
artifact before packaging.

`install-eos-sdk` validated the real package and linked `eos-1.97.1` in the isolated rustup home
`/tmp/eos-task16-isolated-rustup`. With the rustup proxies and packaged SDK `bin` both on `PATH`,
ordinary stable Cargo (no target JSON and no `-Z build-std`) built `hello-std` against the named
toolchain and precompiled target sysroot. The base artifact was a 1,748,696-byte ELF32,
little-endian ARM EABI5 soft-float `ET_DYN` PIE and passed the packaged base validator. The
authentication packager produced a 1,748,825-byte file; independent inspection proved its exact
129-byte marker, SHA-256 digest, and newline. Allowed-trailer validation passed, while default
base validation rejected the authenticated file with status 2 and `authentication trailer is not
allowed for a base ELF`. Generated Cargo lock/target state was then removed from the source tree.

Fresh final source verification produced:

```text
python3 -m unittest src/tools/eos-sdk/tests/test_sdk_layout.py -v
  Ran 17 tests in 83.432s ... OK

python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_*.py' -v
  Ran 61 tests in 123.790s ... OK

python3 -m unittest -v \
  tests.eos.host.test_ffi_unwind_policy \
  tests.eos.host.test_bootstrap_target \
  tests.eos.host.test_pal_cfg_scope \
  tests.eos.host.test_libc_links \
  tests.eos.host.test_libc_source_provenance \
  tests.eos.host.test_toolchain_lock
  Ran 37 tests in 57.100s ... OK

PYTHONPYCACHEPREFIX=/tmp/eos-task16-pycache \
  python3 -Wall -Werror -m py_compile \
  src/tools/eos-sdk/bin/build-eos-sdk \
  src/tools/eos-sdk/bin/install-eos-sdk \
  src/tools/eos-sdk/tests/test_sdk_layout.py
  exit 0
```

The final exact scope is the eight Task 16 files named in the brief plus the five authorized,
test-backed bootstrap prerequisites listed above. The nested backtrace gitlink remains
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`; no nested worktree or Task 17/controller ledger
file is changed. The resulting outer commit is
`fbd7cf8017adcbb2a99124a3f0ed26bb253e1904` with the required subject
`dist: package the EOS Rust SDK and sysroot`. A post-commit full untracked scan was empty, the
hello source tree contained only its two committed files, and no private `.eos-sdk-rust-build-*`
directory remained.

## Remaining concerns

- A rustup-linked custom toolchain selects the packaged compiler but does not itself add the
  custom toolchain directory to process `PATH`; ordinary Cargo use therefore needs the packaged
  SDK `bin` visible so `eos-rust-link` can be resolved. This is the same explicit `PATH` contract
  printed by the installer's no-rustup path and does not require unstable Cargo behavior.
- Hardware loader/authentication and unwind execution remain the documented manual XC7Z030 and
  XC7Z045 release gates; Task 16's completion evidence is build, package, and static validation.
- The Task 15 nested backtrace commit must accompany the outer gitlink when the branch is
  published.

## Independent review hardening

Independent review of the primary Task 16 commit found package-closure, full-input hashing,
baseline/provenance, installer validation, TOCTOU, and atomic-publication gaps. The approved fix
range is integrated as commits `f26bd86b`, `a783c331`, `49855a78`, and `680eb7a0`. The builder now
packages exactly five MARTOS runtime libraries and no proprietary EOS headers or sources, hashes
and rechecks the complete supplied ARM GNU/EOS roots, validates the exact EOS release heading and
Rust/backtrace/libc provenance, publishes with Linux `RENAME_NOREPLACE`, and hard-codes and
recursively validates the installer inventory and release manifest.

The first scoped re-review found one remaining installer race: root identity was captured only
after semantic validation, allowing a same-path replacement during initial validation to become
the accepted baseline. Fix Round 2 captures device/inode immediately after resolve/type checks,
before every semantic/tree/artifact check, retains the post-validation fingerprint/identity
bracket, and revalidates immediately before rustup execution or no-rustup output. Its focused RED
accepted the replacement; GREEN rejects it, and the existing package-mutation and pre-rustup
replacement controls remain green.

Fresh final-code verification passed Task 16 `32/32`, full EOS SDK discovery `76/76`, the six host
regression modules `37/37`, and strict Python compilation. Per the user's exactly-one-builder
constraint, the sole real builder ran on clean recovery commit
`baeb1a5b368d15d98a5433a3aadd216f472bf9e2`, which is production-equivalent to the integrated
builder/package fix. It atomically published `/tmp/eos-task16-fix1-replacement-sdk`; full-root
hashes reproduced the release manifest, the package contained 36 directories, 116 files, no
symlinks, exactly five EOS libraries, and the exact two-file example. Isolated named-toolchain
Cargo, base ELF validation, authentication packaging, allowed-trailer validation, and default
trailer rejection all passed. Fix Round 2 changes only installer/test/report code and was not
given a second real builder. The scoped re-review explicitly approved this evidence boundary and
returned all findings addressed with no new Critical or Important breakage.
