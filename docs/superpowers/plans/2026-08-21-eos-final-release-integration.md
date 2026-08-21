# EOS Final Release Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce one final reviewed EOS Rust SDK that packages the complete manual board-test bundle, binds signed results to its exact release manifest, and contains applications capable of generating every mandatory two-board acceptance row.

**Architecture:** Task 17 remains the trust anchor for the complete SDK and ARM GNU tree hashes. Task 18 binds signed board results to the exact generated `release-manifest.toml` byte digest, avoiding an SDK self-hash cycle. The SDK builder generates the packaged board policy after it writes the release manifest, while source pins are updated once after the single authorized final build.

**Tech Stack:** Python 3 standard library, TOML/JSON, Rust 1.97.1, Cargo, C11, ARM GNU 14.3.Rel1, CMake/CTest, Bash, `readelf`/`objdump`.

**Spec:** `docs/superpowers/specs/2026-08-21-eos-final-release-integration-design.md`

## Global Constraints

- The built-in target remains exactly `armv7a-unknown-eos-eabi`; no hard-float sibling is added.
- Do not change the public native ABI, exact `eos_rust_*` export set, linker relocation policy, authentication trailer, unwind boundary, random policy, or capability claims.
- Package only the five reviewed MARTOS runtime libraries and no proprietary EOS headers.
- Store no board credentials, locations, transfer commands, signing secrets, fabricated results, or deployment automation.
- Use native `apply_patch` for every source edit.
- Every task starts with an independently observed RED and ends with focused GREEN, regression evidence, a separate commit, and read-only review.
- Run exactly one additional full SDK builder invocation from a clean committed prebuild head. If it fails, stop; do not relaunch without new user authorization.
- Physical XC7Z030/XC7Z045 execution remains manual. The overall release remains incomplete until genuine signed results and a release bundle exist.

---

## File map

- `tests/eos/board/check_results.py`: one-snapshot manifest parsing, exact digest binding, signed-result semantics.
- `tests/eos/board/board-test-manifest.toml`: fixed semantic sets and reviewed release-manifest identity.
- `tests/eos/board/result.schema.json`: signed result wire schema without a self-referential SDK-tree field.
- `tests/eos/board/test_check_results.py`: cryptographic and policy mutation controls.
- `tests/eos/apps/{process,network,unwind}`: truthful self-contained board behavior.
- `tests/eos/apps/ffi-containment` and `tests/eos/abi/ffi_caller.c`: real C-to-Rust containment artifact.
- `tests/eos/host/test_static_elves.py`: full board application build/validation/authentication evidence and exact SDK pin.
- `src/tools/eos-sdk/bin/build-eos-sdk`: exact-file board bundle staging and release-specific packaged policy generation.
- `src/tools/eos-sdk/bin/install-eos-sdk`: hard-coded final SDK inventory.
- `src/tools/eos-sdk/manifests/sdk-layout.toml`: package layout contract.
- `src/tools/eos-sdk/tests/test_sdk_layout.py`: poison, omission, symlink, manifest-generation, and inventory controls.
- `tests/eos/run-ci.sh`: complete automated gate including Task 18 checker tests.
- `docs/eos/*.md`: final package/manual release commands and evidence boundaries.

---

### Task 1: Bind Results to One Exact Release-Manifest Snapshot

**Files:**
- Modify: `tests/eos/board/check_results.py`
- Modify: `tests/eos/board/board-test-manifest.toml`
- Modify: `tests/eos/board/result.schema.json`
- Modify: `tests/eos/board/test_check_results.py`
- Modify: `docs/eos/manual-board-test.md`
- Modify: `docs/eos/releasing.md`
- Modify: `.superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-18-report.md`

**Interfaces:**
- Produces: `load_toml_snapshot(path: Path, description: str) -> tuple[dict[str, Any], bytes]`.
- Produces: `validate_release_manifest(...) -> tuple[dict[str, Any], str]`, where the digest is computed from the parsed byte snapshot and must equal `expected["release_manifest_sha256"]`.
- Removes: `sdk_package_sha256_tree_v1` from the signed board-result identity.

- [ ] **Step 1: Add the manifest-substitution RED**

Extend the synthetic release fixture with the real six-entry archive-name shape. Sign two valid
results, then alter one distribution digest in the release manifest, update both signed result
digests to match that altered file, and invoke the public checker. Assert exit status 2 and an
`exact reviewed release-manifest digest` diagnostic. The current checker returns 0.

- [ ] **Step 2: Add the same-path snapshot RED**

Patch the manifest reader in-process so the first read returns valid TOML bytes and replaces the
path before a second read. Assert the reader is called once and the digest matches the parsed
bytes. The current separate parse/hash reads fail this control.

- [ ] **Step 3: Make the result identity non-self-referential**

Remove `sdk_package_sha256_tree_v1` from `board-test-manifest.toml`, `result.schema.json`, fixture
builders, and docs. Add an exact 64-hex `release_manifest_sha256` member to `[expected]`. Retain
the Task 17 full SDK-tree pin; do not weaken `verify_release_identity()`.

- [ ] **Step 4: Implement one-snapshot parsing and exact comparison**

Implement the reader as:

```python
def load_toml_snapshot(path: Path, description: str) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    value = tomllib.loads(data.decode("utf-8"))
    if not isinstance(value, dict):
        raise GateError(f"{description} root must be an object")
    return value, data
```

`validate_release_manifest()` must validate `value`, compute `sha256(data)`, compare it with the
policy constant, then compare every derived identity. No second path read is permitted.

- [ ] **Step 5: Run focused GREEN**

Run:

```bash
PYTHONPYCACHEPREFIX=/tmp/eos-final-task1-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

Expected: all existing and new manifest-substitution/snapshot controls pass.

- [ ] **Step 6: Commit Task 1**

```bash
git diff --check
git add tests/eos/board docs/eos/manual-board-test.md docs/eos/releasing.md
git add -u .superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-18-report.md
git commit -m "fix: bind EOS board results to release bytes"
```

---

### Task 2: Make Every Board Application Produce Honest Evidence

**Files:**
- Modify: `tests/eos/apps/process/src/main.rs`
- Modify: `tests/eos/apps/network/src/main.rs`
- Modify: `tests/eos/apps/unwind/src/main.rs`
- Modify: `tests/eos/host/test_static_elves.py`
- Modify: `tests/eos/board/board-test-manifest.toml`
- Modify: `tests/eos/board/test_check_results.py`
- Test/retain: `tests/eos/apps/ffi-containment/Cargo.toml`
- Test/retain: `tests/eos/apps/ffi-containment/src/lib.rs`
- Test/retain: `tests/eos/abi/ffi_caller.c`
- Modify: `.superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-17-report.md`

**Interfaces:**
- Produces: final debug/release artifacts for every `board-test-manifest.toml` application.
- Produces: `build_ffi_containment_application(...) -> tuple[Path, Path, str]` in the static gate.
- Preserves: the exact base/authenticated ELF and 40-hex build-ID retention contract.

- [ ] **Step 1: Add application-realism RED controls**

Add focused tests that reject the current sources when they contain the nonexistent
`/eos/rust-process-child`, per-child `.env()`/`.current_dir()`, fallible DNS `?` before numeric
transport, no `Backtrace::force_capture()`, or a board manifest that omits `ffi-containment`.
Also require the static artifact application set to equal the board policy set.

- [ ] **Step 2: Implement the self-spawning process probe**

Use `std::env::current_exe()` and a `--child` mode. The child reads three bytes from stdin,
writes them to stdout, and exits. The parent records its own args/environment/current directory,
spawns itself without per-child env/cwd overrides, exercises piped stdin/stdout, `try_wait`,
conditional `kill`, and repeated `wait`, then prints stable evidence lines.

- [ ] **Step 3: Implement numeric loopback TCP and UDP**

Bind a numeric IPv4 loopback listener, spawn a local server thread, connect a client, and exchange
fixed bytes. Bind two UDP loopback sockets and exchange/verify fixed datagrams. Probe
`("localhost", 7).to_socket_addrs()` separately and accept only a stable `Unsupported` error on
EOS; do not let it skip numeric TCP/UDP work.

- [ ] **Step 4: Add backtrace evidence**

Call `Backtrace::force_capture()`, format it, require captured content, and print it before the
existing catch/drop probe. Preserve `deny(ffi_unwind_calls)` and the TLS-cleanup abort mode.

- [ ] **Step 5: Build the real C-to-Rust containment executable**

In `test_static_elves.py`, build the Rust `ffi-containment` static library with the reviewed SDK,
compile `ffi_caller.c` using the reviewed full ARM GCC and exact softfp flags, and final-link them
through `eos-rust-link` with `--build-id=sha1`. Feed the resulting ELF through the same validator,
symbol-table, build-ID, authentication, trailer, and retention helper as Cargo applications.

- [ ] **Step 6: Expand exact artifact coverage**

Add `unwind` and `ffi-containment` to the board application policy and require debug/release
triplets for the complete set. Keep `ffi-abi` as separate mixed-ABI evidence.

- [ ] **Step 7: Run focused GREEN**

```bash
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-task2-artifacts \
PYTHONPYCACHEPREFIX=/tmp/eos-final-task2-pycache \
python3 -m unittest -v tests.eos.host.test_static_elves tests.eos.board.test_check_results
```

Expected: complete artifact and board-policy suites pass; every retained application has base,
authenticated, and build-ID evidence.

- [ ] **Step 8: Commit Task 2**

```bash
git diff --check
git add tests/eos/apps tests/eos/abi tests/eos/host/test_static_elves.py tests/eos/board
git commit -m "test: make EOS board probes release-complete"
```

---

### Task 3: Package and Validate the Manual Board-Test Bundle

**Files:**
- Modify: `src/tools/eos-sdk/bin/build-eos-sdk`
- Modify: `src/tools/eos-sdk/bin/install-eos-sdk`
- Modify: `src/tools/eos-sdk/manifests/sdk-layout.toml`
- Modify: `src/tools/eos-sdk/tests/test_sdk_layout.py`
- Modify: `docs/eos/capabilities.md`
- Modify: `docs/eos/manual-board-test.md`
- Modify: `docs/eos/releasing.md`
- Modify: `.superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-16-report.md`

**Interfaces:**
- Produces: `render_board_policy(source_policy: Path, release: dict[str, Any], release_bytes: bytes) -> str`.
- Produces: `write_manifests(...) -> tuple[dict[str, Any], bytes]` so policy generation uses the exact written release bytes.
- Produces: exact `share/board-test/` package inventory required by layout and installer.

- [ ] **Step 1: Add SDK inventory RED controls**

Extend the fake builder success test to require capability/release schemas, checker/result schema,
generated board policy, three manual docs, the exact application sources, and C caller. Seed
neighboring poison files (`Cargo.lock`, `target/`, keys, results, unrelated apps) and require they
are absent. Existing builder output must fail these assertions.

- [ ] **Step 2: Add generated-policy RED controls**

Make the fake dist release manifest deterministic. Require the packaged board policy's
`release_manifest_sha256`, source revisions, input hashes, release/toolchain identity, ABI, and
linker-script hash to match that exact generated manifest and source. Mutate the source policy
release values and prove the builder regenerates rather than copies stale values.

- [ ] **Step 3: Stage exact bundle files**

Add explicit `copy_source_file()` calls for checker, schemas, docs, each application manifest and
source, and `ffi_caller.c`. Do not use recursive application copies. Preserve executable mode for
packaged Python tools only where the layout requires it.

- [ ] **Step 4: Generate the release-specific board policy**

Have `write_manifests()` create release bytes once, write them, parse the same bytes, and return
both. `render_board_policy()` retains fixed semantic sets/signature policy but replaces the
`[expected]` release values and `release_manifest_sha256` from those bytes. Write the result to
`share/board-test/board-test-manifest.toml` before staged validation.

- [ ] **Step 5: Harden layout and installer inventory**

Add every new manifest and board-bundle path to `sdk-layout.toml` and the installer's hard-coded
`REQUIRED_PATHS`. Update fingerprint/inventory tests so omission, extra poison, broken link,
escape, unsupported entry type, and mutation-before-rustup remain fail-closed.

- [ ] **Step 6: Run focused GREEN**

```bash
PYTHONPYCACHEPREFIX=/tmp/eos-final-task3-pycache \
python3 -m unittest -v src.tools.eos-sdk.tests.test_sdk_layout
```

Expected: all SDK source/builder/installer tests pass with exact bundle inventory and generated
policy evidence.

- [ ] **Step 7: Commit Task 3**

```bash
git diff --check
git add src/tools/eos-sdk docs/eos
git commit -m "dist: package EOS manual board-test bundle"
```

---

### Task 4: Put Task 18 and the Full Bundle Under CI

**Files:**
- Modify: `tests/eos/run-ci.sh`
- Modify: `tests/eos/host/test_static_elves.py`
- Modify: `tests/eos/board/test_check_results.py`
- Modify: `.superpowers/sdd/2026-08-11-eos-rust-target-implementation/task-17-report.md`

**Interfaces:**
- Produces: one CI entrypoint covering Task 18 policy plus all board artifacts.
- Preserves: workflow's one-command/no-board-action shape.

- [ ] **Step 1: Add CI omission RED**

Extend `CiEntryPointPolicyTests` to parse `run-ci.sh` and require an invocation of
`tests.eos.board.test_check_results` through `"$TRUSTED_PYTHON"`. Add a mutation control that
removes that line and must fail. Current CI must be RED.

- [ ] **Step 2: Add the board suite to `run-ci.sh`**

Invoke:

```bash
"$TRUSTED_PYTHON" -m unittest -v tests.eos.board.test_check_results
```

after the reviewed identity preflight and before final static artifact generation. Do not add a
second workflow command or any board action.

- [ ] **Step 3: Run focused CI-policy GREEN**

```bash
PYTHONPYCACHEPREFIX=/tmp/eos-final-task4-pycache \
python3 -m unittest -v \
  tests.eos.board.test_check_results \
  tests.eos.host.test_static_elves.CiEntryPointPolicyTests
bash -n tests/eos/run-ci.sh
```

- [ ] **Step 4: Run the full prebuild regression with the old reviewed SDK**

```bash
EOS_RUST_SDK_ROOT=/tmp/eos-task16-fix1-replacement-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-prebuild-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-final-prebuild-pycache tests/eos/run-ci.sh
```

Expected: all layers pass. This does not validate the new package inventory; Task 5 does.

- [ ] **Step 5: Commit Task 4 and freeze the prebuild head**

```bash
git diff --check
git add tests/eos/run-ci.sh tests/eos/host/test_static_elves.py tests/eos/board
git commit -m "ci: gate EOS manual release policy"
git status --porcelain=v2 --untracked-files=all
```

Expected: clean worktree. Record this exact commit as `PREBUILD_HEAD` in the implementation report.

---

### Task 5: Run the Single Authorized Final SDK Build

**Files:**
- Create externally: `/tmp/eos-final-release-sdk` (not committed)
- Modify after build: `tests/eos/host/test_static_elves.py`
- Modify after build: `tests/eos/board/board-test-manifest.toml`
- Modify after build: Task 16/17/18 implementation reports

**Interfaces:**
- Consumes: exact clean `PREBUILD_HEAD` from Task 4.
- Produces: final SDK tree digest, release-manifest digest, generated source revision, and final package inventory.

- [ ] **Step 1: Prove the one-build preconditions**

Require clean porcelain, exact backtrace gitlink/checkout, no output collision at
`/tmp/eos-final-release-sdk`, exact CMake/CTest 3.31.10, ARM GCC 14.3.1, binutils 2.44, EOS
`Release-Notes.md` build 14.0.39, and no live/orphan builder directories. Stop on any failure.

- [ ] **Step 2: Invoke the builder exactly once**

```bash
PATH=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin:$PATH \
PYTHONPYCACHEPREFIX=/tmp/eos-final-builder-pycache \
src/tools/eos-sdk/bin/build-eos-sdk \
  --source-root "$PWD" \
  --arm-gnu-root /home/dev/code/arm-toolchain-build/custom-arm-libs \
  --eos-sdk-root /home/dev/code/gpt-test/lib/martos-smp-14.0.39 \
  --output /tmp/eos-final-release-sdk
```

If this invocation exits nonzero, preserve its log/evidence and stop. Do not invoke it again.

- [ ] **Step 3: Deep-inspect the final SDK**

Verify exact required inventory, zero symlinks/escapes, exact five EOS libraries, no proprietary
headers, exact board bundle sources, no Cargo locks/targets/keys/results, prebuilt sysroot, tool
versions, source revisions, input-tree hashes, six distribution hashes, generated board policy,
and exact release-manifest digest. Recompute all hashes independently.

- [ ] **Step 4: Update repository pins only**

With `apply_patch`, update:

- `REVIEWED_SDK_TREE_SHA256` and `REVIEWED_SOURCE_REVISIONS["rust_fork"]` in
  `tests/eos/host/test_static_elves.py`;
- `release_manifest_sha256`, `rust_fork`, and other generated release values in
  `tests/eos/board/board-test-manifest.toml`; and
- the Task 16/17/18 reports with exact one-build evidence.

Do not change production behavior and do not rebuild.

- [ ] **Step 5: Commit final pins**

```bash
git diff --check
git add tests/eos/host/test_static_elves.py tests/eos/board/board-test-manifest.toml
git add -u .superpowers/sdd/2026-08-11-eos-rust-target-implementation
git commit -m "build: pin final EOS release SDK"
```

---

### Task 6: Verify the Final SDK and Branch

**Files:**
- No production edits expected.
- Modify only implementation reports/ledger after independent approval.

**Interfaces:**
- Consumes: final SDK at `/tmp/eos-final-release-sdk` and final source pins.
- Produces: complete automated implementation evidence and explicit hardware blocker.

- [ ] **Step 1: Run exact full CI against the new SDK**

```bash
EOS_RUST_SDK_ROOT=/tmp/eos-final-release-sdk \
EOS_ARM_GNU_CC=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-gcc \
EOS_ARM_GNU_OBJDUMP=/home/dev/code/arm-toolchain-build/custom-arm-libs/bin/arm-none-eabi-objdump \
EOS_CMAKE_BIN_DIR=/tmp/eos-task16-fix1-runtime/cmake-3.31.10-linux-x86_64/bin \
EOS_CI_ARTIFACT_DIR=/tmp/eos-final-release-artifacts EOS_CI_JOBS=2 \
PYTHONPYCACHEPREFIX=/tmp/eos-final-release-pycache tests/eos/run-ci.sh
```

Expected: identity, target, ABI, libc, policy, SDK, sysroot, 111 layouts, full board app/static
artifacts, and Task 18 board checker tests all pass.

- [ ] **Step 2: Run isolated named-toolchain smoke**

Link `/tmp/eos-final-release-sdk` as `eos-1.97.1` under isolated `RUSTUP_HOME`/`CARGO_HOME`, build
the packaged `hello-std` with ordinary Cargo and no target JSON/`-Z build-std`, validate the base
ELF, append the exact 129-byte authentication trailer, validate with `--allow-auth-trailer`, and
require default validation to reject the trailer with status 2. Remove generated Cargo output.

- [ ] **Step 3: Run final static and clean checks**

Run strict Python compile, JSON/TOML parsing, shell syntax, `git diff --check`, exact file modes,
one EOS target-list entry, exact backtrace checkout/gitlink, complete export manifest, deep SDK
tree hash, and empty porcelain.

- [ ] **Step 4: Confirm the honest hardware boundary**

Check for authentic `release-results/xc7z030.json`, `release-results/xc7z045.json`, organization
public key, and `release-bundle`. If absent, record the exact blocker and do not invoke the real
checker/bundle validation with synthetic substitutes.

- [ ] **Step 5: Generate and dispatch final review**

Generate an exact review package from the pre-integration base to final head. Dispatch a fresh
read-only reviewer over design, plan, reports, package, SDK inventory, final CI evidence, and the
manual hardware boundary. Fix/re-review until no Critical/Important/Minor implementation defect
remains.

- [ ] **Step 6: Record completion after approval**

Only after independent approval, update the progress ledger and durable report. State
`repository implementation ready; hardware release pending signed XC7Z030/XC7Z045 evidence`.
Do not state that the overall release is complete.
