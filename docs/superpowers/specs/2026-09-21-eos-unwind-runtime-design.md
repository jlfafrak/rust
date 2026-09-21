# EOS ARM EHABI Runtime Design

## Goal

Make ARM EHABI backtraces and Rust panic unwinding safe in every
`armv7a-unknown-eos-eabi` application, without changing MARTOS.

## Root cause

The ARM GNU runtime currently supplied to the SDK was built with
`-msingle-pic-base -mpic-register=r9`.  Its EHABI objects use `r9` as a
fixed GOT base, but the ARM EABI reserves no such application-wide value.
Rust code and foreign frames may validly use `r9` for ordinary values.
Consequently the unwinder dereferences arbitrary addresses while handling a
backtrace or panic.

The runtime also weakly calls `__gnu_Unwind_Find_exidx`.  The previous
linker-script zero provider was relocated by MARTOS as an image-base pointer,
so the unwinder treated the application header as executable code.

## Architecture

The EOS Rust ABI archive provides a strong
`__gnu_Unwind_Find_exidx(uintptr_t, int *)`.  It returns the current
application's linker-defined `__exidx_start` and `__exidx_end` range.  The
linker driver force-loads that ABI object and removes the zero-valued linker
script provider, ensuring every SDK-linked application has the same hook.

The ARM GNU toolchain used as an SDK input must contain an EHABI subset
(`unwind-arm.o`, `libunwind.o`, `pr-support.o`, and `unwind-c.o`) built with
the EOS ARM flags plus `-mno-single-pic-base` and
`-mno-pic-data-is-text-relative`.  The complete `libgcc.a` remains in place:
only the four EHABI objects are rebuilt, so normal compiler builtins stay
identical to the pinned ARM GNU release.  SDK staging verifies the selected
archive before packaging and refuses an archive with an `r9` GOT-base memory
access.

## Data flow

1. The ARM GNU runtime builder produces `libgcc.a` with the four repaired
   EHABI objects.
2. `build-eos-sdk` validates the selected softfp archive, stages it, and
   records its input-tree hash as usual.
3. `eos-rust-link` force-loads the ABI archive's EXIDX hook before linking
   the validated `libgcc.a`.
4. At runtime, the unwinder calls the hook and walks the application's EHABI
   index without a special register convention.

## Error handling

SDK assembly fails before Rust compilation if the ARM GNU input is missing an
EHABI member, has an unexpected member layout, or contains a static-base
`[r9, ...]` access.  This is intentionally a build error: a successful SDK
package must not contain an unwinder that can corrupt or crash applications.

## Verification

Automated tests cover the link-driver force-load behavior, the non-zero
strong EXIDX hook, and the SDK build rejection of an `r9`-based EHABI object.
The target board-test `unwind` app verifies `Backtrace::force_capture()` and
`catch_unwind()` through real panic-drop cleanup.  The application-local
diagnostic hook is removed before the final board deployment.

## Constraints

- Do not modify MARTOS-SMP.
- Preserve ARMv7-A Cortex-A9 softfp ABI flags.
- Keep GCC runtime source/version at 14.3.Rel1 / GCC 14.3.1.
- Do not accept a zero `__gnu_Unwind_Find_exidx` provider.
