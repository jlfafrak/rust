# Task 6 Canonical r2 Final Verification Report

## Status and boundary

Repository-side verification of the canonical replacement SDK passed. This report records only
the final integrated CI, isolated named-toolchain Cargo smoke, strict read-only/static checks,
and the honest hardware boundary. It did not invoke `build-eos-sdk`, mutate either SDK, change
production or test behavior, run the real hardware checker with fixtures, deploy, or access a
physical board.

Verification ran from exact clean source HEAD
`39008f336258ab42492f428d49dafe76966d236d`. The `library/backtrace` gitlink and checkout were
both exact `02ef1b533157e8ddbd0f9295c867e79b59e9bbbd` and clean. Canonical input
`/tmp/eos-final-release-sdk-r2` had these independently reproduced identities before and after
every gate:

```text
SDK sha256-tree-v1             6599a2b56b3a842125f5a9a72f8b21c4a27a7a767f6a7465ae0355c67685bfba
SDK sha256-tree-mode-v1        17798957eb020e568d6fea5bf85c91355602a43bfe277e26b934304eacb0f9ff
release-manifest SHA-256       f4d12b3d57b1e8c71446938e5b595700b73792b80ef10dbddb9fa938af4dacc4
generated board-policy SHA-256 1aa11ebf8b20538d256a6cc6e5d6ca5e2eaeb0c02bce6eb0c293fb1107f1e61c
sorted file-list SHA-256       a41c460a1f7158ee9e54ae0072cafc0e557d47fc69cc0a05d1ddc6f18f4b214e
```

The SDK remained exactly 141 regular files and 56 directories including the root, with zero
links or unsupported entries. All 56 directories were `0755`; the exact 35-file executable
allowlist was `0755`; the remaining 106 ordinary files were `0644`; and no special or
group/other-write bits were present.

## Exact integrated CI

The final r2 CI entrypoint was invoked exactly once:

```bash
EOS_RUST_SDK_ROOT=/tmp/eos-final-release-sdk-r2 \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-final-runtime/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-integrated-ci-r2.pBTV5r/artifacts \
EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-integrated-ci-r2.pBTV5r/pycache \
tests/eos/run-ci.sh
```

It ran from `2026-08-25T22:23:15.430200939Z` through
`2026-08-25T22:38:38.811357263Z`, elapsed `923.381156324s`, exited `0`, and ended with the exact
`EOS CI gates passed` line. The retained log is
`/tmp/eos-integrated-ci-r2.pBTV5r/integrated-ci.log`, SHA-256
`09d4e6b3d89f477609e854c1b0b7fc383081af080f4bf23644ff18320289d324`.

```text
release identity preflight              PASS (SDK + complete ARM GNU tree)
Task 18 board checker                    21/21 PASS in 7.325s
toolchain lock                            1/1 PASS in 0.002s
compiler/rustc_target EOS tests           2/2 PASS (326 filtered)
library/test EOS tests                    2/2 PASS (58 filtered)
native EOS ABI CTest                     45/45 PASS, 0 failed in 35.49s
libc link/provenance                      6/6 PASS in 13.463s
bootstrap/PAL/FFI-unwind policy          30/30 PASS in 23.699s
SDK builder/installer/tool discovery     85/85 PASS in 331.227s
EOS target sysroot build                  PASS
C/Rust layout comparison                 111/111 ARM facts matched
static ELF/ABI/identity/CI suite         24/24 PASS in 230.181s
total explicit test cases               216 PASS, 0 failed
```

The retained CI artifact root contains 71 files, five directories including the root, zero
links or other entries, and 51,651,619 bytes. Its legacy tree identity is
`89605071c8148cd3ab91dd6572c13dec5e31bb79905c2216039ba4f39ab1d52a`; its mode-aware tree
identity is `dc496a2a96f81099cba6ff138573568782bf4e3770f4ff6b966160ded216d9d2`.
All 71 artifact checksums and all 141 SDK checksums passed. The evidence manifest is
`/tmp/eos-integrated-ci-r2.pBTV5r/evidence-manifest.sha256`, SHA-256
`74b90ec46462e59b5e9c08f06b95e14fb4f8d5c5e8b58d4f1af01c4468a75828`.

## Isolated named-toolchain Cargo smoke

The retained private smoke root is `/tmp/eos-final-r2-cargo-smoke.MTMyHaS3gAun`, mode `0700`.
Only packaged `examples/hello-std/Cargo.toml` and `src/main.rs` were copied. Under isolated
`RUSTUP_HOME`, `CARGO_HOME`, and Python cache state, the source installer linked exactly
`eos-1.97.1 /tmp/eos-final-release-sdk-r2`; rustup proxies reported Cargo and rustc
`1.97.1-dev`.

The single ordinary build used no target JSON and no `-Z build-std`:

```bash
/home/dev/.cargo/bin/cargo +eos-1.97.1 build \
  --manifest-path /tmp/eos-final-r2-cargo-smoke.MTMyHaS3gAun/hello-std/Cargo.toml \
  --target armv7a-unknown-eos-eabi
```

It exited `0` with a project-local target directory. The packaged validator accepted the base
ELF. The packaged authentication tool produced the authenticated ELF, and independent byte and
digest checks proved that the only addition was the exact 129-byte trailer: the reviewed
64-byte marker, 64 lowercase SHA-256 characters for the base ELF, and one newline.

```text
base ELF
  size   1748640
  sha256 a0f42f0039da6459002d745abe19faf4f40844c3f8badc21c05f99a397cd83fb
authenticated ELF
  size   1748769
  sha256 86edcf7380fc4c3616b341f135234c25328c515022218753830c2c016de3dbf4
size delta 129 bytes
```

`eos-elf-validate --allow-auth-trailer` accepted the authenticated ELF. Default validation
rejected it with exact status `2` and `authentication trailer is not allowed for a base ELF`.
After retaining both evidence ELFs, cleanup removed only the resolved private target directory
and Cargo lock; the copied project again contained exactly the two original source files.

## Strict final checks

Fresh read-only/static verification passed:

```text
python3 -Wall -Werror -m py_compile     24 tracked + 6 packaged Python programs
JSON parsing                            2 source + 2 package documents
TOML parsing                           16 source + 17 package documents
bash -n tests/eos/run-ci.sh             PASS
git diff --check                        PASS
tracked/package executable data modes   PASS
six mode-policy representations         exact equality
EOS OS target-list entry                exactly armv7a-unknown-eos-eabi
installer inventory validation          PASS with controlled no-rustup PATH
release identity and mode gates          PASS
native ABI export checker               exact 120/120 expected symbols
```

The deep package audit again proved the exact top level, ARM ten-file closure, five MARTOS
libraries, 23-file board bundle, two-file example, 40 byte-identical source copies, and absence
of keys, results, release bundle, Cargo targets/locks, bytecode, proprietary EOS headers/sources,
links, unsupported entries, and permission violations.

## Review and hardware boundary

The canonical artifact/pin review approved range `7f623073..627f8cca` with no Critical or
Important findings. Its sole Minor, a one-second launch timestamp mismatch, was corrected in
`39008f336258ab42492f428d49dafe76966d236d`; the targeted supplemental review approved that
correction with no findings. Final whole-branch review remains the next gate after this
documentation-only record.

Read-only checks found `release-results/xc7z030.json`, `release-results/xc7z045.json`,
`release-results/organization-public-key.json`, the `release-results/` directory, and
`release-bundle` absent from the worktree, outer repository, canonical SDK, and temporary
release roots. The real checker was not invoked with fixtures, and no signed result was
fabricated.

Independent whole-branch review of `31afc958..f3a2db35` is APPROVED with no Critical, Important,
or Minor findings.

repository implementation ready; hardware release pending signed XC7Z030/XC7Z045 evidence
