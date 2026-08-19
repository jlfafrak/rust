# Task 15 Report — EOS Link Validation and Authentication

## Status and boundary

Task 15 supplies the EOS linker wrapper, application linker script, dynamic-symbol allowlist,
ELF validator, and authentication packager. The exact EOS `std` bootstrap now succeeds, and the
Task 14 unwind application links as a final EOS PIE, passes the strict application validator,
packages with the exact MARTOS authentication trailer, and passes allowed-trailer validation.

The outer repository started at `596c23b0a4fdb1c8e0020e9f01597fb56b8a59f6`. The required outer
commit subject is `tools: add EOS link validation and authentication`; its resulting hash is
reported in the controller handoff because a commit cannot contain its own stable hash.

One evidence-driven integration correction is deliberately disclosed outside the Task 15 file
list. `library/backtrace` is a submodule whose starting commit was
`28ec93b503bf0410745bc3d571bf3dc1caac3019`. Its `codex/task-15-eos-arm-unwind` branch adds one EOS
cfg line and is committed as `02ef1b533157e8ddbd0f9295c867e79b59e9bbbd` (subject
`Fix EOS ARM backtrace unwind macros`). The outer Task 15 commit records that gitlink and depends
on carrying or publishing the nested commit; neither repository is pushed by this task.

No Task 16 file and no progress ledger is changed. Hardware execution remains a manual board
gate; static inspection does not claim runtime execution on XC7Z030 or XC7Z045 hardware.

## RED / GREEN evidence

The initial independent tests were written while every production Task 15 tool was absent. The
first full run produced 29 failures/errors: wrapper subprocess tests ended at missing script,
ELF mutation tests could not invoke a validator, authentication round trips could not invoke a
packager, and real GNU linker-script tests had no script. This established a production-absence
RED rather than a fixture-only failure.

Focused cycles then exposed and fixed concrete behavior:

- The first linker script used symbolic `PT_ARM_EXIDX`; pinned GNU ld rejected that token. The
  script now uses the specified numeric program-header type `0x70000001` and the real header test
  proves `ARM_EXIDX` is emitted.
- A first TLS fixture was garbage-collected before the script assertion. The independent fixture
  was corrected to reference its TLS datum; the real linker then rejected it with
  `native TLS is unsupported`.
- Trailer comparison initially used a nonexistent `hashlib.compare_digest`, and marker-only input
  reached the wrong diagnostic. The validator now uses constant-time `hmac.compare_digest` and
  checks ELF magic before trailer processing.
- A real C/ABI link exposed `R_ARM_GLOB_DAT` for weak `__gnu_Unwind_Find_exidx` and
  `__cxa_type_match`. New tests first failed on those relocations. No approved MARTOS/toolchain
  archive defines either hook; the script's zero-valued `PROVIDE` entries preserve their weak
  fallback semantics without dynamic relocations.
- The pinned `libmartos_c++abi.a` does define `__cxa_begin_cleanup` in `cxa_exception.o` and
  `__cxa_call_unexpected` in `cxa_personality.o`. Tests first failed because archive scanning did
  not extract those weak EHABI providers. The wrapper now forces exactly those two definitions
  immediately before the resolved `libmartos_c++abi.a`; it does not use `--whole-archive` or
  `-Bsymbolic`.
- The first exact `./x build library/std --target armv7a-unknown-eos-eabi` reached the new wrapper
  and failed because rustc requests a shared `libstd-<hash>.so`. A captured rustc-shaped shared
  invocation plus bypass tests produced six focused failures. The narrow sysroot-shared mode then
  passed, while arbitrary DSOs, mismatched/alternate sonames or version scripts, PIE/static/
  hard-float mixes, application scripts/libraries, and symlink escapes remained rejected. Two
  later mutation controls independently caught alternate `-Wl,-soname,...`, alternate version
  script, and `-Wl,-pie` encodings before those paths were closed.
- The first exact Task 14 application link retained `-z defs` and failed only on
  `_Unwind_GetIP` and `_Unwind_FindEnclosingFunction`. ARM GNU 14.3.Rel1 implements these as header
  macros and no approved archive or multilib defines the strong functions. A focused policy test
  was RED until EOS ARM selected backtrace-rs's existing `_Unwind_VRS_Get` macro expansion and
  identity enclosing-function implementation. The authorized fix is one cfg exclusion line in
  the nested backtrace submodule; no shim archive or undefined-symbol relaxation was added.
- Independent review reproduced a driver-policy escape: GCC accepted `-Wl,-o,/outside` and
  `-Wl,@response`, while related `-Xlinker`, `-B`, specs, sysroot, plugin, and output aliases
  could bypass direct-argument checks. Eleven app/shared subtests were RED. Both modes now reject
  opaque forwarding, output/mode/script/interpreter/tool-selection overrides in all supported
  encodings, and compiler probes and execution receive only `PATH=os.defpath`, `LC_ALL=C`, and
  `LANG=C`. A hostile-environment subprocess test and the exact bootstrap are GREEN.
- Review also found `_end` after `.ARM.attributes 0`; the real application had `_end=0x224f`
  while `__bss_end__=0x0f94d0`. A real ARM linker test was RED (`33` versus `4276` in its small
  fixture). `_end` now immediately follows `.bss`, before the zero-address attributes section;
  the final application proves `_end == __bss_end__ == 0x0f94d0`.
- Validator and packager replacement-attack tests were RED because bytes were read from one path
  and external inspection reopened the mutable source path. Each external validation now uses a
  private, read-only, fsynced snapshot of the exact captured byte string and verifies it before
  and after the subprocess. The packager validates and publishes the same captured base bytes,
  even when its source pathname is replaced concurrently. Both attack tests are GREEN.
- A final review mutation showed that searching for the authentication marker anywhere rejected
  legitimate embedded bytes. Trailer recognition is now anchored to the exact 129-byte suffix;
  embedded marker bytes remain part of the ELF and are included in the package digest.

The final Task 15 suite is 42/42 GREEN. The consolidated unwind, bootstrap, PAL, libc link/source,
and toolchain-lock regression is 37/37 GREEN, including the new backtrace cfg check and a mutation
that proves removing the EOS line is rejected.

## Staged SDK root and command construction

The pinned staged layout, also consumed by source-tree tests, is:

```text
<root>/bin
<root>/arm-gnu
<root>/eos
<root>/lib
<root>/include
<root>/linker
<root>/manifests
```

`EOS_RUST_SDK_ROOT` selects a test/staged root. Without it, each packaged script derives the root
from its own `<root>/bin` location. Every root-derived tool, library, script, manifest, and selected
multilib `libgcc.a` is strictly resolved beneath that root; symlink escapes fail before execution.
The compiler must report GCC `14.3.1`, the compiler version shipped as ARM GNU 14.3.Rel1. The
wrapper queries the absolute softfp multilib `libgcc.a` using the same four target flags used for
the link and rejects a relative, escaped, or differently named answer.

Application mode invokes the pinned compiler with:

```text
-march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp
-fPIC -pie -nostdlib -Wl,--gc-sections -Wl,-z,notext -Wl,-z,defs
-Wl,--dynamic-linker=/usr/lib/ld.so.1
-Wl,-T,<root>/linker/app_linker_script.ld
<all rustc output/search/export/object/archive arguments, unchanged>
<root>/lib/libeos_rust_abi.a
-Wl,-Bdynamic <resolved-root>/eos/martos/lib/libmartos_app.so
<resolved-root>/eos/martos/lib/libmartos_c++.a
-Wl,-u,__cxa_begin_cleanup -Wl,-u,__cxa_call_unexpected
<resolved-root>/eos/martos/lib/libmartos_c++abi.a
<resolved-root>/eos/martos/lib/libmartos_c.a
<absolute in-root softfp libgcc.a>
```

Rust objects, including the object defining `main`, remain before the runtime closure. No GCC crt
object is injected or accepted. Static, hard-float, non-PIC/non-PIE, relocatable, alternate
linker/script/interpreter, response-file, tool-prefix/specs/sysroot/plugin, crt, and ambiguous-
output attempts fail closed. Injected MARTOS operands are absolute resolved paths, so preserved
Rust search paths cannot shadow the pinned closure. A single resolved output is required beneath
the resolved current working directory. Consequently an external `CARGO_TARGET_DIR` outside the
invocation cwd is unsupported in release one.

The shared mode exists only to make the approved precompiled Rust sysroot possible. It requires
exactly one `-shared`, an output named `libstd-<16 lowercase hex>.so`, a single exactly matching
soname, one real cwd-contained rustc version script, `--no-undefined-version`, `symbols.o`,
`rmeta.o`, at least one rlib, the observed Bstatic-to-Bdynamic transition, `-lgcc`, and
`-nodefaultlibs`. It emits only the pinned target flags, `-fPIC -nostdlib`, rustc's unchanged argv,
and the selected absolute libgcc. It does not inject the application PIE, ENTRY/main script,
interpreter, EOS ABI archive, MARTOS application closure, authentication, or application
validation. This is a build-time sysroot artifact path, not support for arbitrary EOS DSOs.

## Linker script and EHABI closure

The script is derived from MARTOS-SMP 14.0.39's application script. It preserves PHDR, interpreter,
RX and RW load, dynamic, and ARM EXIDX program headers; uses `ENTRY(main)`; retains
`.ARM.extab*`, `.ARM.exidx*`, `.gcc_except_table*`, and init/fini/preinit arrays under garbage
collection; and exports `__exidx_start`/`__exidx_end` for ARM GNU fallback lookup. Native `.tdata`
or `.tbss` makes the real link fail via the script assertion.

The final wrapper leaves `-z defs` enabled. Exact archive evidence, narrow force extraction, the
two weak zero fallbacks, and the backtrace ARM macro implementation collectively close EHABI
without a broader relocation allowlist, blanket archive extraction, native TLS, or a new public
native ABI.

## ELF validator threat model

Before invoking external tools, the validator captures the bytes once and checks ELF magic,
ELF32/little-endian/current-version, ARM, `ET_DYN`, EABI5 softfp flags, exact ELF header sizes,
bounded program/section tables, and every file-backed segment and section extent. Marker-free mode
rejects a structurally exact trailer suffix. Allowed-trailer mode accepts the marker only at the
exact 129-byte suffix, followed by 64 lowercase digest characters and one newline, and verifies
the digest over exactly the preceding bytes. Marker text embedded elsewhere is ordinary ELF data.

Only root-resolved executable ARM GNU `readelf` and `nm` are invoked, with argv arrays rather than
a shell, closed stdin, a minimal `PATH`/C-locale environment, timeouts, strict UTF-8, return-code
checks, and a 32 MiB output limit. They inspect a private, read-only, fsynced snapshot containing
exactly the internally checked base bytes, not the caller-controlled pathname; snapshot bytes are
verified again after inspection. Separate `readelf -hW/-lW/-SW/-rW/-dW/-AW/-Ws` and `nm -n`
results are parsed fail closed. The application contract requires:

- ELF32 little-endian ARM EABI5 softfp `ET_DYN`, ARMv7 Application profile, A32/Thumb-2, VFPv3,
  and NEONv1;
- entry address resolved by both symbol-table and nm evidence to a defined global/default `main`;
- `/usr/lib/ld.so.1`, RX and RW loads, dynamic and ARM EXIDX headers, allocated nonempty exidx,
  and allocated extab when emitted;
- PIE, exactly the expected `libmartos_app.so.1.0` dependency, no TEXTREL/PT_TLS/nonempty native
  TLS, and only `R_ARM_RELATIVE`, `R_ARM_ABS32`, or `R_ARM_JUMP_SLOT` dynamic relocations;
- every undefined dynamic global/weak symbol in the reviewed manifest.

The manifest contains 95 symbols. In the real final application all 95 undefined dynamic symbols
exactly match it, and independent provider inspection proves all 95 are defined exports of the
pinned `libmartos_app.so.1.0`. The list covers reviewed direct EOS ABI calls plus transitive calls
from the selected MARTOS static C/C++ runtime objects; it was not widened for unwind hooks.

## Authentication threat model

The packager rejects missing/non-regular/non-ELF/already-trailed input, invalid ELF extents, same
input/output, output outside cwd, and validator symlink escape before publishing output. It
captures the opened source bytes and permission mode once, validates a private read-only snapshot
of exactly those bytes, hashes those same bytes with SHA-256, and appends exactly:

```text
martos_smp_elf_authentication_block_sha2_256_adbc_1394_e532_101\n
<64 lowercase hexadecimal SHA-256 characters>\n
```

It validates another private snapshot of the exact authenticated bytes, using a minimal subprocess
environment with the resolved SDK root. It then writes a same-directory temporary file, flushes
and fsyncs it, preserves captured input permission bits, verifies the bytes, and only then
atomically replaces the requested output. Final bytes are checked after publication. Concurrent
replacement of the source pathname cannot change either the validated or published bytes. The
trailer is exactly 129 bytes and does not rewrite any hashed ELF byte.

## Real asset and artifact evidence

The real verification root was assembled under `/tmp/eos-task15-stage.tdg9Vz/sdk` from:

- `/home/dev/code/arm-toolchain-build/custom-arm-libs`, whose compiler reports GCC 14.3.1
  (ARM GNU 14.3.Rel1) and binutils 2.44;
- `/home/dev/code/gpt-test/lib/martos-smp-14.0.39`, including the pinned linker-script baseline,
  headers, `libmartos_app.so.1.0`, and static C/C++ runtime archives;
- an ARM `libeos_rust_abi.a` compiled from the current MARTOS port with the pinned compiler and
  headers. CMake is unavailable on this host, so the existing single amalgamated translation unit
  was compiled directly; its 120/120 stable exports pass the current export checker.

All staged root files are real copies, not symlinks. Source-tree tools use
`EOS_RUST_SDK_ROOT` to target that root; this does not confuse the source tree with the packaged
script-relative default.

The exact final bootstrap/application sequence produced these results:

- `./x build library/std --target armv7a-unknown-eos-eabi`: passed after the cfg correction in
  2:25 in the final review rerun. The installed `libstd-0e630290a4d2acf4.so` is ELF32
  little-endian ARM EABI5 softfp
  `ET_DYN`, has RX/RW loads, DYNAMIC and ARM_EXIDX, no PT_TLS or TEXTREL, and the exact soname.
  Its 113 undefineds and broader shared-object relocation set were inspected separately; the
  application validator is intentionally not applied to this build-time sysroot DSO.
- Exact Task 14 unwind Cargo build with source stage-1 rustc: forced a fresh link and passed in
  49.64 seconds.
- Final unwind application: 1,821,648-byte ELF32 ARM EABI5 softfp `ET_DYN` PIE, global `main`
  entry `0x239ac`, interpreter, RX/RW, DYNAMIC, ARM_EXIDX, allocated `.ARM.extab` size `0x683c`
  and `.ARM.exidx` size `0x3710`, PIE/NEEDED contract, 1,934 `R_ARM_RELATIVE` plus 95
  `R_ARM_JUMP_SLOT`, and no other dynamic relocation, TLS, TEXTREL, unresolved `_Unwind_*`,
  `__aeabi_unwind*`, `__gnu_Unwind*`, or personality dependency.
- The final application proves `_end == __bss_end__ == 0x0f94d0` after the attributes-placement
  correction.
- The authenticated output is 1,821,777 bytes, exactly 129 bytes larger, and passes
  `eos-elf-validate --allow-auth-trailer`.
- The Task 14 FFI-containment staticlib also builds against the installed target sysroot.

## Final verification

The final verification set is:

```text
python3 -m unittest discover -s src/tools/eos-sdk/tests -p 'test_*.py' -v
  Ran 42 tests ... OK

python3 -m unittest -v \
  tests.eos.host.test_ffi_unwind_policy \
  tests.eos.host.test_bootstrap_target \
  tests.eos.host.test_pal_cfg_scope \
  tests.eos.host.test_libc_links \
  tests.eos.host.test_libc_source_provenance \
  tests.eos.host.test_toolchain_lock
  Ran 37 tests ... OK

python3 src/tools/eos-abi/tests/check_exports_test.py ...
python3 src/tools/eos-abi/tests/check_exports.py \
  --nm <pinned-arm-nm> --archive /tmp/eos-task15-libeos_rust_abi.a \
  --expected src/tools/eos-abi/tests/expected-exports-v1.txt
  both exit 0; exact archive exports 120/120

PYTHONPYCACHEPREFIX=/tmp/eos-task15-pycache python3 -Wall -Werror -m py_compile \
  <all Task 15 Python files>
  exit 0
```

Real GNU subprocess boundary tests, real MARTOS sibling ELF parsing, exact bootstrap, exact Cargo
link, static readelf/nm inspection, base validation, authentication, and trailer validation all
run in addition to fake deterministic fixtures.

## Scope and concerns

Outer scope is the five Task 15 production assets, their independent tests/fixture/support, this
report, the narrow existing unwind-policy extension, and the backtrace gitlink. Nested scope is
exactly one cfg line. No target softfp/PIC/PIE setting, loader relocation allowlist, native TLS
policy, stable native export manifest, SDK/toolchain revision, application output constraint,
Task 16 file, or progress ledger is weakened or changed.

Remaining concerns are operational rather than hidden test failures:

- The nested backtrace commit must accompany the outer gitlink; it is not pushed here.
- The release-one output policy intentionally rejects an external Cargo target directory outside
  the wrapper's resolved cwd.
- The shared mode is only the exact observed rustc libstd form. A future rustc change to its
  hashed filename, version-script, or input convention must be reviewed and tested rather than
  accepted generically.
- Hardware loader/authentication and unwind execution still require the documented board matrix.
