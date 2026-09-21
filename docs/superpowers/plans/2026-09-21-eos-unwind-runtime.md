# EOS ARM EHABI Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every EOS Rust application link a per-application EXIDX lookup hook and an ARM EHABI runtime that does not reserve `r9` as a PIC base.

**Architecture:** The ABI archive owns the strong EXIDX hook and the linker force-loads it. The ARM GNU input is rebuilt with a repaired EHABI subset, and SDK staging rejects an archive that contains static-base `r9` addressing.

**Tech Stack:** C11, GNU ld, ARM GNU 14.3.1, Python 3 unittest, Rust board-test application.

**Spec:** `docs/superpowers/specs/2026-09-21-eos-unwind-runtime-design.md`

## Global Constraints

- Do not change MARTOS-SMP.
- Use `-march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp`.
- Require GCC 14.3.1 EHABI objects built with `-mno-single-pic-base` and `-mno-pic-data-is-text-relative`.
- Keep the app-local EXIDX workaround out of final applications.

---

### Task 1: Provide the EXIDX hook from the SDK ABI archive

**Files:**
- Create: `src/tools/eos-abi/src/eos_unwind.c`
- Modify: `src/tools/eos-abi/CMakeLists.txt`
- Modify: `src/tools/eos-sdk/linker/app_linker_script.ld`
- Modify: `src/tools/eos-sdk/bin/eos-rust-link`
- Test: `src/tools/eos-sdk/tests/test_linker_args.py`

**Interfaces:**
- Produces: `uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int *count)`.
- Consumes: linker-defined `__exidx_start` and `__exidx_end`.

- [ ] **Step 1: Write the failing link-driver test**

```python
assert "-Wl,-u,__gnu_Unwind_Find_exidx" in argv
assert "PROVIDE(__gnu_Unwind_Find_exidx = 0);" not in linker_script
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest src.tools.eos-sdk.tests.test_linker_args -v`

Expected: the driver does not force-load the ABI hook and the linker script still provides zero.

- [ ] **Step 3: Implement the ABI hook and force-load behavior**

```c
uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int *count) {
    (void)pc;
    if (count != NULL)
        *count = (int)((__exidx_end - __exidx_start) / 2U);
    return (uintptr_t)__exidx_start;
}
```

- [ ] **Step 4: Run link-driver and ABI tests**

Run: `python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_linker_args.py' -v`

Expected: PASS; the archived SDK ABI hook is selected before libgcc.

### Task 2: Enforce the repaired ARM EHABI runtime contract

**Files:**
- Modify: `src/tools/eos-sdk/bin/build-eos-sdk`
- Modify: `src/tools/eos-sdk/tests/test_sdk_layout.py`
- Modify: `/home/dev/code/arm-toolchain-build/build-all.sh`

**Interfaces:**
- Consumes: ARM GNU `libgcc.a` selected by `arm-none-eabi-gcc -print-libgcc-file-name`.
- Produces: SDK package only when all four EHABI archive members have no `r9`-base memory operand.

- [ ] **Step 1: Write the failing staging-contract test**

```python
with mock.patch.object(builder, "run_checked", return_value="... [r9, r3] ..."):
    with self.assertRaisesRegex(builder.BuildError, "r9 PIC base"):
        builder.verify_ehabi_runtime(...)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest src.tools.eos-sdk.tests.test_sdk_layout.BuilderContractTests -v`

Expected: `verify_ehabi_runtime` is absent.

- [ ] **Step 3: Implement validation and rebuild configuration**

```python
for member in ("unwind-arm.o", "libunwind.o", "pr-support.o", "unwind-c.o"):
    require_member(libgcc, member)
    reject_r9_memory_operand(objdump(member))
```

The ARM runtime build flags remove `-msingle-pic-base -mpic-register=r9` and retain `-fPIC -mno-pic-data-is-text-relative` plus the pinned target flags.

- [ ] **Step 4: Run the builder test suite**

Run: `python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_sdk_layout.py' -v`

Expected: PASS; a runtime with `[r9, ...]` is rejected and a repaired archive passes.

### Task 3: Rebuild, package, and board-verify

**Files:**
- Modify: `tests/eos/apps/unwind/src/main.rs` only if required to emit an explicit completion marker.
- Modify: `docs/eos/manual-board-test.md` with the repaired-runtime verification command.

**Interfaces:**
- Consumes: repaired ARM toolchain and packaged SDK.
- Produces: target-side `unwind` app completion after `Backtrace::force_capture` and `catch_unwind`.

- [ ] **Step 1: Build a fresh ARM GNU input with repaired EHABI objects**

Run the ARM GNU 14.3.Rel1 build with the target flags from Task 2 and inspect `libgcc.a` with `arm-none-eabi-objdump -dr`.

- [ ] **Step 2: Build the SDK and a clean app**

Run: `build-eos-sdk --source-root ... --arm-gnu-root ... --eos-sdk-root ... --output ...`

Expected: package succeeds and the app ELF has a non-zero `__gnu_Unwind_Find_exidx` function.

- [ ] **Step 3: Reboot, upload, round-trip verify, and launch over UART**

Run: reboot via RFT; upload to `/tmp`; download and compare SHA-256 and `cmp -s`; then launch only with UART `load` and application commands.

- [ ] **Step 4: Record board outcome**

Expected: captured backtrace, contained panic, and drop-cleanup success; no prefetch/data abort.
