# EOS Final Release Integration Design

**Date:** 2026-08-21

**Status:** Proposed for implementation after user review

## Purpose

The task-scoped implementation is complete through Task 18, but the final whole-branch review
found three integration gaps:

1. signed board results are not bound to the exact reviewed release-manifest bytes;
2. the reviewed SDK predates the capability manifests and manual board-test bundle that the
   approved design requires it to package; and
3. several board applications cannot honestly produce the mandatory acceptance evidence.

This design closes those gaps without widening the EOS ABI, target, relocation allowlist,
unwind boundary, or capability claims. It authorizes one additional final SDK build after the
integration changes are committed. Physical board execution remains manual and outside the
repository.

## Constraints

- The built-in target remains exactly `armv7a-unknown-eos-eabi`; no hard-float sibling is added.
- The public native ABI and its exact `eos_rust_*` export set remain unchanged.
- Linker, ELF-validation, authentication-trailer, unwind, and capability semantics remain
  fail-closed.
- No proprietary EOS headers, credentials, board locations, transfer commands, or deployment
  automation enter the repository or SDK.
- The SDK continues to package only the five reviewed MARTOS runtime libraries.
- The final SDK is built once from a clean committed integration head. Hash pins are updated
  afterward without rebuilding it.
- No hardware result or signature is synthesized. The release remains incomplete until genuine
  organization-signed XC7Z030 and XC7Z045 results pass.

## Identity model

### Separate package and board-result identities

The SDK cannot contain a board policy that embeds the SHA-256 tree hash of the SDK containing
that policy; doing so would be self-referential. The two release layers therefore bind different
identities:

- **Task 17 automated release gate:** pins and recomputes the complete final SDK
  `sha256-tree-v1` digest and the complete ARM GNU input-tree digest before any SDK/toolchain
  executable runs.
- **Task 18 manual board gate:** pins the exact SHA-256 digest of the generated
  `release-manifest.toml` bytes. That manifest fixes the release/toolchain/target identity,
  source revisions, input-tree hashes, and every distribution archive name and hash.

Board results no longer carry an SDK-tree digest. They carry the exact release-manifest digest
and the identities derived from that manifest. The final release procedure always runs Task 17
against the exact SDK before accepting Task 18 results, so the two gates jointly bind package
contents and release provenance without a hash cycle.

### Exact release-manifest handling

`check_results.py` reads `release-manifest.toml` into one immutable byte snapshot. It decodes,
parses, validates, and hashes that same snapshot. The digest must equal the reviewed constant in
the adjacent board-test policy; it is not derived from or trusted merely because the caller
provided the file.

The board-result test seam may construct a temporary policy with a fixture digest. The public
CLI cannot override schemas, capability policy, board policy, or the expected digest.

### Generated packaged policy

The source board policy contains the fixed semantic sets and the last reviewed release identity.
During SDK assembly, the builder generates the packaged board policy from those semantic sets
and the just-created release manifest. It replaces only release-specific expected values with
the actual manifest-derived values and its exact byte digest. It does not copy a stale source
digest.

After the one final build succeeds, the repository policy and Task 17 SDK identity constants are
updated to the generated values. The packaged and repository policies therefore describe the
same final release even though the source branch gains a later bookkeeping/pin commit.

## SDK package contents

The SDK layout, builder, installer, and their contract tests require these additional assets:

- `manifests/capabilities.toml`
- `manifests/release-manifest.schema.json`
- `share/board-test/board-test-manifest.toml` (generated release-specific policy)
- `share/board-test/result.schema.json`
- `share/board-test/check_results.py`
- `share/board-test/docs/capabilities.md`
- `share/board-test/docs/manual-board-test.md`
- `share/board-test/docs/releasing.md`
- the exact board-application source bundle and C caller needed to reproduce the reviewed
  binaries

The builder stages explicit files only. Cargo locks, target directories, caches, result files,
keys, credentials, and arbitrary neighboring files are excluded. The installer hard-codes the
same required inventory and applies its existing recursive type, symlink, fingerprint, and
root-identity checks.

## Board applications and evidence

### Process

The process application records its own arguments, environment access, current working
directory, and standard streams. It self-spawns through `current_exe()` in a child mode and
exercises piped input/output, `try_wait`, conditional termination, and repeated wait. It does not
request unsupported per-child environment or current-directory overrides and does not depend on
a nonexistent external child.

### Network

The network application completes self-contained numeric IPv4 loopback TCP and UDP exchanges.
Its DNS probe records the stable `Unsupported` result without aborting numeric transport tests.
It does not imply IPv6, multicast, or DNS support.

### Panic, FFI, and backtrace

- The unwind application calls `Backtrace::force_capture()`, prints retained address evidence,
  then proves Rust panic catching and destructor execution.
- The FFI-containment artifact links the real C caller to the Rust static library so the board
  executes ordinary C-to-Rust calls for both normal and contained-panic paths.
- The ordinary `ffi-abi` application remains as mixed scalar/aggregate ABI evidence; it is not
  mislabeled as the C-caller containment proof.

The Task 17 artifact gate builds, validates, authenticates, retains build IDs for, and uploads
every application named by the board policy in both debug and release profiles. The manual
result checker continues to require identical corresponding authenticated bytes on both board
families.

## CI integration

`tests/eos/run-ci.sh` adds the Task 18 checker/schema unit suite. The existing workflow still
contains one release-gate command and no board action.

Final automated verification includes:

- all existing Task 17 layers;
- SDK builder/installer tests for the new bundle and exact-file hygiene;
- board checker tests, including exact manifest digest and same-snapshot mutation controls;
- cross-linking and static validation for the full board application set;
- named-toolchain ordinary Cargo smoke from the rebuilt SDK;
- base ELF validation, exact authentication trailer, allow-trailer validation, and default
  trailer rejection; and
- deep final SDK inventory, symlink, provenance, input-hash, and source-revision checks.

## One-build implementation sequence

1. Add failing controls for the exact release-manifest digest/snapshot, SDK bundle inventory,
   truthful applications, full artifact set, and Task 18 CI inclusion.
2. Implement the integration changes and make all focused/source tests green using the currently
   reviewed SDK where applicable.
3. Commit the clean prebuild integration source.
4. Run exactly one additional full SDK builder from that clean commit to a fresh output path.
5. Inspect the generated SDK, release-manifest digest, SDK tree digest, source revision, package
   inventory, and board policy.
6. Update only the repository release pins and evidence report to those generated values; commit
   separately. Do not rebuild.
7. Run the exact full CI gate with the new SDK, isolated named-toolchain Cargo smoke, validators,
   authentication checks, board checker unit suite, static checks, and clean-state proof.
8. Dispatch an independent whole-branch re-review.

## Failure handling

- Any absent required SDK asset, extra recursively copied artifact, symlink escape, identity
  mismatch, or release-manifest mutation fails before publication or release acceptance.
- Any application that cannot build, link, authenticate, or statically prove its expected
  target properties blocks the final SDK pin.
- Any missing board, missing run, failed acceptance row, unsupported/core-capability mismatch,
  signature failure, or cross-board binary mismatch blocks the manual release.
- If physical results or a release bundle remain unavailable, implementation can be reviewed as
  ready, but the overall release remains explicitly incomplete.

## Out of scope

- Board transfer, loading, console automation, addresses, or credentials
- New native services or capability promotion
- ABI, target, float-ABI, relocation, TLS, random, process-exec, or unwind-policy changes
- Replacing the pinned EOS, ARM GNU, Rust, libc, or backtrace baselines
