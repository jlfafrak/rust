# Task 18 Report — Capability Matrix and Manual Two-board Release Gate

## Status and boundary

Task 18 implementation is committed as
`334fe9bafeeaed99ce2257ff62dd7dbdfed92f03` with the required subject
`docs: add EOS capability and two-board release gates`. The implementation began at exact clean
HEAD `a02c0c05218efd256e95b66e2f8936e772ebc958` on `codex/eos-rust-target`.

The code, schemas, manifests, focused tests, and manual release documentation are complete. The
overall EOS Rust release is not complete: no authentic organization-signed XC7Z030 or XC7Z045
result exists in this checkout, and no release bundle exists at the plan's result paths. No board
outcome, signature, trusted release key, credential, address, or transfer action was fabricated.

The commit contains exactly these nine Task 18 paths:

- `src/tools/eos-sdk/manifests/capabilities.toml`
- `src/tools/eos-sdk/manifests/release-manifest.schema.json`
- `tests/eos/board/board-test-manifest.toml`
- `tests/eos/board/result.schema.json`
- `tests/eos/board/check_results.py`
- `tests/eos/board/test_check_results.py`
- `docs/eos/capabilities.md`
- `docs/eos/manual-board-test.md`
- `docs/eos/releasing.md`

The focused test module is the one extra board-gate test path required by the controller's
explicit schema/checker test contract. No Task 17 or earlier production behavior, native ABI,
target, linker wrapper, validator, authentication packager, relocation, unwind, PAL, or shim
implementation changed. The controller progress ledger was not modified.

## Fail-closed board result contract

The checker requires exactly two result paths and exactly one result for each of `XC7Z030` and
`XC7Z045`. Each JSON document must pass the full checked-in schema and semantic validation, then
pass its own cryptographic signature verification before it contributes any release evidence.

The result contract requires:

- exact reviewed release-manifest byte digest, toolchain, target, layout, Rust fork/upstream,
  libc, backtrace, ARM GNU release/tree, EOS baseline/tree, native ABI, and linker-script
  identities;
- debug and release profiles exactly once;
- two distinct eight-digit hexadecimal PIE load addresses in each profile;
- the exact application set, with a 40-hex GNU build ID and 64-hex authenticated-file SHA-256
  for every application/profile pair;
- identical corresponding build IDs and authenticated binary hashes across both boards;
- every v1 acceptance row exactly once and passing;
- every audited capability row exactly once and consistent with the capability matrix; and
- an explicit `observed_failures` array that must be empty for release acceptance.

The schema rejects missing/extra fields, wrong primitive types, unsafe or malformed digest/name
shapes, duplicate members, malformed dates, profiles, rows, artifacts, and signatures. The
checker also rejects duplicate row IDs and all cross-field/set/identity mismatches that JSON
Schema alone cannot express.

## Signature boundary

The documented scheme is `RSASSA-PKCS1-v1_5-SHA256`. The signed bytes are canonical JSON of the
entire result except its `signature` member, using sorted keys, compact separators, and ASCII
escaping. Verification is implemented using Python standard-library base64, SHA-256, constant-
time byte comparison, and modular exponentiation against an external public-key JSON file.

The checker requires a modulus of at least 2048 bits, a valid odd public exponent, a canonical
signature integer smaller than the modulus, exact EMSA-PKCS1-v1_5 SHA-256 encoding, and a trusted
key identifier. Public keys enter through repeatable `--trusted-key` arguments for organization-
controlled rotation. The repository contains no release public-key trust anchor and no release
private key. The only private exponent is an explicitly labeled synthetic RSA-2048 test fixture
inside `test_check_results.py`; it is not accepted implicitly and is not release or hardware
evidence.

## Capability truth

The capability matrix marks implemented, automated-contract-covered shim services true while
keeping their physical-board evidence field false. The manual release rows remain mandatory on
both boards.

The fixed v1 exclusions are false with stable `Unsupported` semantics:

- `secure_random`
- `native_elf_tls`
- `hard_float_abi`
- `process_exec_replacement`
- `cross_language_unwind`
- `automatic_board_deployment`

DNS, IPv6 transport, multicast, symlinks, hard links, ownership, permission fidelity,
mmap/protection, dynamic loading, per-child environment, per-child current directory, signals,
and filesystem durability also remain false. An optional capability cannot be true unless the
matrix records both contract and board evidence. A false board row is accepted only as
`unsupported` with exact error `Unsupported`.

## Task 16 release-manifest schema

The release-manifest JSON Schema describes the parsed TOML object actually emitted by Task 16:
the exact `release`, `input_hashes`, `source_revisions`, and `distribution_hashes` tables. It
requires the fixed release identity and algorithm, exact 40/64 lowercase hexadecimal shapes,
safe `.tar.xz`/`.tar.gz` archive names, and SHA-256 values. Semantic validation also requires at
least one rustc, Cargo, and target rust-std archive.

The schema was run against the actual reviewed Task 16 manifest at
`/tmp/eos-task16-fix1-replacement-sdk/manifests/release-manifest.toml`; it passed, and the exact
manifest byte digest was
`fac60315f383ea6341e874a01d2b802e09419f1e5137e5b20129e3f9a160c4b5`.

## RED and GREEN evidence

The initial focused command was:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-red-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

Before production/schema/manifests existed, it ran 11 tests and produced 12 expected failures.
The positive fixture failed because `check_results.py` did not exist; every negative case also
failed to produce its required semantic rejection. The controls covered missing/duplicate board,
review identity mismatch, missing/invalid signature, missing profile/load address/acceptance row/
build ID, observed failure, failed acceptance, cross-board binary mismatch, optional capability
semantics, and release-manifest digest/archive shapes.

The first implementation run exposed that the synthetic test modulus was only 2047 bits. The
checker correctly rejected it under the 2048-bit policy. Replacing only the test-only key with a
full-width RSA modulus made the cryptographic fixtures exercise the intended boundary.

A later mutation control added the reviewed RSA requirement that a signature integer be smaller
than its modulus. Before the production check, a same-width signature plus one modulus was
accepted and the focused test failed because the checker exited 0. After the minimal canonical-
integer check, the complete final focused command passed 12/12 in 6.496 seconds:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-rsa-green-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

The final suite explicitly covers an unknown board part, missing board result, duplicate board
part, mismatched reviewed identities, unsigned result, correct-width tampered signature,
noncanonical RSA signature integer, missing profile, missing and duplicate load address, missing
acceptance row, missing observed-failure list, recorded failure, failed row, missing build ID,
nonidentical authenticated binary, false optional capability policy, and invalid Task 16
release-manifest digest/archive names. Its sole positive input pair is labeled test-only and is
cryptographically signed with the synthetic fixture.

## Task 17 regression evidence

All reviewed environment assets were present. The exact full regression command was:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task18-final-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-task18-ci-pycache tests/eos/run-ci.sh
```

It exited 0 and ended `EOS CI gates passed`. Results were:

```text
release identity                    reviewed SDK + ARM GNU passed
release lock                        1/1 passed
compiler/rustc_target EOS tests     2/2 passed (326 filtered)
library/test EOS tests              2/2 passed (58 filtered)
native EOS ABI CTest               45/45 passed
libc link/provenance audits          6/6 passed in 16.177s
bootstrap/PAL/unwind policy         30/30 passed in 22.474s
SDK contract suite                  76/76 passed in 303.986s
EOS target sysroot build            passed
C/Rust layout comparison            matched 111 ARM layout facts
static ELF/ABI/identity/CI suite    13/13 passed in 61.221s
```

Artifacts are at `/tmp/eos-task18-final-artifacts`.

## Static, scope, and mode checks

Python 3.14 `-Wall -Werror -m py_compile` passed with an external bytecode cache. Both JSON files
and both TOML files parsed successfully. A static invariant check confirmed all required-false
and optional capabilities are false with `Unsupported`, and confirmed the recorded linker-script
SHA-256 against the actual source file. `git diff --cached --check` passed before commit.

The staged scope contained exactly the nine paths listed above, all at mode `100644`. The commit
parent is exact `a02c0c05218efd256e95b66e2f8936e772ebc958`. The `library/backtrace` gitlink and
checked submodule remained exact
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd` with no tracked or cached delta.

## Manual hardware blocker

Read-only inspection found no files beneath `release-results` or `release-bundle`; those paths do
not exist in this checkout. Consequently the real checker and final authenticated bundle
validator were not run against nominal plan paths: there were no authentic inputs to run. This
is the exact environmental boundary, not a checker workaround. An organization operator must
perform the documented manual transfer, EOS console start/capture, and signing flow on both
physical part families before the overall release can be called complete.

## Fix Round 1 — superseding review response

This section supersedes the earlier result-layout, capability-policy, focused-test, and final
regression statements where they differ. Independent review did not approve the original Task 18
implementation. Fix Round 1 preserves its standard-library RSA-2048+
`RSASSA-PKCS1-v1_5-SHA256` verification boundary and makes no change to the signature algorithm,
trust model, or private-key policy.

The separate Fix Round 1 commit is
`2ffb852f29cffc4f1a4fcf2d83a0aebfe0be2b22` with subject
`fix: harden EOS two-board release policy`; its parent is the original Task 18 commit
`334fe9bafeeaed99ce2257ff62dd7dbdfed92f03`.

### Reproduced RED controls

Each review finding was reproduced against the original behavior before its production fix:

- A correctly re-signed result with only one global 19-row acceptance set was accepted despite
  claiming four profile/address combinations. After the first layout change, an additional RED
  control showed that debug and release could still use different address pairs, so the evidence
  was not a true two-profile by two-address cross-product.
- A caller-provided board manifest containing only `hello-std` and
  `pie_load_relocation`, paired with trimmed correctly re-signed results and policy path override
  flags, exited 0.
- End-to-end `layout_version = true` in the release TOML, with matching signed digest updates, and
  `schema_version: true` in both signed results were accepted because Python equates booleans and
  integers.
- Removing the supported `allocation` capability, setting its `contract_evidence` false, or adding
  an unreviewed capability all exited 0 under the old incomplete-inventory policy.
- A correctly re-signed result with `captured_at_utc` set to
  `2026-99-99T99:99:99Z` exited 0 because only a regular expression was applied.

The additional address-set RED command was:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-fix1-red-matrix-pycache \
python3 -m unittest -v \
  tests.eos.board.test_check_results.BoardResultGateTests.test_rejects_missing_profile_or_non_distinct_load_address
```

It failed as intended with `AssertionError: 0 != 2` before the matching production check.

### Fix Round 1 policy and schema

Every board result now has exactly two profiles and each profile has exactly two run records. The
debug and release address sets must be identical and contain two distinct addresses, producing
exactly four unique profile/address keys. Each run independently contains the exact complete v1
acceptance inventory, every row must pass, and application build IDs/authenticated hashes remain
tied to profiles and compared across boards.

Production policy paths are fixed to the checked-in board manifest, result schema,
release-manifest schema, and capability matrix. The public CLI accepts only
`--release-manifest`, repeatable `--trusted-key`, and exactly two positional results. Tests mutate
policy only through the internal `_run_with_policy` seam; public callers cannot redirect policy
files.

Schema `const` and `enum` comparison is type-aware, and both integer-valued schema-version fields
declare explicit integer types. UTC timestamps are parsed as real calendar values using the exact
`YYYY-MM-DDTHH:MM:SSZ` format and round-tripped to reject invalid or noncanonical values.

The reviewed board manifest now contains disjoint supported, required-false, and optional
capability inventories. Their union must exactly equal the matrix with no missing or extra rows.
Supported rows must be true with contract evidence and cannot claim `Unsupported`; every
required-false and optional row must be false with stable exact `Unsupported` semantics. Both
signed result documents must report the complete inventory consistently.

### Fix Round 1 GREEN and regression evidence

The final focused command was:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-fix1-focused3-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

It exited 0 with 17/17 tests passing in 12.283 seconds. The suite includes the exact 2x2 run
matrix, immutable public CLI, boolean-as-integer, exact capability inventory/evidence, and real
calendar timestamp mutation controls, plus the original signature, identity, application,
failure, release-manifest, and two-board controls. Positive signatures remain explicitly
synthetic and test-only.

The exact Task 17 regression was rerun with the requested reviewed environment:

```text
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-task18-fix1-final-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-task18-fix1-ci-pycache tests/eos/run-ci.sh
```

It exited 0 with `EOS CI gates passed`: release identity and lock passed; compiler and library
tests passed 2/2 each; native ABI passed 45/45; libc/provenance passed 6/6 in 14.340 seconds;
bootstrap/PAL/unwind passed 30/30 in 26.071 seconds; the SDK contract passed 76/76 in 323.371
seconds; sysroot construction passed; 111 ARM layout facts matched; and the final static suite
passed 13/13 in 59.298 seconds. Evidence is under
`/tmp/eos-task18-fix1-final-artifacts`.

### Preserved release boundary

Read-only inspection again found both `release-results` and `release-bundle` absent. Therefore no
real checker or bundle-validation command was run and no board outcome was inferred. Authentic,
organization-signed results from both XC7Z030 and XC7Z045 remain the manual hardware release
blocker; the overall release is not complete.

## Fix Round 2 — superseding load-address canonicalization

This section supersedes the earlier load-address uniqueness statements where they differ. Fix
Round 1 compared address strings, while the result schema allowed both uppercase and lowercase
hexadecimal digits. Consequently two spellings of one numeric address could satisfy the
two-address check.

The separate Fix Round 2 commit is
`ebaca4ee9bd3255e5c13fa641167186cc7c9546f` with subject
`fix: canonicalize EOS board load addresses`; its parent is Fix Round 1 commit
`2ffb852f29cffc4f1a4fcf2d83a0aebfe0be2b22`.

The production-gate RED fixture used correctly signed synthetic XC7Z030 and XC7Z045 results. In
both debug and release profiles on both boards, the two runs used `0xabcdef00` and `0xABCDEF00`.
The checker exited 0, so this control failed as intended with `AssertionError: 0 != 2`:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-fix2-red-pycache \
python3 -m unittest -v \
  tests.eos.board.test_check_results.BoardResultGateTests.test_rejects_case_aliased_load_addresses
```

Fix Round 2 applies defense in depth. The immutable result schema now accepts only canonical
lowercase `0x` followed by exactly eight lowercase hexadecimal digits. The semantic checker also
normalizes every address with `int(address, 16)` before checking within-profile uniqueness and
the equality of the debug/release address sets. The focused control verifies both layers: the
production gate rejects the uppercase spelling at schema validation, while a test-only permissive
schema still reaches and is rejected by numeric semantic validation. The exact 2x2 run matrix and
all earlier identity, capability, application, result, and RSA signature policies are unchanged.

The final focused command passed 18/18 tests in 9.443 seconds:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-task18-fix2-focused-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

The exact full Task 17 regression was rerun with the same reviewed SDK, ARM GNU, and CMake paths,
using `/tmp/eos-task18-fix2-final-artifacts` and
`/tmp/eos-task18-fix2-ci-pycache`. It exited 0 with `EOS CI gates passed`: release identity and
lock passed; compiler and library tests passed 2/2 each; native ABI passed 45/45; libc/provenance
passed 6/6 in 11.883 seconds; bootstrap/PAL/unwind passed 30/30 in 19.261 seconds; the SDK contract
passed 76/76 in 247.184 seconds; sysroot construction passed; 111 ARM layout facts matched; and
the final static suite passed 13/13 in 49.849 seconds.

Read-only inspection still finds `release-results` and `release-bundle` absent. No real board
checker or bundle validation was run, no hardware evidence was created, and the overall release
remains incomplete pending authentic organization-signed XC7Z030 and XC7Z045 results.

## Final integration Task 1 — exact release-manifest snapshot binding

The board-result gate now reads a release manifest into one byte snapshot, parses that snapshot,
and hashes those same bytes. Its SHA-256 must equal the exact reviewed policy value
`fac60315f383ea6341e874a01d2b802e09419f1e5137e5b20129e3f9a160c4b5` before the checker derives
or accepts any release identity. Consequently correctly re-signed results cannot substitute a
different valid-looking manifest or choose their own release-manifest digest.

The signed identity no longer carries `sdk_package_sha256_tree_v1`; it carries the fixed
`release_manifest_sha256` instead. This does not weaken the Task 17 full SDK-tree pin, which is
outside this board-result identity change. RSA signature policy, target, ABI, linker,
authentication, capability, and manual-hardware boundaries are unchanged.

Two focused RED controls were observed before the change. A public checker invocation accepted
two correctly signed synthetic results after a distribution digest and both result digests were
changed, exiting `0` rather than rejecting with the exact reviewed release-manifest diagnostic.
An in-process control that replaced the manifest after its first read observed two reads rather
than one. After the snapshot and policy change, the required focused command passed all 20 tests:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task1-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

The synthetic fixture now mirrors the real six-entry archive-name shape. The tests remain
synthetic and test-only; no board action, signature, release result, or hardware claim was made.
