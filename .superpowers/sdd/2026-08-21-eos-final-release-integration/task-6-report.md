# Task 6 Report — Final EOS SDK and Branch Verification

## Status and immutable inputs

Task 6 completed the repository-side verification against the existing final SDK without
invoking `build-eos-sdk`, rebuilding Rust/native/SDK artifacts, mutating the SDK, editing
production or test behavior, changing the progress ledger, fabricating hardware evidence, or
running the real hardware result checker with synthetic inputs.

The frozen inputs independently observed before CI were:

```text
source HEAD                    58b3e851c2d63a1cf5fbf519f90a5241d2ef4e4c
backtrace gitlink              02ef1b533157e8ddbd0f9295c867e79b59e9bbbd
backtrace checkout             02ef1b533157e8ddbd0f9295c867e79b59e9bbbd
SDK root                       /tmp/eos-final-release-sdk
SDK sha256-tree-v1             919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7
release-manifest SHA-256       6aab31c83e79e891dc86b37df843f645d39720e48d0eba0351e4666b75a01150
generated board-policy SHA-256 52210320c433c32256c7d5b74ab3ca57d29890a49c6434e89217b702d4613eba
SDK inventory-list SHA-256     a41c460a1f7158ee9e54ae0072cafc0e557d47fc69cc0a05d1ddc6f18f4b214e
CMake / CTest                  3.31.10 / 3.31.10
CMake binary directory         /tmp/eos-final-runtime/bin
```

The outer `git status --porcelain=v2 --untracked-files=all` exited 0 with no output. The nested
backtrace porcelain was also empty. `/tmp/eos-final-release-artifacts` and
`/tmp/eos-final-release-pycache` were absent before launch. Preflight counted 141 regular SDK
files, 56 directories including the root, zero symlinks, zero unsupported entries, and zero SDK
`__pycache__`/`.pyc` entries.

## Exact full CI

The final full gate ran exactly once with the restored, version-identical runtime path:

```bash
EOS_RUST_SDK_ROOT=/tmp/eos-final-release-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-final-runtime/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-release-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-final-release-pycache tests/eos/run-ci.sh
```

It ran from `2026-08-24T20:42:52Z` through `2026-08-24T21:01:33Z`, exited 0, and ended with
`EOS CI gates passed`. The complete log is `/tmp/eos-final-task6-ci.log`, SHA-256
`04914d755d455057c4b6301250ecfdfb2091b195bb6eee53ec6f30278ac0d8d8`.

Exact layer results were:

```text
release identity preflight              PASS (SDK + complete ARM GNU tree)
Task 18 board checker                    21/21 PASS in 7.619s
toolchain lock                            1/1 PASS
compiler/rustc_target EOS tests           2/2 PASS (326 filtered)
library/test EOS tests                    2/2 PASS (58 filtered)
native EOS ABI CTest                     45/45 PASS, 0 failed
libc link/provenance                      6/6 PASS in 30.629s
bootstrap/PAL/FFI-unwind policy          30/30 PASS in 47.525s
SDK builder/installer/tool discovery     79/79 PASS in 388.859s
EOS target sysroot build                  PASS
C/Rust layout comparison                 111/111 ARM facts matched
static ELF/ABI/identity/CI suite         22/22 PASS in 252.597s
total explicit test cases               208 PASS, 0 failed
```

The CI artifact root is `/tmp/eos-final-release-artifacts`. Independent hashing after the run
found 71 files, 5 directories including the root, zero symlinks, and sha256-tree-v1
`9003cd4dc26df409243a7601aa77b3312c972be107def32ad9e40a7ea8e874a9`. Its
`static-tests/` directory contains 64 files, including base/authenticated/build-ID triplets for
all eight board applications in debug and release profiles plus the softfp objects and evidence.
The seven SDK evidence file hashes are:

```text
arm-gcc-version.txt      cbdb063cf64c6de2e377a8dd456f5229ba2caf95c5c339ed625b1eb01caf5bde
arm-objdump-version.txt  f584ed84999df7772d60d59c0c6d66eda60bfa07161ac2950c27ada0969c5b3f
cargo-version.txt        2aca36095d1472a01a2d14ba3fd8c9e96f26572905145333b443b1dad54e66e6
release-manifest.toml    6aab31c83e79e891dc86b37df843f645d39720e48d0eba0351e4666b75a01150
rustc-version.txt        a394962f35151f0fd25d40f8c20fd6987c914310fcfc8829c35e3cb6d6c8c40c
sdk-layout.toml          44dd84c209d06da03df50729917b5104986adbe0a4e4cf24a6f8ceec9ed2ad0e
source-revisions.toml    64c7a22ce1e04c850d8f87f26978e3bbd1860925e8ee765e6ec055f1055afb8f
```

## Isolated named-toolchain smoke

The retained private smoke root is `/tmp/eos-final-release-smoke.U2cTjSVTOudp`. Its isolated
state is:

```text
RUSTUP_HOME=/tmp/eos-final-release-smoke.U2cTjSVTOudp/rustup
CARGO_HOME=/tmp/eos-final-release-smoke.U2cTjSVTOudp/cargo-home
PYTHONPYCACHEPREFIX=/tmp/eos-final-release-smoke.U2cTjSVTOudp/pycache
copied source=/tmp/eos-final-release-smoke.U2cTjSVTOudp/hello-std
```

Only the packaged `Cargo.toml` and `src/main.rs` were copied. The existing SDK installer command

```bash
/tmp/eos-final-release-sdk/bin/install-eos-sdk /tmp/eos-final-release-sdk
```

validated the real package and linked the isolated toolchain as `eos-1.97.1`. The rustup proxy
reported Cargo `1.97.1-dev` and rustc `1.97.1-dev`. The successful ordinary Cargo command was:

```bash
/home/dev/.cargo/bin/cargo +eos-1.97.1 build \
  --manifest-path /tmp/eos-final-release-smoke.U2cTjSVTOudp/hello-std/Cargo.toml \
  --target armv7a-unknown-eos-eabi
```

It used the built-in target by name with no target JSON and no `-Z build-std`. The target output
was kept beneath the private copied project so the fail-closed linker output-root policy was
satisfied. The packaged validator accepted the base ELF. The packaged `eos-auth-package` created
the authenticated ELF, and an independent byte comparison proved that the only appended bytes
were the exact 129-byte marker, lowercase SHA-256 digest, and newline trailer.

```text
base ELF
  path   /tmp/eos-final-release-smoke.U2cTjSVTOudp/evidence/hello-std-base.elf
  size   1748640
  sha256 55c173e470d18df9a8e205edb3d66323d93fb6e39301a93ff522887f78e833f4
authenticated ELF
  path   /tmp/eos-final-release-smoke.U2cTjSVTOudp/evidence/hello-std-authenticated.elf
  size   1748769
  sha256 aee802e53f0fab4fb90630d291f0fdedfdd741c55e90c326b6955f48daa018b0
size delta 129 bytes
```

`eos-elf-validate --allow-auth-trailer` accepted the authenticated ELF. Default validation
rejected it with status 2 and `authentication trailer is not allowed for a base ELF`. Generated
Cargo target directories and `Cargo.lock` were removed after the two evidence ELFs were retained;
the copied source again contains exactly two files. Cargo never wrote into the SDK, whose pycache
count remained zero.

Three observer issues were diagnosed without source or SDK edits: the command runner rejected an
initial `rm`-based cleanup before starting; an initial version query resolved packaged Cargo
instead of the rustup proxy because SDK `bin` was first in `PATH`; and the first build put
`CARGO_TARGET_DIR` in a sibling directory, which the reviewed linker correctly rejected as outside
its resolved working directory. Explicit rustup-proxy invocation and a private project-local
target directory addressed the observer setup. The final smoke above is a fresh successful build
and complete validation, not an extrapolation from either failed observer attempt.

## Strict final branch and package checks

The following fresh read-only/static checks passed:

```text
python3 -Wall -Werror -m py_compile     15 EOS Python files PASS
JSON parsing                            9 source/package JSON documents PASS
TOML parsing                           33 source/package TOML documents PASS
bash -n tests/eos/run-ci.sh             PASS
git diff --check                        PASS
tracked executable/data modes           PASS
real install-eos-sdk inventory check    PASS without rustup mutation
EOS OS target-list entries              exactly armv7a-unknown-eos-eabi
native ABI export checker               exact 120/120 expected symbols
backtrace gitlink/checkout/porcelain    exact and clean
```

The mode proof used `git ls-files -s`: `run-ci.sh`, the builder/installer, linker, validator, and
packager are `100755`; the board checker source is `100644`. The target-list check selected the OS
component exactly; a deliberately broad diagnostic substring also finds upstream
`aarch64-unknown-teeos`, which is not an EOS OS target and does not alter the exact one-entry
result.

The final deep SDK check independently repeated the sha256-tree-v1 hash and release/policy hashes.
It proved the exact top-level layout, ARM closure of 10 files, MARTOS closure of exactly five
libraries, board bundle of exactly 23 files, two-file hello example, 40 byte-identical source copy
operations, repository/generated policy byte equality, no path escapes, and no Cargo locks,
targets, symlinks, pycache, unsupported entries, keys, or results. The conclusive SDK tree hash
remained `919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7` after every check.

Immediately before creating this report, a fresh full outer
`git status --porcelain=v2 --untracked-files=all` exited 0 with no output. This report is the only
authorized repository change for Task 6.

## Honest hardware boundary

Read-only checks found these exact authentic release paths absent:

```text
release-results/xc7z030.json                   ABSENT
release-results/xc7z045.json                   ABSENT
release-results/organization-public-key.json   ABSENT
release-results/                               ABSENT
release-bundle                                 ABSENT
```

No organization-controlled public-key path was supplied. The real result checker and release
bundle validation were therefore not invoked, and no unit-test fixture or synthetic signature was
substituted. Genuine organization-signed XC7Z030 and XC7Z045 runs, their matching public key, and
the release bundle remain the manual release blocker.

~~repository implementation ready; hardware release pending signed XC7Z030/XC7Z045 evidence~~

**SUPERSEDED by final-review fix wave 1:** the readiness statement above is withdrawn. The
retained `/tmp/eos-final-release-sdk` has noncanonical package permissions and remains immutable
evidence of the rejected build. Repository readiness is pending a controller-owned replacement
build to fresh `/tmp/eos-final-release-sdk-r2`, review of its exact package modes, new content and
mode-aware SDK identity pins, and the required replacement regressions. Authentic signed
XC7Z030/XC7Z045 evidence remains a separate hardware release boundary.

This statement does not claim that the overall hardware release is complete. Independent final
review and progress-ledger bookkeeping remain controller responsibilities after this report-only
commit.

## Canonical replacement build and pin addendum

The pending replacement named above is now built and pinned. The retained
`/tmp/eos-final-release-sdk` and all Task 5/first-attempt evidence remain immutable rejected or
historical evidence; none of this report's earlier full-CI or smoke outcomes is relabeled as an
r2 result.

The first replacement attempt was suspended for the user-requested pause. Its 7,200-second
subprocess timeout continued to count wall time and expired immediately after resume, so it exited
`2` solely for that pause-induced timeout. The user then authorized exactly one retry. That retry
exited `0` with invocation count one and published `/tmp/eos-final-release-sdk-r2`; no retry or
relaunch followed it.

Independent r2 inspection passed the complete 141-file/56-directory content closure and exact
canonical mode closure: 56 directories at `0755`, 35 exact executable files at `0755`, and 106
ordinary files at `0644`, with no links, unsupported entries, special bits, or group/other writes.
Its identities are:

```text
SDK sha256-tree-v1             6599a2b56b3a842125f5a9a72f8b21c4a27a7a767f6a7465ae0355c67685bfba
SDK sha256-tree-mode-v1        17798957eb020e568d6fea5bf85c91355602a43bfe277e26b934304eacb0f9ff
release-manifest SHA-256       f4d12b3d57b1e8c71446938e5b595700b73792b80ef10dbddb9fa938af4dacc4
generated board-policy SHA-256 1aa11ebf8b20538d256a6cc6e5d6ca5e2eaeb0c02bce6eb0c293fb1107f1e61c
SDK inventory-list SHA-256     a41c460a1f7158ee9e54ae0072cafc0e557d47fc69cc0a05d1ddc6f18f4b214e
```

The focused stale-pin controls failed for the exact old SDK-tree and release-manifest identities.
After the pin-only patch, real SDK/static identity and release/capability identity passed, the
board tests passed 21/21, the mode/hash tests passed 2/2, and strict compilation passed for 24 EOS
Python files. Pin commit `e9ff5e0c11e8d4ea53840c49504fc7b07b5939c3` has exact subject
`build: pin canonical EOS release SDK` and changes only the three authorized pin/fixture files.

The complete attempt, inspection, identity, mode, installer, mutation, RED/GREEN, and immutable
evidence is recorded in `task-6-replacement-build-report.md`. This replacement-build task did not
run final integrated CI, the real hardware checker, a Cargo smoke build, deployment, or physical
hardware. Those controller review gates remain pending against exact canonical r2.
