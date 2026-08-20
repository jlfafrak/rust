# Task 16 Fix Round 1 Report

## Status and scope

Fix Round 1 addresses every finding from the review of
`166453dafed10c607052639cb74f70959f65f72e..fbd7cf8017adcbb2a99124a3f0ed26bb253e1904`.
The production delta is confined to the Task 16 builder, installer, and layout manifest. The
Task 16 test harness adds the reviewed poison, mutation, collision, provenance, manifest, and
TOCTOU controls. The exact two-file `hello-std` example is unchanged. No ABI, wrapper, target,
relocation, unwind, capability, Task 17+, or progress-ledger file is changed.

The recovery clone started clean at outer commit
`fbd7cf8017adcbb2a99124a3f0ed26bb253e1904`. The nested backtrace gitlink and checkout are
exactly `02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`.

## Reviewed findings and fixes

1. The builder no longer copies the proprietary EOS tree. The package closure beneath `eos/`
   is exactly:

   - `martos/lib/libmartos_app.so`
   - `martos/lib/libmartos_app.so.1.0`
   - `martos/lib/libmartos_c++.a`
   - `martos/lib/libmartos_c++abi.a`
   - `martos/lib/libmartos_c.a`

   MARTOS/libc headers remain external `EOS_SDK_ROOT` build inputs. Poisoned headers and source
   are absent from the package.

2. `sha256-tree-v1` is computed over each complete, validated supplied ARM GNU and EOS root
   before staging. Those full-root digests are recorded in the release manifest and recomputed
   immediately before publication. Non-staged file changes alter the digest; a mid-build input
   change rejects publication.

3. The builder requires external `Release-Notes.md` to contain the exact line
   `## Build 14.0.39`. Missing and wrong-build controls reject before external builds.

4. The builder requires a clean outer Rust tree, a clean backtrace tree, the exact outer
   backtrace gitlink, and the exact checked-out backtrace commit. Vendored libc is checked against
   the existing 508-file offline provenance fixture, its locked commit/tree metadata, exact
   upstream content, and the four allowed EOS content hashes.

5. The installer hard-codes the exact Task 16 SDK table, host tools, target libraries, and
   required paths, then requires `sdk-layout.toml` to equal that contract. It recursively
   rejects broken/escaping symlinks and non-file/non-directory entries, applies the ARM/EOS
   subtree boundaries, and requires the exact five-file EOS runtime closure. The release manifest
   must have the exact four-section schema, exact release/input/revision field names, lowercase
   string digests of the required lengths, safe distribution archive basenames, and required
   rustc/Cargo/EOS rust-std archive coverage. Weakened, expanded, escaping, unsupported, and
   corrupted controls reject.

6. Initial installer validation captures the SDK root device/inode and a deterministic
   full-package path/type/mode/content/symlink fingerprint. After `rustup` discovery it rechecks
   identity and fingerprint immediately before either the exact
   `rustup toolchain link eos-1.97.1 <sdk>` call or no-rustup output. Package mutation and
   same-path root replacement both fail before the mocked external command. An irreducible
   same-user micro-race remains in the few instructions between the final recheck and
   exec/printing; the installer documents this boundary and does not claim stronger exclusion.

7. Linux publication uses `renameat2(..., RENAME_NOREPLACE)`. Missing or unsupported no-replace
   support fails closed. Late regular-file, symlink, and empty-directory collisions all preserve
   the destination and reject as controlled builder errors. A direct run on the Windows-mounted
   test parent demonstrated the unavailable-support failure; success/collision tests run on native
   `/tmp`, where the required primitive is supported.

8. The exact stable Cargo template, bootstrap behavior, two-file example, Task 15 wrapper,
   validator, authentication, linker, ABI, relocation, unwind, and capability behavior are
   retained.

## Focused RED/GREEN evidence

- EOS poison closure: RED listed `martos/inc/martos_smp.h`, `libc/include/errno.h`, and
  `martos/src/proprietary-runtime.c` as surplus package files; GREEN 1/1.
- Complete input hashing and recheck: RED 2/2 (unchanged omitted-file hash and accepted mid-build
  mutation); GREEN 2/2.
- EOS release notes: RED 2/2 (missing and wrong heading accepted); GREEN 2/2.
- Source/provenance: RED 4/4 (dirty outer, dirty backtrace, wrong gitlink, and corrupted libc
  accepted); GREEN outer/gitlink controls plus GREEN 2/2 explicit backtrace/libc controls.
- Installer layout/tree/manifest: RED weakened/expanded, broken/escaping link, FIFO, EOS poison,
  extra schema, bad digest, and unsafe archive-name controls; GREEN 3/3 grouped controls.
- Installer TOCTOU: RED 2/2 (mutation printed instructions; replacement reached fake rustup);
  GREEN 4/4 including the unchanged rustup/no-rustup success paths.
- Atomic publication: RED file/symlink collisions escaped as raw exceptions and an empty directory
  was overwritten; GREEN 1/1 across all three collision kinds.
- Digest TOML type: RED accepted a 64-digit integer; GREEN strict schema plus exact layout 2/2.

## Fresh final verification

```text
python3 -m unittest src/tools/eos-sdk/tests/test_sdk_layout.py -v
Ran 31 tests in 372.480s
OK

python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_*.py' -v
Ran 75 tests in 259.014s
OK

python3 -m unittest -v \
  tests.eos.host.test_ffi_unwind_policy \
  tests.eos.host.test_bootstrap_target \
  tests.eos.host.test_pal_cfg_scope \
  tests.eos.host.test_libc_links \
  tests.eos.host.test_libc_source_provenance \
  tests.eos.host.test_toolchain_lock
Ran 37 tests in 23.611s
OK

PYTHONPYCACHEPREFIX=/tmp/eos-task16-fix1-final-pycache \
  python3 -Wall -Werror -m py_compile \
  src/tools/eos-sdk/bin/build-eos-sdk \
  src/tools/eos-sdk/bin/install-eos-sdk \
  src/tools/eos-sdk/tests/test_sdk_layout.py
exit 0

git diff --check -- <four Task 16 code/test paths>
exit 0
```

Git modes remain `100755` for both scripts and `100644` for the layout/test. The Git mode
summary is empty. The source example inventory is exactly `Cargo.toml` and `src/main.rs`.

## Remaining concerns

- A same-user process can still win the irreducible micro-race after the installer's final
  identity/fingerprint recheck and before rustup exec or no-rustup printing.
- Atomic no-replace publication intentionally fails closed on filesystems that do not implement
  the Linux primitive. The real builder output must therefore be placed on a supporting Linux
  filesystem.
- Hardware loader/authentication and on-target unwind execution remain the existing manual release
  gates; this fix does not change them.
