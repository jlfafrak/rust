# Task 6 Final-Review Fix Wave 1 Report — Deterministic SDK Package Modes

## Status, baseline, and scope

The source fix is committed as `ea284349ef0be4ea3ab04f94fa7037740d479722` with exact subject
`dist: canonicalize EOS SDK package modes`, based directly on required baseline
`37540827d90a14006f0ad81a912e750653e41b60` on branch `codex/eos-rust-target`.

The implementation commit changes exactly:

- `src/tools/eos-sdk/bin/build-eos-sdk`
- `src/tools/eos-sdk/bin/install-eos-sdk`
- `src/tools/eos-sdk/manifests/sdk-layout.toml`
- `src/tools/eos-sdk/tests/test_sdk_layout.py`
- `tests/eos/host/test_static_elves.py`

This evidence wave additionally amends `task-6-report.md` and creates this report. The progress
ledger was not edited. No release manifest, board policy, release result, source revision,
legacy SDK content identity, ARM GNU identity, or EOS input identity pin was changed.

## Root cause and retained rejected output

The builder used `shutil.copy2` and `shutil.copytree`, which preserve source and stage2 permission
bits. Generated paths inherited the process umask. The final staged validator checked only that
selected tools were executable; the installer likewise checked executability and accepted all
other modes. The legacy `sha256-tree-v1` identity intentionally hashes types, paths, contents,
and link targets but not permission modes. Task 6's Git-index mode check therefore did not prove
the modes of the published package.

A read-only scan of retained `/tmp/eos-final-release-sdk` reproduced the finding exactly:

```text
root 0777: 1
directories 0777: 10
directories 0700: 45
regular files 0777: 122
regular files 0600: 4
regular files 0644: 6
regular files 0755: 9
```

Thus the root plus ten descendant directories account for the 11 unsafe `0777` directory
objects. The retained tree still has 56 directories including its root and 141 regular files.

## Exact mode contract and implementation

`sdk-layout.toml` now locks this exact package-controlled representation, while builder,
installer, and static release gate each compare it against the same hard-coded reviewed value:

```text
algorithm             sha256-tree-mode-v1
root                  0755
every directory       0755
ordinary regular file 0644
executable file       0755
executable subtrees   bin/
                      arm-gnu/bin/
                      arm-gnu/arm-none-eabi/bin/
                      lib/rustlib/<host-triple>/bin/
executable file       arm-gnu/libexec/gcc/arm-none-eabi/14.3.1/collect2
```

Every regular descendant of an executable subtree is `0755`. Only the exact `collect2` path is
executable beneath `arm-gnu/libexec`; `liblto_plugin.so` is `0644`. Target/host shared libraries,
archives, manifests, metadata, headers, docs, examples, board sources/checker, and all other
regular files are `0644`. Exact equality rejects setuid, setgid, sticky, group-write,
other-write, missing execute, and excess execute bits.

The builder canonicalizes root, directories, and regular files only after all ARM, static,
native ABI, stage2 Rust, generated manifest, and generated board-policy staging has completed.
It then validates every staged mode before the existing atomic no-replace publication. The
installer validates the locked policy and every mode before its initial package fingerprint and
before rustup discovery/action. Its revalidation fingerprint is now explicitly named
`sha256_tree_mode_v1` and continues to close the validation/action window.

`sha256-tree-mode-v1` is SHA-256 over these concatenated byte records, with entries sorted by
UTF-8 POSIX relative path and modes rendered as lowercase octal without a prefix:

```text
root:      R NUL mode NUL
directory: D NUL relative NUL mode NUL
file:      F NUL relative NUL mode NUL content-bytes NUL
link:      L NUL relative NUL mode NUL link-target NUL
```

The root mode and each entry's type, relative path, permission mode, and file bytes or link target
are therefore bound. The legacy `sha256-tree-v1` implementation and release-manifest input
algorithm remain byte-for-byte semantically unchanged for supplied ARM GNU and EOS roots.

The static release gate retains the reviewed legacy SDK content pin and introduces a distinct
`REVIEWED_SDK_TREE_MODE_SHA256`. Its value is deliberately `None`, so the gate fails closed until
the controller builds and reviews the authorized replacement and records its real mode-aware
identity. No replacement identity was guessed or derived from the rejected SDK.

## RED/GREEN and diagnostic evidence

The first test-only patch ran:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 src/tools/eos-sdk/tests/test_sdk_layout.py \
  SdkSourceContractTests.test_layout_manifest_and_hello_example_cover_release_artifacts \
  InstallerContractTests.test_installer_rejects_noncanonical_package_modes_before_fake_rustup \
  InstallerContractTests.test_installer_rechecks_chmod_in_package_fingerprint_before_fake_rustup \
  BuilderContractTests.test_builder_canonicalizes_hostile_sources_stage2_outputs_and_umask \
  BuilderContractTests.test_builder_staged_validation_rejects_noncanonical_package_modes
```

RED exited 1 after 23.052 seconds: 5 test methods ran with 15 failing subtests and one error.
The layout lacked `package_modes`; a fake builder using hostile `0777` sources/stage2 output and
umask `000` published root `0777`; builder and installer accepted wrong root, directory,
ordinary-file, special-bit, and writable modes. Their old executable-only checks fired for the
non-executable tool but did not report the package-mode contract. The pre-existing fingerprint
already caught the added chmod-after-validation test, so that method passed during RED.

After the focused implementation, the identical command exited 0: 5/5 passed in 27.473 seconds.
Two builder/installer weakened, expanded, and malformed policy behavior tests then passed 2/2 in
10.743 seconds.

The static test-only patch ran:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/eos/host/test_static_elves.py \
  ReleaseIdentityGateTests.test_mode_aware_sdk_tree_identity_binds_root_entry_modes_bytes_and_links \
  ReleaseIdentityGateTests.test_static_sdk_mode_gate_accepts_only_the_exact_executable_allowlist
```

RED exited 1 with 2/2 errors because `sha256_tree_mode_v1` and
`validate_sdk_package_modes` did not exist. GREEN exited 0 with 2/2 passing in 0.002 seconds; the
fresh final rerun also exited 0 with 2/2 in 0.003 seconds. The hand-derived serialization fixture
has exact `sha256-tree-mode-v1`
`623304f4622194acdf7779eb2485cdfb3502765dca5b4ea6fc354d3e41b2aedf` and proves root-mode,
file-mode/content, and link-target sensitivity.

The first complete layout-suite run exposed a test-fixture portability issue rather than a
production defect: Python selected
`/mnt/c/Users/jfafrak/AppData/Local/Temp`, where DrvFS continued to report fixture roots as `0777`
after chmod. It exited 1 with 12 failures across 41 methods in 330.504 seconds; every diagnostic
was the expected new root-mode rejection. Moving all installer package fixtures to native `/tmp`
made `InstallerContractTests` pass 14/14 in 3.079 seconds, and the unchanged full layout suite
then passed 41/41 in 360.699 seconds.

An independent temporary-tree mutation produced:

```text
policy implementations: sdk-layout + builder + installer + static = 4 exact matches
sha256-tree-v1 before  f911348dfe3928b200098be575cb3521d20b0505fa480fc6985408c22f286046
sha256-tree-v1 after   f911348dfe3928b200098be575cb3521d20b0505fa480fc6985408c22f286046
mode-v1 before         1dfa78d3016b23501a1725b5d014ebee8b9d8984f70418ac45cca63515701ea1
mode-v1 after          a764908e0e8b4869bb2f2478eec6227299af324c6ca178b632fd491c75ccd68e
```

Only chmod changed: the legacy identity remained stable and the mode-aware identity changed.

## Full non-replacement regressions and strict checks

All commands below exited 0 unless an expected rejection is stated:

```text
python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_*.py' -v
  85/85 PASS in 313.756s

python3 -m unittest discover -s tests/eos/board -p 'test_*.py' -v
  21/21 PASS in 8.907s

focused static mode/hash tests
  2/2 PASS in 0.003s

python3 -Wall -Werror -m py_compile
  15 EOS Python files PASS; bytecode redirected to
  /tmp/eos-final-review-fix1-pycache

strict JSON/TOML parsing
  7 JSON and 16 TOML documents PASS

bash -n tests/eos/run-ci.sh
  PASS

git diff --check
  PASS before the implementation commit

Git-index modes
  build-eos-sdk, install-eos-sdk, run-ci.sh 100755
  sdk-layout.toml, test_sdk_layout.py, test_static_elves.py 100644

backtrace gitlink
  02ef1b533157e8ddbd0f9295c867e79b59e9bbbd
backtrace checkout
  02ef1b533157e8ddbd0f9295c867e79b59e9bbbd
backtrace porcelain
  clean
```

The full discovery preserves authentication trailer behavior, ELF/ABI validation, linker and
wrapper policy, exact layout/inventory, five EOS libraries, the 23-file board bundle, the
two-file example, source bytes, input-mutation detection, private build directories, atomic
no-replace publication, installer root identity and TOCTOU behavior, and all prior negative
controls. No full static artifact build or integrated CI was run because the only available SDK
is the deliberately rejected package and its replacement pin is pending.

## No-build proof and pending replacement boundary

After all tests and checks, read-only hashing reconfirmed retained
`/tmp/eos-final-release-sdk` at exact `sha256-tree-v1`
`919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7`, with the same exact mode
counts recorded above. `/tmp/eos-final-release-sdk-r2` remained absent and no real
`build-eos-sdk` process existed. Running the fixed source installer read-only against the retained
package exited 2 before rustup with `SDK layout does not match the exact Task 16 contract`.

The required fake-builder tests execute the builder only against synthetic inputs and fake
`git`/CMake/CTest/bootstrap commands, publish only into automatically cleaned private `/tmp`
fixtures, and do not build Rust, native ABI artifacts, or either release SDK. No physical
hardware action or fabricated hardware/release result occurred.

Immediately before documentation edits, full outer
`git status --porcelain=v2 --untracked-files=all` exited 0 with no output at implementation commit
`ea284349ef0be4ea3ab04f94fa7037740d479722`.

The remaining controller boundary is strict: obtain independent approval of this clean source
fix, invoke the real builder exactly once to fresh `/tmp/eos-final-release-sdk-r2`, preserve the
rejected SDK and all one-shot evidence, verify the replacement's exact modes and both tree
identities, update only the authorized replacement release/content/mode pins, then rerun the
replacement full CI, isolated Cargo smoke, strict static checks, and whole-branch review. This
report does not claim repository readiness or hardware release completion.
