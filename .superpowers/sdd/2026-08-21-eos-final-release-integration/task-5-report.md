# Task 5 Report — Single Authorized Final EOS Release SDK

## Status and one-shot boundary

Task 5 is complete. Exactly one real src/tools/eos-sdk/bin/build-eos-sdk process was launched,
it exited 0, and it atomically published /tmp/eos-final-release-sdk. No retry, relaunch,
duplicate builder, production behavior edit, Task 6 CI run, controller-ledger edit, backtrace
edit, deployment action, or physical-board action occurred.

The frozen source identity was 61fcff440daed52c444398d00f6a680b215db5f7. The pin-only
product/evidence commit is 3dd3f425d08f295539f4987bafe7a736fc9f76f4, with exact parent
61fcff440daed52c444398d00f6a680b215db5f7 and required subject
"build: pin final EOS release SDK".

## Conclusive preflight

Before any builder invocation or repository/report edit, the preflight proved:

- outer HEAD exactly 61fcff440daed52c444398d00f6a680b215db5f7 and a completely empty tracked
  and untracked porcelain;
- backtrace gitlink, checked-out HEAD, and clean nested porcelain all exactly
  02ef1b533157e8ddbd0f9295c867e79b59e9bbbd;
- CMake and CTest 3.31.10 from /tmp/eos-final-runtime/bin; the restored Kitware archive
  SHA-256 was 3cb3dd247b6a1de2d0f4b20c6fd4326c9024e894cebc9dc8699758887e566ca7;
- ARM GNU GCC 14.3.1 and binutils 2.44.0.20250616 under
  /home/dev/code/arm-toolchain-build/custom-arm-libs;
- EOS Release-Notes.md contained the exact heading "## Build 14.0.39" under
  /home/dev/code/gpt-test/lib/martos-smp-14.0.39;
- complete ARM input sha256-tree-v1
  a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d and complete
  EOS input sha256-tree-v1
  5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910;
- the exact five required EOS runtime libraries existed;
- /tmp/eos-final-release-sdk and /tmp/eos-final-builder-pycache were absent; and
- no live builder, private Rust build, native SDK build, dist scratch, or orphan builder root
  existed.

The full outer porcelain needed 178 seconds on the mounted filesystem and exited cleanly with no
output. It was allowed to complete naturally rather than being terminated for latency.

## Sole builder launch and result

The durable launch identity is:

~~~text
launched_at_utc=2026-08-24T18:20:12Z
builder_pid=602935
durable_session=53161
invocation_count=1
source_root=/mnt/c/Users/jfafrak/Documents/ChatGPT/eos-rust-target/.worktrees/eos-rust-1.97.1
arm_gnu_root=/home/dev/code/arm-toolchain-build/custom-arm-libs
eos_sdk_root=/home/dev/code/gpt-test/lib/martos-smp-14.0.39
output=/tmp/eos-final-release-sdk
PATH_prefix=/tmp/eos-final-runtime/bin
PYTHONPYCACHEPREFIX=/tmp/eos-final-builder-pycache
~~~

The collision-safe evidence directory is /tmp/eos-final-task5-build.2W5yli6mfp; it contains
launch.txt, builder.pid, exit-status, and build.log. The builder native ABI phase passed 45/45
CTests and the MARTOS cross-build/export gate. Exact bootstrap build completed in 32m00s, host
rustc/Cargo distribution in 23m20s, and EOS rust-std distribution in 5m33s. The durable process
exited 0; exit-status contains 0, and the log ends with:

~~~text
Build completed successfully in 0:05:33
built EOS Rust SDK: /tmp/eos-final-release-sdk
~~~

All owned builder/private build roots were cleaned on success. The published SDK was retained
and never rebuilt.

## Independent final package inspection

The conclusive isolated inspection passed with log
/tmp/eos-final-task5-build.2W5yli6mfp/inspection-final.qj3ZTzu1PZ.log. It found:

~~~text
regular files                 141
directories beneath root      55
directories including root    56
symlinks                        0
inventory SHA-256 a41c460a1f7158ee9e54ae0072cafc0e557d47fc69cc0a05d1ddc6f18f4b214e
~~~

The exact top-level layout is arm-gnu, bin, eos, include, lib, linker, manifests, and share.
Every path resolves beneath the SDK. The EOS closure is exactly the five required byte-identical
MARTOS libraries and contains no proprietary EOS headers or sources. The ARM closure is exactly
the ten staged compiler/binutils/support files. The manual board bundle is exactly 23 files; the
ordinary-Cargo example is exactly Cargo.toml and src/main.rs; 40 static package assets are
byte-identical to their reviewed source files.

There are no Cargo locks, target directories, keys, results, credentials, private-key markers, or
poison files. Required host tools and the prebuilt EOS target sysroot are present. The packaged
tools report Rust/Cargo/rustdoc 1.97.1-dev, GCC 14.3.1, and binutils 2.44.0.20250616. The target
shared std is ELF32 little-endian ARM, ET_DYN, EABI5 softfp, VFPv3, NEONv1, with no hard-float
Tag_ABI_VFP_args. The native ABI archive exports exactly the reviewed 120 symbols.

Independent identity recomputation produced:

~~~text
SDK sha256-tree-v1       919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7
release manifest SHA-256 6aab31c83e79e891dc86b37df843f645d39720e48d0eba0351e4666b75a01150
ARM input sha256-tree-v1 a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d
EOS input sha256-tree-v1 5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910
board policy SHA-256     52210320c433c32256c7d5b74ab3ca57d29890a49c6434e89217b702d4613eba
~~~

The release manifest identifies Rust fork 61fcff440daed52c444398d00f6a680b215db5f7,
upstream Rust 8bab26f4f68e0e26f0bb7960be334d5b520ea452, libc
71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8, and backtrace
02ef1b533157e8ddbd0f9295c867e79b59e9bbbd. The verified generated release manifest records these
six distribution identities:

~~~text
cargo tar.gz    633bcf40e6654364800c403fd70a0a3cd861469adfbefbd909e24322895f09d1
cargo tar.xz    2e7ec8fc262ba7fed2e08fb77f7dc5d518a4580adbddf6ae6f6930033f1f7983
rust-std tar.gz 75f9cde5c45f2482b8bd8bc1c3e6a296e252240de7d147dde9a2815f9ea19e4b
rust-std tar.xz 1362e288b13fff2833577ef6b233b357da4367718b75f887f73c52eb58b4d192
rustc tar.gz    289f4d74fd7aa8765cec014487c556afb7a8b8396308396174dd40b9da77c37d
rustc tar.xz    e9e9f5b220ef63af595da669fd572a273ec005738903a82281353f8307aa5804
~~~

Evidence boundary: the six intermediate distribution archives were cleaned with the builder's
scratch state and are not part of the published SDK, so their final payload hashes cannot now be
independently recomputed. Their names and digests are cryptographically bound by the verified
generated release manifest and originate from builder-side hashing. This supports the recorded
manifest identities, but is not a post-build payload rehash claim.

The generated board policy exactly derives the release digest, revisions, input hashes, ABI 1.0,
and linker-script SHA-256 835c2afac09937fb1b6f7dc93027134c61b34bf645eb2809bbaf1ece4994a674.
The packaged checker resolves its immutable in-package defaults and fails closed without
result/key arguments; it makes no hardware claim. The packaged installer validates the exact
closure and, with rustup absent from PATH, prints explicit use instructions without mutating a
toolchain.

## Inspection-control incidents and restoration

Two inspection-harness failures were preserved rather than hidden:

1. The first control asserted 56 directories while Python rglob("*") correctly reported 55
   descendants; the shell count of 56 included the SDK root. The failed log is
   /tmp/eos-final-task5-build.2W5yli6mfp/inspection.DhG7HsqaDx.log.
2. The corrected control imported the packaged checker without a private bytecode cache after
   computing the SDK hash. That observer action created only
   share/board-test/__pycache__/check_results.cpython-314.pyc and its directory, so the installer
   correctly rejected the extra board-bundle entry. The failed log is
   /tmp/eos-final-task5-build.2W5yli6mfp/inspection-corrected.R6eIx8dDQH.log.

Read-only inspection proved those were the only unexpected entries. Exactly that generated pyc
and its now-empty directory were removed, restoring 141 files, 56 directories including root, and
zero symlinks. The entire relevant inspection was rerun with private
PYTHONPYCACHEPREFIX=/tmp/eos-final-inspection-pycache.ER6K20UiVD; it passed, and a post-checker
tree recomputation proved the SDK remained exactly
919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7.

## Pin RED/GREEN and exact committed scope

Before pin edits, the real static identity control failed with exact diagnostic
"reviewed Task 16 SDK tree identity mismatch: /tmp/eos-final-release-sdk", and the board release
control failed with "release manifest does not match exact reviewed release-manifest digest".

The initial five-file pin scope updated the static SDK/Rust-fork constants, generated source board
policy, and Task 16/17/18 reports. A deliberately broader focused board-suite run then exposed
that its synthetic release fixture still carried the superseded fork/distribution identities; it
failed at the newly advanced exact policy before reaching 18 intended test cases. The controller
authorized tests/eos/board/test_check_results.py as a narrow sixth, test-only cryptographic
release-pin file because leaving it stale would knowingly break Task 6 CI. The pin commit changed
its final fork, six distribution hashes, signed result identity, and most corresponding mutation
literals. A focused review follow-up corrected the remaining same-snapshot mutation literal from
the obsolete digest to the exact pinned cargo tar.xz digest and asserted that the replacement
actually changes the fixture bytes. No checker or production behavior was relaxed.

Final verification was:

~~~text
real final SDK/static identity control                  PASS
real final release/capability identity control         PASS
source board policy == packaged generated policy       PASS
tests.eos.board.test_check_results                     21/21 PASS
strict Python -Wall -Werror compilation                PASS
fresh final SDK sha256-tree-v1                         exact
path-scoped and staged git diff --check                PASS
~~~

The exact pin commit contains six modified paths with no mode changes:

- tests/eos/host/test_static_elves.py
- tests/eos/board/board-test-manifest.toml
- tests/eos/board/test_check_results.py
- Task 16 implementation report
- Task 17 implementation report
- Task 18 implementation report

After the commit, tracked staged and unstaged state was clean. The backtrace gitlink and checkout
remained exact and clean at 02ef1b533157e8ddbd0f9295c867e79b59e9bbbd.

## Remaining release boundary

Task 5 intentionally did not run the final integrated CI; Task 6 must consume this exact SDK and
pin commit without invoking the builder again. No authentic organization-signed XC7Z030 or
XC7Z045 result, trusted release key, result bundle, credential, or deployment material exists in
this task. Both physical-board results remain manual and incomplete, so the overall hardware
release gate remains open.
