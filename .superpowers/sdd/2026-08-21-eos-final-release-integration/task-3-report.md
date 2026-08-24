# Task 3 report: package and validate the manual board-test bundle

## Status and scope

Implemented from baseline `b728fd185c0db74c3640368a3c8cc2a9ea76d8cd` for the sole target
`armv7a-unknown-eos-eabi`. The change packages the reviewed manual evidence inputs, generates a
release-specific board policy from the exact emitted release-manifest bytes, and makes the SDK
layout and installer enforce the same hard-coded inventory. It does not alter the target/ABI,
linker or validator policy, authentication or unwind behavior, the backtrace gitlink, Task 4 CI,
or any capability claim. No real SDK builder, physical board action, transfer, or deployment was
launched.

The parent-authorized scope correction adds `tests/eos/board/check_results.py`: the approved flat
bundle required its immutable defaults to work both in the repository and from
`<sdk>/share/board-test`. In packaged layout it resolves the adjacent board policy/result schema
and `<sdk>/manifests/{capabilities.toml,release-manifest.schema.json}`; source CLI arguments and
policy immutability remain unchanged.

## RED evidence

All production edits followed observable controls:

- Inventory RED: the extended fake builder success test seeded `Cargo.lock`, `target/`, an
  unrelated app, private-key/result/credential/location/transfer files, and an unreviewed
  manifest. The pre-change builder failed because recursive manifest copying admitted
  `unreviewed-policy.toml` (`1` test, `59.759s`).
- Generated-policy RED: a source board policy with every release-derived `[expected]` field
  poisoned failed the explicit packaged-policy existence assertion because the old builder did
  not create `share/board-test/board-test-manifest.toml` (`1` test, `54.690s`).
- Installer closure RED: an extra `share/board-test/results/xc7z030.json` was accepted by the old
  installer (`0 != 2`, `1` test, `2.046s`).
- Packaged-default RED: loading the flat packaged checker showed capability/release schemas
  resolving outside the SDK (`1` failure, `8.851s`).
- Determinism RED: release manifests from two independent fake roots differed because fake GCC
  embedded a temporary absolute libgcc path (`1` failure, `38.395s`).

## Implementation and exact inventory

`stage_static_assets` now uses explicit single-file copies for the five SDK manifests, two
templates, three manual documents, checker/result schema, real `ffi_caller.c`, and every reviewed
application manifest/source. The existing hello example remains exactly `Cargo.toml` and
`src/main.rs`; only the five reviewed MARTOS runtime libraries are packaged and proprietary EOS
headers/source are still excluded.

The flat board bundle is an exact 23-file closure:

- `board-test-manifest.toml`, `check_results.py`, and `result.schema.json`;
- `docs/{capabilities.md,manual-board-test.md,releasing.md}`;
- `apps/{hello-std,filesystem,threads-tls,network,process,ffi-abi,unwind}` with `Cargo.toml` and
  `src/main.rs`;
- `apps/ffi-containment/Cargo.toml` and `apps/ffi-containment/src/lib.rs`;
- `abi/ffi_caller.c`.

SDK-level `manifests/capabilities.toml` and `manifests/release-manifest.schema.json` are included.
The layout and installer lists are literal-equal at 48 required paths. The installer computes the
exact board closure including directories, recursively contains all links/entries, rejects
unsupported filesystem types, and retains whole-package fingerprint/root-identity revalidation
immediately before rustup or no-rustup output. The mutation test changes the packaged checker to
prove board-bundle mutations remain inside the TOCTOU bracket. Git modes remain `100755` for the
builder/installer and `100644` for the checker/docs; copy2 preserves source modes, and only `bin/`
Python tools are required executable by layout validation.

## Generated-policy evidence

`write_manifests(...) -> tuple[dict[str, Any], bytes]` encodes once, writes those bytes, parses
those same bytes, and returns both. `render_board_policy(source_policy, release, release_bytes)`
preserves the reviewed semantic sets and signature policy, but derives the release digest,
release/toolchain/target/layout identity, four source revisions, two complete-input hashes, ARM
GNU release, EOS baseline, ABI version, and linker-script hash from that snapshot and reviewed
source inputs. Stale source `[expected]` values cannot flow into the package.

A disposable deterministic fake build recorded:

```text
release_manifest_sha256=3beb847eccf66a7ff38c2759c30365fcc556f35ecb2c82db438e2ac00bd67a32
policy_release_manifest_sha256=3beb847eccf66a7ff38c2759c30365fcc556f35ecb2c82db438e2ac00bd67a32
digest_match=True
rust_fork=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
arm_input=e8becd5a20a87f528e70ba60f483224a7a11209525ef23feafc179e841eddb9b
eos_input=e5f8b43c51985a67207684c1857c114b7c041f40a1fe2cd054a2e0b2c582aa52
linker_script_sha256=835c2afac09937fb1b6f7dc93027134c61b34bf645eb2809bbaf1ece4994a674
bundle_file_count=23
```

The independent two-root determinism control is GREEN (`1/1`, `81.172s`). These are synthetic
fixture identities only, not release or hardware evidence.

## GREEN and validation evidence

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task3-pycache-final \
  python3 -m unittest -v src.tools.eos-sdk.tests.test_sdk_layout
Ran 35 tests in 297.266s ... OK

PYTHONPYCACHEPREFIX=/tmp/eos-final-task3-discovery-final-pycache \
  python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_*.py' -v
Ran 79 tests in 323.263s ... OK

PYTHONPYCACHEPREFIX=/tmp/eos-final-task3-board-suite-pycache \
  python3 -m unittest -v tests.eos.board.test_check_results
Ran 21 tests in 22.172s ... OK

PYTHONPYCACHEPREFIX=/tmp/eos-final-task3-strict-pycache \
  python3 -Wall -Werror -m py_compile \
  src/tools/eos-sdk/bin/build-eos-sdk \
  src/tools/eos-sdk/bin/install-eos-sdk \
  src/tools/eos-sdk/tests/test_sdk_layout.py \
  tests/eos/board/check_results.py
strict Python/JSON/TOML checks: OK

git diff --check -- src/tools/eos-sdk docs/eos tests/eos/board/check_results.py \
  .superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-16-report.md
exit 0
```

One earlier 34-test focused run had a single existing installer subprocess exceed its 20-second
timeout under sustained suite load. Immediate isolated reproduction passed in `3.332s`; the two
fresh final full runs above then passed without timeout. No timeout or production behavior was
changed.

## Commits and concerns

The production/tests/docs/Task 16 evidence commit is
`a92d8113a9d02df3717df917411ae93d3df89e15` with the required subject
`dist: package EOS manual board-test bundle`. This durable SDD report is committed separately so
it records that implementation commit ID.

Remaining environmental boundary: this task packages only source, policy, schemas, and manual
instructions. Authentic XC7Z030/XC7Z045 execution, results, organization public keys/signatures,
locations, credentials, and transfer/deployment procedures remain external manual release gates.
