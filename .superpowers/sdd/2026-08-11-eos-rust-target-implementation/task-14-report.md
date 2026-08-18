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
