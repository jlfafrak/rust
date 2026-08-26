# Task 14 Report — ARM EHABI Unwinding and Ordinary-FFI Containment

## Status and boundary

Task 14 establishes the EOS Rust-side ARM GNU unwinder contract and adds executable
unwind/ordinary-FFI containment fixtures. The exact no-bypass EOS `std` check passes. The
artifact build compiles the EOS unwind stack and stops solely because the Task 15
`eos-rust-link` wrapper does not exist.

This task does not add a linker wrapper or script, use the host linker, disable dynamic linking,
change `panic=unwind`, or claim a final EOS executable link. Literal final-ELF
`.ARM.exidx`/`.ARM.extab` retention and the absence of unresolved `_Unwind_*` or
`__aeabi_unwind*` symbols remain Task 15 gates.

The starting base is `47f0c30eff1f4f903fff6b29c1b96103b132c40a`. The change is committed
with the required subject `runtime: enable contained Rust unwinding on EOS`; its resulting hash
is recorded in the controller handoff because a committed report cannot contain its own stable
commit hash.

## TDD evidence

The first policy/behavior suite was authored before the production link change. Its RED run was:

```text
python3 -m unittest tests/eos/host/test_ffi_unwind_policy.py -v
```

Five independent tests passed: the host Rust unwind/drop probe, C and C++ callers of the real
Rust containment library, the empty EOS-owned `C-unwind` allowlist, the existing Unix GCC
personality route, and current thread-root/TLS-cleanup guards. The sixth test failed only because
`library/unwind/src/lib.rs` lacked the exact EOS `#[link(name = "gcc")]` contract. This proved
that the test was sensitive to the missing production behavior rather than a fixture or compiler
error.

Adding only the EOS-scoped link attribute turned the suite GREEN at 6/6. Removing or changing
that link contract makes the original RED recur. Removing `catch_unwind`, returning a value other
than the fixture-local documented `-1`, or failing to run the drop guard makes the separately
compiled C/C++ behavior test fail or abort.

A second focused RED/GREEN cycle covered cleanup panics. Before the unwind app had a cleanup mode,
the subprocess returned zero instead of aborting. The app now initializes a Rust TLS value whose
destructor panics on a spawned thread. The GREEN test observes exactly `SIGABRT`, exercising
`std`'s no-unwind TLS destructor boundary rather than accepting any generic nonzero exit.

## Implemented contract and fixtures

EOS remains a Unix-family target, so `library/unwind` already selects its libunwind declarations
and `library/std/src/sys/personality/mod.rs` already selects `mod gcc`. The production change is
the explicit target-scoped native library contract:

```rust
#[cfg(target_os = "eos")]
#[link(name = "gcc")]
unsafe extern "C" {}
```

The Task 15 wrapper will resolve that contract to the pinned ARM GNU multilib `libgcc.a` in the
required final link ordering.

The `unwind` application catches a Rust panic, proves its drop guard ran, and provides the
separate TLS-cleanup abort mode. The `ffi-containment` static library exports exactly one ordinary
`extern "C"` fixture function. It catches its own Rust work and returns zero or the documented
fixture-local contained-panic value `-1`; it aborts if its drop guard did not run. The constant is
not part of `libeos_rust_abi`, and no stable native export was added. `ffi_caller.c` is valid C and
C++; the host and ARM checks compile it both ways. The C++ fixture never throws into Rust—it only
proves that Rust returns normally after containing its own panic.

## Thread-root and cleanup audit

### Rust-created threads

`std::thread` builds the user closure in `library/std/src/thread/lifecycle.rs` and executes it
inside `panic::catch_unwind`, storing the panic result for `JoinHandle::join`. The Unix PAL passes
an ordinary `extern "C" fn thread_start` to `eos_rust_pthread_create`. The native compatibility
trampoline invokes that callback and therefore receives a normal return after a user panic is
contained. Any panic from initialization outside the lifecycle catch cannot unwind through the
ordinary C boundary and aborts.

After the callback returns, the native trampoline runs `eos_tls_cleanup_current` before publishing
completion. Rust TLS destructor callbacks are ordinary C callbacks guarded by
`abort_on_dtor_unwind`; a destructor panic therefore aborts rather than entering native cleanup.
The focused source policy verifies this chain, the native thread/TLS/destructor tests cover its
lifecycle and races, and the TLS subprocess proves the abort behavior.

### External/EOS-created roots

MARTOS 14.0.39 exposes no safe universal external-thread termination or Rust-entry interception
hook. Task 14 therefore does not invent one or widen the stable native ABI. Every Rust entry point
placed on an external/EOS-created root must be its own ordinary-C containment wrapper. The
`eos_ffi_containment_probe` fixture is that release-one pattern: its body catches the Rust work and
converts panic to `-1` before returning to C/EOS. The C and C++ executable callers prove this exact
boundary. Any future ordinary-C Rust wrapper must follow the same pattern and is covered by the
zero-allowlist/source policy.

### `C-unwind` policy scope

`C_UNWIND_ALLOWLIST` is literally empty. The scan covers EOS-owned production Rust sources: the
target definition, EOS libc module, unwind link root, EOS-named `std` modules, and shared `std`
files with explicit EOS cfg branches. It rejects actual `extern "C-unwind"`/ABI declarations,
not explanatory prose. Generic upstream unwinder internals retain their existing ABI declarations;
they are the implementation of Rust unwinding, not new EOS application FFI entry points, and the
Task 14 brief does not authorize rewriting them.

## Target compile and ARM object evidence

Both Rust fixtures type-check against the freshly checked EOS `std`/`panic_unwind` metadata using
the source stage-1 compiler, `-Cpanic=unwind`, and `-Dffi-unwind-calls`. The final unwind-app
metadata compile also uses `-Dwarnings` (with only crate-kind-induced dead-code disabled) and is
clean. The resulting final nonempty metadata files are 7,530 bytes for the unwind app and 4,766
bytes for the containment library.

The C and C++ callers compile with ARM GNU 14.3.1 using the approved Cortex-A9 softfp flags:

```text
-march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp -fPIC
```

Both are ELF32 little-endian ARM EABI5 relocatable objects. ARM attributes report v7 Application
profile, A32, Thumb-2, VFPv3, NEON, and small enums. Both contain allocated
`.ARM.exidx.text.startup`; their only undefined symbols are the expected
`eos_ffi_containment_probe` and `__aeabi_unwind_cpp_pr0` pre-link inputs.

The freshly rebuilt EOS Rust `libpanic_unwind` archive supplies stronger Rust-side pre-link
evidence. `readelf -SW` finds multiple allocated, nonempty `.ARM.exidx.*` and `.ARM.extab.*`
sections on Rust panic, cleanup, and drop-glue objects. `nm -u` reports the expected pre-link
inputs `_Unwind_DeleteException`, `_Unwind_RaiseException`, `_Unwind_Resume`,
`__aeabi_unwind_cpp_pr1`, and `rust_eh_personality`. The rebuilt `libunwind` archive contains ARM
EHABI entries and `_Unwind_VRS_Get`/`_Unwind_VRS_Set` inputs. These unresolved archive symbols are
not represented as final-link success.

An initial direct Cargo app build with stage-1 `rustc` stopped at E0463 because the checked EOS
`std` metadata was not installed into the stage-1 sysroot. A direct object-code attempt against
check-only rmeta then ICEd on missing optimized MIR; the generated ICE file was removed. This is
an artifact-availability limit, not a source compile failure. The successful metadata compiles,
the actual Rust archive objects, and the exact bootstrap artifact endpoint above are the strongest
honest evidence before Task 15 installs a linkable sysroot.

## Verification

- Focused unwind/FFI policy and host behavior: 6/6 passed. It includes a real Rust drop-unwind,
  exact TLS-cleanup `SIGABRT`, real cdylib export check, and separately linked/running C and C++
  executables.
- Exact no-bypass `./x check library/std --target armv7a-unknown-eos-eabi`: passed after rebuilding
  `unwind`, `panic_unwind`, and `std`; completed in 3:22.
- Exact `./x build library/std --target armv7a-unknown-eos-eabi`: compiled the EOS unwind stack and
  stopped only with `linker 'eos-rust-link' not found`; completed unsuccessfully in 2:06 as the
  expected Task 15 endpoint.
- Non-EOS preservation: `./x check library/unwind --target x86_64-unknown-linux-gnu` passed in 1:17.
- Consolidated new policy, bootstrap, PAL cfg, libc-link, provenance, and toolchain-lock suite:
  24/24 passed in 67.063 seconds.
- Native root/TLS focused matrix: normal plus TSan 10/10; strict O0 7/7; strict O3 7/7. Coverage
  includes runtime cleanup, thread/TLS/destructors, MARTOS thread/runtime mapping, host consumer,
  and thread/TLS/destructor TSan executables.
- Exact host and MARTOS-configured archive export checks: 120/120 each. No public native ABI or
  test seam was added. The MARTOS contract archive is host object format and is correctly read by
  host `nm`; an initial ARM `nm` attempt reported file format mismatch and no production defect.
- `./x build library/core library/compiler-builtins --target armv7a-unknown-eos-eabi`: passed in
  1:11, supplying the rlibs required by the layout probe.
- `python3 tests/eos/abi/compare_layouts.py /tmp/eos-task14-layout`: passed with 111 matched ARM
  C/Rust layout facts. Its first attempt stopped before compilation because check-only sysroot
  metadata lacks `libcore.rlib`; building core/compiler-builtins resolved that prerequisite.
- EOS target cfg UI and Cortex-A9 softfp assembly tests: 1/1 and 1/1 passed in the combined run.
- Strict ARM GNU C and C++ fixture compiles, `readelf -h -A -SW`, and `nm -u`: passed with the
  object/metadata results recorded above.

## Scope and deferred gates

Production scope is one EOS-only link attribute in `library/unwind/src/lib.rs`. Test scope is the
two requested Cargo fixtures, the dual-language caller, the policy/behavior test, and matching
per-fixture ignore files. No `library/std` selector, target option, bootstrap rule, native header,
runtime source, stable export manifest, linker wrapper, linker script, SDK packager, or progress
ledger is changed.

Task 15 must still provide `eos-rust-link`, append the pinned ARM GNU multilib `libgcc.a` after
Rust archives, retain allocated `.ARM.exidx` and required `.ARM.extab` through the final linker
script, and then run the literal final executable gates:

- successful ordinary Cargo links for both Task 14 crates;
- final ELF32 ARM EABI5 `ET_DYN` PIE validation;
- allocated final `.ARM.exidx` and `.ARM.extab` when required;
- no unresolved `_Unwind_*`, `__aeabi_unwind*`, or personality dependency outside the approved
  dynamic/runtime contract.

Those results are deliberately not claimed here. Hardware execution of ARM EHABI catch/drop,
ordinary-FFI containment, and backtrace behavior also remains part of the manual board matrix.

## Fix Round 1 — review hardening

This follow-up addresses the review findings without changing the Task 15 boundary or widening
the native ABI. It is a separate fix commit from the implementation commit recorded above. The
closure, lint, verification, and scope statements in this Fix Round 1 section supersede the
corresponding historical statements from the initial implementation run.

### Independent RED controls

The policy test was first extended only with mutation/control cases and the missing crate-policy
assertion. The initial run was:

```text
python3 -m unittest -v tests.eos.host.test_ffi_unwind_policy
Ran 13 tests ... FAILED (failures=9)
```

The independent failure records demonstrated that the old checks accepted each of these defects:

- an injected `extern "C-unwind"` declaration in shared Unix `std` personality code;
- omission of `library/unwind/src/libunwind.rs` and silent omission of the wasm backend;
- removal of the target's Unix-family fact while a disconnected `mod gcc` substring remained;
- replacement of the lifecycle `catch_unwind` while a disconnected marker remained;
- removal of the OS TLS destructor abort wrapper while a disconnected marker remained;
- an additional defined dynamic export beside `eos_ffi_containment_probe`;
- absence of the production lint policy from `library/unwind` and both Task 14 crates.

The production GNU unwinder contract and the real host unwind/containment behavior continued to
pass during RED. Thus the failures were specific to the review gaps, not regressions in the
original runtime behavior. A further GREEN mutation control now also rejects changing the GCC
personality's ARM EHABI cfg branch to a non-ARM branch.

### Hardened production closure and internal unwind audit

The EOS entry-surface scan now covers a conservative superset of the relevant production closure:
all Rust sources under `library/std`, `library/panic_unwind`, and the vendored libc crate, together
with the EOS target. The entry allowlist remains exactly empty. The scanner removes comments,
extracts named `extern "C-unwind"` blocks, and rejects other `C-unwind`/ABI tokens rather than
accepting a path because it lacks an explicit EOS substring.

Unwinder internals are audited separately rather than silently omitted. The complete
`library/unwind/src/libunwind.rs` declaration set is pinned by path and symbol:

- active for EOS: `_Unwind_Resume` and `_Unwind_RaiseException`;
- excluded for EOS by the pinned Apple/ARM SjLj cfg branch:
  `_Unwind_SjLj_RaiseException`.

The target's `families: cvs!["unix"]` fact is coupled to the `library/unwind` Unix branch selecting
`mod libunwind`. The wasm branch is separately required to be guarded by
`target_family = "wasm"`, and its sole `C-unwind` intrinsic `wasm_throw` is pinned while being
excluded from the EOS closure by that cfg evidence.

The vendored libc crate was evaluated as requested. It has no `C-unwind` declaration or call, and
its source is included in the empty entry-surface scan, so adding a crate-root lint there would
not protect an actual unwind call and was not justified. `panic_unwind` necessarily raises Rust
panics through the audited unwinder-internal `_Unwind_RaiseException`; it is not an ordinary FFI
entry surface and is not incorrectly denied from performing that required operation.

### Production lint and SDK boundary

`std` already had the stronger unconditional `#![deny(ffi_unwind_calls)]`. Fix Round 1 adds
`#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]` to the `unwind` crate root and both Task
14 fixture crate roots. Host fixture compilation continues to pass an explicit
`-Dffi-unwind-calls`, so the lint is exercised even while the behavioral binaries run on the host.

The exact no-bypass EOS std check compiled `unwind`, `panic_unwind`, and `std` with the source-level
policy active:

```text
./x check library/std --target armv7a-unknown-eos-eabi
Build completed successfully in 0:02:33

./x check library/unwind --target armv7a-unknown-eos-eabi
Build completed successfully in 0:01:05
```

Both Task 14 sources also compile against the freshly checked EOS metadata with
`-Cpanic=unwind -Dffi-unwind-calls -Dwarnings`. The unwind app was compiled as an rlib only to
obtain meaningful pre-link metadata, with crate-kind-only dead code disabled; its rmeta is 7,552
bytes. The containment rmeta is 4,771 bytes. An initial direct invocation omitted the explicit
matching `panic_unwind` metadata and stopped with E0463. Supplying the checked
`std`/`panic_unwind`/`unwind` metadata resolved that artifact-selection issue, after which both
linted compiles passed.

Task 14 does not impose a compiler flag on arbitrary downstream user crates. Shipping
`-Dffi-unwind-calls` through the EOS SDK/Cargo configuration is the Task 16 user-application
policy boundary. Task 14 enforces the production runtime crates and all of its own EOS Rust
fixtures now; it does not pull Task 16 packaging into this commit.

### Coupled root, cleanup, and export checks

The personality policy now couples the target's Unix family, unwind panic strategy, and ARM arch
to the Unix `mod gcc` selection and then extracts the actual ARM/not-Apple/not-NetBSD EHABI branch,
requiring its `_Unwind_State` signature and `__gnu_unwind_frame` dependency.

The root policy extracts bounded bodies rather than accepting disconnected substrings:

- the lifecycle `rust_start` closure's `catch_unwind` body contains hooks and the user closure,
  and the result is published before the packet is dropped;
- the Unix `thread_start` is exactly ordinary `extern "C"`, reconstructs/initializes the Rust
  closure, calls it, and returns null in order, so panics outside the inner lifecycle catch abort
  at the ordinary-C root;
- `has_thread_local: false` is coupled to the OS TLS selection and Unix key backend;
- the actual OS `destroy_value` body runs sentinel set, destructor drop, null reset, and cleanup
  re-enable in order inside `abort_on_dtor_unwind`;
- the EOS native trampoline performs TLS identity, callback, TLS cleanup, and completion publish
  in order; native TLS cleanup invokes each destructor before releasing its ownership and clears
  the TLS slot before freeing the root.

The existing subprocess still observes exact `SIGABRT` for a Rust TLS cleanup panic. The dynamic
host library assertion now requires the entire defined dynamic symbol set to equal
`{eos_ffi_containment_probe}`; a synthetic surplus-export control proves this is not a membership
check.

### Fix-round verification and unchanged deferred gates

- Focused final policy/behavior suite: 14/14 passed, including all mutation controls, real C and
  C++ callers, drop unwind, and exact cleanup `SIGABRT`.
- Non-EOS preservation: `./x check library/unwind --target x86_64-unknown-linux-gnu` passed in
  2:32.
- Targeted native roots/cleanup regression: 10/10 passed after rebuilding the existing native
  tree. It covers runtime services, thread, TLS, TLS destructors, MARTOS thread/runtime contracts,
  the host consumer, and the thread/TLS/destructor TSan executables.
- Consolidated unwind policy, bootstrap, PAL, libc link/provenance, and toolchain-lock suite:
  32/32 passed in 21.133 seconds. No target selector, native source, header, or export manifest
  changed.

The initial ARM Rust/C object metadata and symbol evidence remains applicable. This fix changes
only lint policy and policy-test precision; it does not manufacture a linkable sysroot. The exact
artifact endpoint remains the missing target-spec `eos-rust-link`. Literal final-ELF
`.ARM.exidx`/`.ARM.extab` retention and resolved `_Unwind_*`/`__aeabi_unwind*` gates remain
Task 15-dependent and are not claimed here. No Task 15 file or progress ledger is changed.

## Fix Round 2 — root-closure and code-only structural checks

This second review follow-up changes policy-test implementation only. The production runtime,
native ABI/runtime, target, Task 15 inputs, and controller-owned progress ledger are unchanged.

### Exact RED controls

Three mutations were added before changing the scanner or structural helpers:

```text
python3 -m unittest -v tests.eos.host.test_ffi_unwind_policy
Ran 17 tests ... FAILED (failures=3)
```

The failures reproduced the two root causes precisely:

- injecting `unsafe extern "C-unwind" { fn injected_unwind_root(); }` into
  `library/unwind/src/lib.rs` was not rejected because the entry-surface source set omitted the
  unwind crate root;
- replacing the native callback with an in-place block comment plus `result = NULL`, or commenting
  out `eos_tls_cleanup_current()`, was not rejected because raw substring ordering counted text in
  comments as executable calls.

The other fourteen policy controls and behavioral fixtures remained green in this RED run.

### GREEN implementation

`library/unwind/src/lib.rs` is now part of `eos_entry_production_rust_sources()`, so its EOS entry
surface participates in the same literally empty `C-unwind` allowlist as `std`, `panic_unwind`,
libc, and the target. The separate GNU contract test continues to allow only the exact zero-symbol
ordinary-C block selected by `#[cfg(target_os = "eos")] #[link(name = "gcc")]`. The separately
audited libunwind set is unchanged: EOS activates only `_Unwind_Resume` and
`_Unwind_RaiseException`; Apple SjLj and wasm remain excluded by their pinned cfg evidence.

The structural policy now lexically masks line comments, nested block comments, normal/raw string
literals, and character literals while preserving byte offsets and newlines. Bounded Rust/C body
extraction balances braces only in executable code. Every marker-order assertion searches this
code-only representation, so neither comments nor diagnostic strings can stand in for calls.
Cfg/source assertions ignore comments while retaining semantically relevant string literals.

For `C-unwind` declarations, the exact ABI literal is first converted to a length-preserving token
and every other comment/literal is masked. This keeps real ABI declarations auditable without
accepting explanatory text or raw strings as declarations. A cheap exact-token prefilter avoids
lexing vendored files that cannot contain the ABI.

The native trampoline check therefore requires actual bounded call expressions for
`record->start_routine(record->argument)` followed by `eos_tls_cleanup_current()`, then result and
completion publication. The exact callback and cleanup comment-out controls are both GREEN, as
are the pre-existing lifecycle, TLS destructor, cleanup-order, personality, export, C, and C++
controls.

### Verification and unchanged boundary

- Focused unwind/FFI policy and behavior: 17/17 passed in 22.554 seconds.
- Consolidated unwind policy, bootstrap, PAL, libc link/provenance, and toolchain-lock suite:
  35/35 passed in 24.098 seconds.
- Fresh exact no-bypass EOS std lint check:
  `./x check library/std --target armv7a-unknown-eos-eabi` passed in 1:22.
- Fresh standalone EOS unwind lint check:
  `./x check library/unwind --target armv7a-unknown-eos-eabi` passed in 2:35.

No production source changed in Fix Round 2. The missing target-spec `eos-rust-link` remains the
honest build endpoint, and final ELF EHABI retention and resolved unwind-symbol checks remain Task
15 gates. No final link result is claimed.
