# Task 6 Report — Canonical EOS Replacement Build

## Outcome and authorization boundary

The canonical replacement is complete at `/tmp/eos-final-release-sdk-r2`. The successful retry
exited `0`, atomically published the package, and was the only retry invocation. No builder was
relaunched after success. No production behavior changed after the build, and this task did not
run final integrated CI, the real hardware checker, deployment, or physical hardware.

There were two separately authorized attempt histories:

1. The first replacement attempt launched once at `2026-08-24T23:10:50Z` with builder PID
   `792855` and SID `792838`. During the user-requested pause, its 7,200-second subprocess timeout
   continued to count wall time. On resume it immediately expired, the child `gmake` received
   `Hangup`, and the builder exited `2` at `2026-08-25T19:37:16Z` with
   `EOS ABI host build failed ... timed out after 7200 seconds`. This failure was caused solely by
   the pause consuming that timeout. It was not retried under the original authorization.
2. After the user explicitly authorized one retry, the retry launched once at
   `2026-08-25T20:03:12Z` and exited `0` at `2026-08-25T21:20:02Z`. Its durable evidence says
   `retry_invocations_started=1`; no further launch occurred.

## Immutable retained evidence

Read-only rehashing before and after replacement inspection reproduced these exact retained
identities:

```text
/tmp/eos-final-release-sdk
  sha256-tree-v1      919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7
  sha256-tree-mode-v1 9a479f8bedf63b0cee542982c3baa79a29a4466f15fc8daf9fb8e7fd36f70579

/tmp/eos-final-task5-build.2W5yli6mfp
  sha256-tree-v1      ffe2a9dd894ff833cbaa229874cee8ef197d69859b6d083f518bbfce7fdeb5c7
  sha256-tree-mode-v1 c5b82c00c475762b132feb7afa895a186e0b9bd42b38d025da3926118db7919b

/tmp/eos-final-task6-replacement-build.JptApeN5S96T
  sha256-tree-v1      e43b44aa4ce76741a128a2489285bb6ae1469dc775d8550922049e13133c1e84
  sha256-tree-mode-v1 dc46ac6347b8f2e37d2254a0e6a346e151c9c94b34d29f3984bc5efe660fe17a
  build.log SHA-256   197e50f6d9fd2839efe80541ed0a1160abc271413ede64596192e2620ab0cad4
```

The rejected r1 package remains 141 files and 56 directories including its root. The Task 5
evidence remains seven files beneath its private `0700` root. The failed-attempt evidence remains
12 files beneath its private `0700` root, with 11 files at `0600` and one at `0644`. None was
renamed, chmodded, removed, or overwritten.

## Retry preflight and launch

The direct, no-pipeline preflight exited `0` at `2026-08-25T20:02:40Z`. It proved:

- outer HEAD exactly `7f62307323c3d38c766e9040b7cb7cc74591713c`, with empty full tracked and
  untracked porcelain;
- backtrace gitlink and checkout exactly
  `02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`, with empty nested porcelain;
- CMake and CTest 3.31.10 from `/tmp/eos-final-runtime/bin`;
- ARM GCC 14.3.1 and binutils 2.44.0.20250616;
- EOS heading `## Build 14.0.39` and exactly the five required runtime libraries;
- full ARM input `sha256-tree-v1`
  `a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d` and full EOS input
  `sha256-tree-v1` `5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910`;
- the immutable identities above; fresh absent r2 and bytecode-cache roots; zero live/orphan
  builders or builder scratch; and retry invocation count zero.

The successful retry evidence root is
`/tmp/eos-final-task6-replacement-retry.8bSaL018py2S` and is private `0700`. Its launch identity is:

```text
builder PID              844574
Linux SID                844553
durable exec session     8481
retry invocation count   1
source root              /mnt/c/Users/jfafrak/Documents/ChatGPT/eos-rust-target/.worktrees/eos-rust-1.97.1
output                   /tmp/eos-final-release-sdk-r2
PYTHONPYCACHEPREFIX      /tmp/eos-final-r2-retry-builder-pycache
```

The builder passed native ABI configuration/build, 45/45 CTests, MARTOS cross-build/export
validation, the host compiler and EOS sysroot build (`0:38:25`), host rustc/Cargo distribution
(`0:24:16`), and EOS rust-std distribution (`0:10:00`). Its log ends with
`built EOS Rust SDK: /tmp/eos-final-release-sdk-r2`. The build-log SHA-256 is
`a7e834242d3e660036984c0c607a726a9a97f56993a7ef7848f22be3c7c8f127`. Builder PID/SID and all
owned builder scratch were absent after exit.

## Independent content and mode inspection

The conclusive inspection used external bytecode root
`/tmp/eos-final-r2-inspection-pycache.CRnySvpa15mV`. Its log is
`/tmp/eos-final-task6-replacement-retry.8bSaL018py2S/inspection-r2.8f7jwMjBIT.log`, SHA-256
`cb7af220d3779ccf7ba66645f9ce01260c5c9418e5bdf33a58b6afe3dedb5e21`, and ends
`inspection_result=PASS` / `inspection_exit=0`.

The atomic package has exactly 141 regular files, 56 directories including its root, zero
symlinks, and zero unsupported entries. Every directory is `0755`; exactly 35 executable files
are `0755`; and exactly 106 ordinary files are `0644`. No special, group-write, or other-write
bits exist. The executable allowlist is exactly:

- `arm-gnu/arm-none-eabi/bin/{as,ld}`;
- `arm-gnu/bin/arm-none-eabi-{ar,gcc,nm,ranlib,readelf}`;
- `arm-gnu/libexec/gcc/arm-none-eabi/14.3.1/collect2`;
- `bin/{build-eos-sdk,cargo,eos-auth-package,eos-elf-validate,eos-rust-link,install-eos-sdk,rustc,rustdoc}`;
- host `lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/{ld.lld,ld64.lld,lld-link,wasm-ld}`; and
- host `lib/rustlib/x86_64-unknown-linux-gnu/bin/{llc,llvm-ar,llvm-as,llvm-cov,llvm-dis,llvm-link,llvm-nm,llvm-objcopy,llvm-objdump,llvm-profdata,llvm-readobj,llvm-size,opt,rust-lld,rust-objcopy}`.

The inspection independently proved exact byte-identical ARM10 and EOS5 closures, the exact
23-file board bundle, exact two-file ordinary-Cargo example, 40 byte-identical static source
copies, and two byte-identical native ABI headers. It found no proprietary EOS headers/sources,
Cargo locks, target directories, links, unsupported entries, keys, results, credentials, pycache,
or poison content.

Rustc, Cargo, and rustdoc report 1.97.1-dev; GCC reports 14.3.1; readelf reports binutils
2.44.0.20250616. The target sysroot contains the required core, alloc, std, panic_unwind,
proc_macro, and test rlibs plus shared std. Shared std is ELF32 little-endian ARM ET_DYN, EABI5
softfp, VFPv3, NEONv1, with no `Tag_ABI_VFP_args`. The native archive exports exactly the 120
reviewed symbols.

Independent identities are:

```text
SDK sha256-tree-v1             6599a2b56b3a842125f5a9a72f8b21c4a27a7a767f6a7465ae0355c67685bfba
SDK sha256-tree-mode-v1        17798957eb020e568d6fea5bf85c91355602a43bfe277e26b934304eacb0f9ff
release-manifest SHA-256       f4d12b3d57b1e8c71446938e5b595700b73792b80ef10dbddb9fa938af4dacc4
generated board-policy SHA-256 1aa11ebf8b20538d256a6cc6e5d6ca5e2eaeb0c02bce6eb0c293fb1107f1e61c
inventory-list SHA-256         a41c460a1f7158ee9e54ae0072cafc0e557d47fc69cc0a05d1ddc6f18f4b214e
ARM input sha256-tree-v1       a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d
EOS input sha256-tree-v1       5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910
```

The six release-manifest distribution identities are:

```text
cargo-1.97.1-dev-x86_64-unknown-linux-gnu.tar.gz
  1952473253ff51e6a16279239c6f55813ebc8f37e34260c4cdc75bd11648a709
cargo-1.97.1-dev-x86_64-unknown-linux-gnu.tar.xz
  0162909029b0ad8cb029d45605760090e55c0b7ca0fb02666812c2f67df81fd0
rust-std-1.97.1-dev-armv7a-unknown-eos-eabi.tar.gz
  edd9774fc2cf88af08253b7e07999f067585adcc696381d35861c1474371e313
rust-std-1.97.1-dev-armv7a-unknown-eos-eabi.tar.xz
  7b3a7a0a5a0d8086a14f754fbad07a6d1e32442e594b595bc70e06cfc28db90c
rustc-1.97.1-dev-x86_64-unknown-linux-gnu.tar.gz
  63b1e564cf5f66d79e09ca118f8c2e4c3d19b06fc4f95f92a409063bc5644527
rustc-1.97.1-dev-x86_64-unknown-linux-gnu.tar.xz
  06e5456e639058f43ac86f226b5ea516b48f2a074885ad13aa0a8b0078a20c5b
```

The fixed source installer accepted r2 with rustup absent and rejected immutable r1 before a fake
rustup could run. On a private r2 copy, chmodding only `share/source-revisions.toml` from `0644`
to `0600` left legacy identity
`6599a2b56b3a842125f5a9a72f8b21c4a27a7a767f6a7465ae0355c67685bfba` unchanged, changed the
mode-aware identity from `17798957eb020e568d6fea5bf85c91355602a43bfe277e26b934304eacb0f9ff`
to `a67c6086ed606eca538a647adb03a2824b6271d640bc547e3bd9de990859181e`, and was rejected before
the fake rustup. The packaged checker defaults resolve only to in-package policy/schema files and
require release-manifest, trusted-key, and result arguments; the real hardware checker was not
run. No observer artifact was created in r2, and both package hashes remained exact afterward.

## Pin RED/GREEN and commits

Before repository edits, the focused static identity command exited `2` with
`reviewed Task 16 SDK tree identity mismatch`, and the real board release validation exited `2`
with `release manifest does not match exact reviewed release-manifest digest`.

After the exact pin-only patch:

```text
real SDK/static content+mode identity   PASS
real release/capability identity        PASS
source policy == packaged policy        PASS
tests.eos.board.test_check_results      21/21 PASS
static mode/hash regressions              2/2 PASS
strict Python -Wall -Werror compile     24 files PASS
path-scoped git diff --check            PASS
```

One observer anomaly is retained honestly. A consolidated command runner reported outer status
`1`, while its durable inner log records every gate passing and ends `pin_verification=PASS` and
`exit=0`. Each component was immediately rerun as an isolated direct command; all returned tool
exit `0` with the exact pass counts above. No source change was made in response.

The pin commit is `e9ff5e0c11e8d4ea53840c49504fc7b07b5939c3`, exact subject
`build: pin canonical EOS release SDK`. It changes only:

- `tests/eos/host/test_static_elves.py`;
- `tests/eos/board/board-test-manifest.toml`; and
- `tests/eos/board/test_check_results.py`.

The follow-up documentation commit has exact subject
`docs: record canonical EOS replacement build` and changes only this report plus the Task 16/17/18
and Task 6 reports. Final integrated CI,
isolated Cargo smoke, independent whole-branch review, progress-ledger bookkeeping, and authentic
organization-signed XC7Z030/XC7Z045 evidence remain controller/manual release boundaries.
