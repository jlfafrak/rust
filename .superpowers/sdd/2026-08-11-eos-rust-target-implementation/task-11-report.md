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
`71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8`. A verified local crates.io archive was reused at
`/home/dev/.cargo/registry/cache/index.crates.io-1949cf8c6b5b557f/libc-0.2.185.crate`; no network
retrieval was needed.

- Archive SHA-256:
  `52ff2c0fe9bc6cb6b14a0592c2ff4fa9ceb83eea9db979b0487cd054946a2b8f`.
- The archive digest exactly matches the checksum in the base `library/Cargo.lock`.
- The archive's `.cargo_vcs_info.json` records SHA-1
  `71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8` with an empty `path_in_vcs`.
- The archive was extracted with `tar -xzf ... --strip-components=1` into
  `src/tools/eos-libc`; the pristine import contained 360 files.
- The deterministic pristine path/content digest was computed by sorting null-delimited paths,
  hashing each file, and hashing that manifest. It is
  `188378303cfc7821d2fb9cf01114e8d77a83614ba446397d4fef061b76274b97`.
- The production EOS module is the sole added file within the imported tree, bringing the
  working tree to 361 files. There is no `.git` directory and no proprietary EOS header in the
  vendored source.

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
