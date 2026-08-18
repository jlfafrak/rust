# Task 11 Report — Vendored libc and EOS Rust ABI Bindings

## Status

The Task 11 implementation and its bindings/layout verification are complete. On 2026-08-18 the
user explicitly approved the plan correction that keeps Task 11 scoped to the verified vendored
libc bindings and layouts, and defers the exact
`./x check library/std --target armv7a-unknown-eos-eabi` acceptance gate to Task 12, which owns
the EOS PAL routing and call-site adaptations. The check reaches the EOS target and then fails
because the generic Unix PAL assumes APIs and ABI shapes that the approved 118-export EOS
interface deliberately does not provide. Adding fake libc aliases, unexported native calls, or
misleading compatibility helpers in Task 11 would violate the stable ABI. Accordingly, no
`library/std` source was changed.

The starting base is `1d856bee74badca00bc85e86a2358a631c5c4350`. The implementation is committed
with the exact subject `library: add EOS libc bindings`; the handoff records its resulting hash.

## Source provenance and import

The authoritative source is libc 0.2.185 at commit
`71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8`. The final vendor is a `.git`-free `git archive` of
that exact commit from the authoritative `https://github.com/rust-lang/libc.git`, not the
crates.io package payload.

- No local exact git object or checkout was available. A scoped one-commit fetch into `/tmp`
  resolved `FETCH_HEAD` exactly to `71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8`; `git cat-file`
  identified it as a commit and `git fsck --full` found no corruption.
- The commit's tree object is `68d565ea31258a8056ada681c1e0ec90dcb23988` and contains 508
  tracked blobs.
- `tests/eos/fixtures/libc-0.2.185-71d5bfcc.sha256` records all 508 tracked paths and content
  digests plus source, commit, tree, and file-count metadata for future offline verification.
- The final vendor contains exactly those 508 tracked files plus `src/eos/mod.rs`. The only
  modified tracked files are `build.rs`, `src/lib.rs`, and `src/new/mod.rs`; their exact final
  digests and the EOS addition's digest are allowlisted by the provenance test.
- The tracked source `Cargo.toml` is used directly. Package-only `.cargo_vcs_info.json`,
  `Cargo.toml.orig`, and the normalized generated Cargo manifest are not vendored.
- There is no `.git`, symlink, unexpected path, or proprietary EOS header in the vendored tree.

DrvFS reports permissive working-tree modes, so every newly added vendored, report, and test
file is explicitly normalized to index mode `100644` before handoff.

## TDD evidence

The link tests were written and run before the production EOS module existed. The first static
audit failed because `src/tools/eos-libc/src/eos/mod.rs` was absent. An independent object probe
against the unmodified vendored crate then emitted undefined `open` instead of
`eos_rust_open`; an isolated link providing only `eos_rust_open` failed, and the hosted runtime
probe resolved the host libc `open` and exited 101 rather than reaching the sentinel stub. A
separate standalone compile succeeded, isolating the defect to EOS routing/link mapping rather
than the test compiler.

The C and Rust layout fixtures were also authored before the EOS module. Their first compile
failed in the vendored crate at `src/new/mod.rs` with unresolved `unistd`, proving that EOS needed
an explicit common-POSIX `new`-module route. After the production bindings were present, the
first comparator attempts honestly reported missing staged EOS `core` and then missing
`compiler_builtins`; those build prerequisites were populated before comparison. The first
actual C/Rust ABI comparison had no mismatch.

Final focused binding tests contain distinct evidence rather than treating the static audit as
behavioral TDD:

- the static audit compares the current public header, `expected-exports.txt`, and every Rust
  `#[link_name]`, all at exactly 118 unique symbols;
- a standalone crate containing the real EOS module compiles;
- an object probe has `UND eos_rust_open` and no `UND open`;
- a `-nostdlib -Wl,--no-undefined` shared link succeeds with only an
  `eos_rust_open` stub;
- a hosted executable calls that stable stub and observes its sentinel value 73.

## Binding and layout decisions

`target_os = "eos"` is selected before generic Unix in the vendored `src/lib.rs`; EOS is also
listed in `build.rs` check-cfg values. EOS does not route through Linux, NuttX, newlib, or any
other platform ABI. The unconditional upstream `new` module routes EOS directly to
`common::posix` so only the common standard-descriptor constants are reused.

`src/eos/mod.rs` defines fixed-width EOS aliases, errno and PAL constants, stable structs, and
the complete callable surface of the authoritative header. Every one of the 118 extern
declarations has an explicit `#[link_name = "eos_rust_..."]`. Rust-facing names are conventional
where useful (`stat` maps to `eos_rust_stat_path`, `__errno` maps to
`eos_rust_errno_location`), while process calls retain explicit EOS names. No pure Rust callable
helper was added and no symbol absent from the 118-export header is declared.

The ABI uses 32-bit byte counts, socket lengths, thread/key/process identities, directory
handles, and opaque synchronization words exactly as approved. File offsets, time fields, and
stable stat counters remain fixed-width. Public structs contain only compatibility pointers and
fixed-width values; no MARTOS or host-native pointer/layout is exposed.

The ARM comparator compiles C with clang for ARMv7-A Cortex-A9 softfp and Rust with the source
stage-1 EOS compiler. Each fixture emits named `eos_layout_*` byte-array object symbols whose
ELF symbol size minus one is the fact value. `readelf -Ws` supplies the comparison data and both
`readelf` and `objdump -s` outputs are retained in the requested output directory. The 111 facts
cover size, alignment, and named offsets for `timespec`, `timeval`, `stat`, `dirent`, `iovec`, all
seven pthread opaque types, IPv4/IPv6 address and socket structures, `pollfd`, `addrinfo`, and the
Task 10 spawn request and process status.

## Cargo patch and lock

`library/Cargo.toml` patches crates.io libc to `../src/tools/eos-libc`. The lock was not edited by
hand. Ordinary stable Cargo and stage-0 Cargo first rejected this Rust workspace's unstable
profile-rustflags manifest feature without changing the lock. The successful generated update
was:

```text
RUSTC_BOOTSTRAP=1 build/host/stage0/bin/cargo update \
  --manifest-path library/Cargo.toml -p libc --offline
```

It leaves libc at version 0.2.185 and removes only its registry source/checksum fields, which is
the expected representation of the local patched package.

## Verification

Final-state non-conflicting commands and results:

- `python3 -m unittest tests/eos/host/test_libc_links.py -v`: 5/5 passed.
- `python3 -m unittest discover -s tests/eos/host -p 'test_*lock*.py' -v`: 1/1 passed.
- `BOOTSTRAP_SKIP_TARGET_SANITY=1 ./x build library/core --target
  armv7a-unknown-eos-eabi`: passed; the bypass is required when the stage-0 target list performs
  its custom-target sanity check.
- `BOOTSTRAP_SKIP_TARGET_SANITY=1 ./x build library/compiler-builtins --target
  armv7a-unknown-eos-eabi`: passed.
- `python3 tests/eos/abi/compare_layouts.py /tmp/eos-task11-layout`: passed,
  `matched 111 ARM layout facts`.
- `BOOTSTRAP_SKIP_TARGET_SANITY=1 ./x test tests/ui/target-cfg/eos.rs
  tests/assembly-llvm/targets/armv7a-unknown-eos-eabi.rs`: passed; one UI target-cfg test and one
  LLVM assembly ABI test.
- The vendored libc also compiled successfully for the host as part of that stage-1 regression
  run. A standalone `cargo check --manifest-path src/tools/eos-libc/Cargo.toml` is not a valid
  command inside the Rust repository because the deliberately vendored package is patched into,
  but is not a member of, the root workspace.
- `git diff --check` and `git diff --cached --check`: passed with no output in the final
  mode/scope audit before handoff.

The required exact command
`./x check library/std --target armv7a-unknown-eos-eabi` reached stage-1 EOS `core`,
`compiler_builtins`, the vendored libc, and `std`, then exited 1 after 4 minutes 24 seconds. It did
not require the target-sanity bypass in that already-built stage-1 invocation. The first
diagnostic was:

```text
library/std/src/os/unix/fs.rs:10:22:
unresolved import `super::platform::fs`
```

Further diagnostics fall into two Task 12 PAL classes:

1. EOS platform modules/gating do not yet supply `fs`, `raw`, thread naming, `current_exe`,
   random, and related platform functions.
2. Generic Unix call sites assume capabilities or signatures absent from the stable EOS ABI,
   including signal/sigset functions, `readdir_r`, `ftruncate`, `openat`, `unlinkat`, AF_UNIX,
   pointer-valued directory handles, pointer-returning `getcwd`, one-argument `pipe`, and
   `usize` transfer counts rather than the stable EOS `u32` counts.

Those cannot be corrected honestly in the Task 11 binding layer. No std/PAL source was modified,
no `fork`/`exec`/signal name was invented, and no helper claims an unsupported capability.

## Approved acceptance ruling — 2026-08-18

The user approved the corrected task boundary explicitly: Task 11 is accepted on its verified
libc source provenance, stable 118-symbol link mapping, compile/link/runtime probes, and C/Rust
ARM layout comparison. The exact `library/std` EOS check is not a Task 11 acceptance gate; it is
deferred to Task 12 because the failures are EOS PAL selection and generic-Unix call-site
adaptation work.

The failing check is classified precisely as follows:

- **Task 11 binding layer: verified.** The vendored libc compiles for EOS, every callable maps to
  one current public `eos_rust_*` export, and all 111 ARM layout facts match the C ABI.
- **Task 12 EOS PAL modules/gating: missing.** `std` has no EOS implementations/routing yet for
  platform `fs`, `raw`, thread naming, `current_exe`, random, and related services.
- **Task 12 generic-Unix call-site adaptation: missing.** Existing consumers assume unsupported
  functions or incompatible POSIX shapes, including signal/sigset calls, `readdir_r`,
  `ftruncate`, `openat`, `unlinkat`, AF_UNIX, pointer-valued directory handles,
  pointer-returning `getcwd`, one-argument `pipe`, and `usize` transfer lengths rather than the
  EOS ABI's fixed `u32` lengths.

This ruling resolves the earlier plan-order hold without broadening Task 11. The std check's
failure remains durable evidence for Task 12; it is not hidden, weakened, or bypassed.

## Changed scope and self-review

The intended scope contains the exact imported `src/tools/eos-libc` tree plus its new EOS module,
the Cargo patch and generated lock change, three ARM layout fixtures, one focused host test, and
this report. Within the vendor, production edits are limited to `build.rs`, `src/lib.rs`,
`src/new/mod.rs`, and new `src/eos/mod.rs`; all other vendor files are pristine archive content.

Self-review confirmed 118/118 export equality, explicit stable link names, EOS-before-Unix
routing, no `.git`, no native EOS layout/pointer exposure, no Task 12 source edits, and no manual
lock edit. The approved ruling resolves the former plan-order concern. The deferred std/PAL gate
is recorded above for Task 12 and is the only downstream concern.

## Fix Round 1 — exact tracked source provenance (2026-08-18)

Review found that the first commit had authenticated the crates.io package as produced from the
pinned commit, but had not imported the commit's exact tracked tree as Task 11 Step 1 required.
The finding was verified before implementation: the vendored `Cargo.toml` began with Cargo's
normalized-package banner and referred readers to package-only `Cargo.toml.orig`; the package had
360 files while the pinned git tree has 508, with repository paths excluded from the package.
The package archive SHA-256
`52ff2c0fe9bc6cb6b14a0592c2ff4fa9ceb83eea9db979b0487cd054946a2b8f` and its
`.cargo_vcs_info.json` commit value were valid package provenance, but were insufficient proof of
an exact tracked-tree export.

### Provenance TDD

Before replacing the package payload, the 508-entry offline SHA-256 manifest and
`test_libc_source_provenance.py` were added. The production change that makes this test fail is a
missing, extra, or content-drifted upstream path outside the four explicit EOS changes.

The RED command was:

```text
python3 -m unittest tests/eos/host/test_libc_source_provenance.py -v
```

It exited 1 and reported exactly 150 tracked paths missing from the package payload, beginning
with `.cirrus.yml`, `.github/*`, `ci/*`, `ctest/*`, and `libc-test/*`. This was the expected
behavioral provenance failure, not a parser or test-fixture error.

The package tree was then replaced by a `git archive` of the verified commit. Only the three
small EOS routing patches were reapplied to `build.rs`, `src/lib.rs`, and `src/new/mod.rs`, and
the existing `src/eos/mod.rs` was restored as the sole added vendor file. The GREEN rerun passed
1/1 with 509 files observed. An independent `diff -qr` against the extracted git archive reports
only those three modified files and the `src/eos` addition.

### Fix verification

- Offline Cargo lock regeneration used
  `RUSTC_BOOTSTRAP=1 build/host/stage0/bin/cargo update --manifest-path library/Cargo.toml -p
  libc --offline`; it exited 0 with `Locking 0 packages` and 14 unchanged dependencies. The
  workspace lock required no generated content change and was not hand-edited.
- `python3 -m unittest tests/eos/host/test_libc_source_provenance.py
  tests/eos/host/test_libc_links.py tests/eos/host/test_toolchain_lock.py -v`: 7/7 passed,
  consisting of provenance 1/1, focused link/compile/runtime 5/5, and toolchain lock 1/1.
- `BOOTSTRAP_SKIP_TARGET_SANITY=1 ./x build library/core library/compiler-builtins --target
  armv7a-unknown-eos-eabi`: passed. The first comparator attempt had stopped before compilation
  because these staged sysroot prerequisites were absent; no layout mismatch occurred.
- `python3 tests/eos/abi/compare_layouts.py /tmp/eos-task11-fix1-layout`: passed,
  `matched 111 ARM layout facts`.
- `BOOTSTRAP_SKIP_TARGET_SANITY=1 ./x test tests/ui/target-cfg/eos.rs
  tests/assembly-llvm/targets/armv7a-unknown-eos-eabi.rs`: passed, one UI target-cfg test and one
  LLVM assembly ABI test. The tracked Cargo manifest built libc successfully in this run.
- The first full `git diff --cached --check` was an honest RED: after removing one blank EOF
  introduced in the generated manifest, it still reported whitespace in four otherwise pristine
  upstream paths. Their bytes could not be changed without violating exact-source provenance.
  Four exact path rules in the repository-level `.gitattributes` therefore unset whitespace
  checking only for `ci/sysinfo_guard.patch`, `libc-test/semver/hermit.txt`,
  `libc-test/semver/l4re.txt`, and `libc-test/src/cmsg.c` under the vendored tree. `git
  check-attr whitespace` reports `unset` for all four and the full `git diff --cached --check`
  GREEN rerun exits 0 with no output.
- The pre-commit index contains 158 paths: 152 additions, four modifications, and two deletions.
  All 152 additions are mode `100644`, and the staged path audit contains no `library/std`, PAL,
  or progress-ledger path.
- The fix contains no `library/std` or PAL change. The user-approved Task 12 deferral and the
  failing-check classification above remain unchanged.

The separate fix commit uses subject `library: correct EOS libc source provenance`; its full hash
is recorded in the handoff because a commit cannot contain its own hash.

## Fix Round 2 — remove unreproducible archive metadata (2026-08-18)

Re-review found that the fixture, test, and initial provenance narrative called
`6a16cc74f09fd7914b4c1e14ebe84b9ab335a62917a9200e29b2aba4122f0b2c` a deterministic git-archive
digest, but the test merely compared that fixture value with the same hardcoded value. It did
not—and could not from the vendored files alone—independently validate the tar byte stream.

The retained authenticated repository still has official origin
`https://github.com/rust-lang/libc.git`; `FETCH_HEAD` resolves exactly to
`71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8`, and `git cat-file -t` identifies that object as a
commit. The exact reproduction command is:

```text
git -C /tmp/eos-libc-upstream-71d5bfcc archive --format=tar 71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8 | sha256sum
```

It produced
`0df13342fa65b048f63bb8c5e5671320debf5ca5c6b8dea48dd850ef35ab4179`, confirming that the old
metadata was inaccurate. Rather than retain a redundant archive checksum that an offline test
cannot reconstruct, the checksum field and its tautological hardcoded expectation were removed.
The durable offline proof remains the authoritative source URL, commit and tree identities,
exact 508-file set, and independently recomputed SHA-256 for every vendored path, with only the
four intended EOS contents separately allowlisted.

For TDD, `EXPECTED_METADATA` first stopped accepting the archive field while the fixture still
contained it. The provenance test exited 1 with an `AssertionError` showing the unexpected
`git_archive_sha256=6a16cc74...` entry. Removing that single fixture entry was the production
change; the immediate GREEN rerun passed 1/1. No vendor, binding, std/PAL, or ledger content was
changed in this round.

The focused final command combined the provenance test with `test_libc_links.py` and passed 6/6
(provenance 1/1 and binding compile/link/runtime 5/5). `git diff --check` exited 0 with no output.
The round changes exactly three existing paths: this report, the provenance fixture, and its host
test; all remain mode `100644`. The separate fix commit uses subject
`library: correct libc provenance metadata`.
