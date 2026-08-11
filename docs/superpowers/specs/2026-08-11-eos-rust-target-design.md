# EOS Rust Target and Standard Library Design

**Date:** 2026-08-11  
**Status:** Approved design  
**Initial Rust baseline:** 1.97.1  
**Initial ARM GNU baseline:** 14.3.Rel1

## 1. Purpose

Build and distribute a Rust toolchain for EOS applications running on the ARM processing system in the Xilinx/AMD Zynq-7000 XC7Z030 and XC7Z045 devices. The toolchain must include a built-in target and a near-complete Rust standard library, integrate with the existing EOS application loader, and produce authenticated ELF32 position-independent executables.

The permanent target name is:

```text
armv7a-unknown-eos-eabi
```

`eos` is a stable technical ABI identifier. A future product rename does not rename this target or invalidate existing Rust artifacts. A new target name is introduced only for an incompatible EOS ABI.

## 2. Release-one scope

Release one includes:

- A maintained fork of Rust 1.97.1.
- One soft-float procedure-call ABI target optimized for the Cortex-A9.
- Hardware VFPv3-D32 and NEON instructions while preserving the current EOS `softfp` calling convention.
- Prebuilt `core`, `alloc`, `std`, `panic_unwind`, `proc_macro`, and `test` sysroot artifacts.
- A versioned native compatibility library named `libeos_rust_abi`.
- Reuse of Rust's Unix platform abstraction layer (PAL) with narrowly scoped EOS adaptations.
- ELF32 ARM EABI5 `ET_DYN` PIE output compatible with the current EOS loader.
- Rust panic unwinding within Rust code and containment at ordinary C/C++ FFI boundaries.
- Strong non-cryptographic hash-table seed diversification without a cryptographic-randomness claim.
- Automated compile, ABI, host-contract, and static ELF tests.
- A manual release test matrix on both XC7Z030 and XC7Z045 hardware.

Release one excludes:

- The hard-float procedure-call ABI target. It remains a future sibling target and requires hard-float builds of every native library crossing the ABI boundary.
- Unrestricted Rust panic unwinding across C/C++ frames.
- C++ exceptions entering Rust.
- A cryptographically secure `getrandom` service.
- Automatic deployment to physical boards.
- Renaming the permanent `eos` ABI identifier during a product rebrand.

## 3. Hardware and ABI baseline

Both supported Zynq-7000 device families use ARM Cortex-A9 processors implementing ARMv7-A. Their NEON media processing engines support Advanced SIMD and VFPv3. The target uses:

| Property | Required value |
| --- | --- |
| Architecture | 32-bit ARMv7-A |
| CPU | Cortex-A9 |
| Endianness | Little-endian |
| Primary instruction state | A32 with Thumb-2 interworking |
| Floating-point instructions | VFPv3-D32 and NEON enabled |
| Procedure-call float ABI | Soft, matching GCC `-mfloat-abi=softfp` |
| Pointer width | 32 bits |
| C `int` width | 32 bits |
| Maximum atomic width | 64 bits |
| C enum minimum width | 8 bits, matching `arm-none-eabi-gcc` |
| Panic strategy | Unwind |
| Relocation model | PIC |
| Final application ELF type | `ET_DYN` PIE |

The target must not enable LLVM's `+soft-float` feature: doing so would disable the required VFP/NEON instruction generation. It instead uses the soft procedure-call ABI together with Cortex-A9 VFPv3-D32 and NEON target features.

The initial target disables native ELF thread-local storage. Rust TLS is implemented over EOS thread-local keys until the EOS loader has a tested `PT_TLS` and ARM TLS relocation contract.

## 4. System architecture

The solution has three maintained components.

### 4.1 Pinned Rust fork

The Rust fork owns:

- The built-in `armv7a-unknown-eos-eabi` target definition.
- EOS target registration and target metadata.
- EOS definitions in the Rust `libc` dependency.
- Selection and small adaptations of the Unix PAL for `target_os = "eos"`.
- EOS runtime initialization and cleanup hooks.
- Standard-library and compiler conformance tests.
- Bootstrap configuration for building the host compiler and EOS sysroot.

The Rust target is in the Unix family so portable Unix-backed `std` functionality and applicable `std::os::unix` APIs are available. EOS-specific exceptions are explicit `target_os = "eos"` branches; they are not hidden behind unrelated target configurations.

### 4.2 `libeos_rust_abi`

`libeos_rust_abi` is the stable boundary between Rust and EOS. It translates the POSIX-shaped operations needed by Rust's Unix PAL into the current EOS `os_*` services.

Its public ABI follows these rules:

- Every exported symbol has an `eos_rust_` prefix.
- Public integers use fixed-width types.
- EOS-private structures, enum layouts, and kernel pointers do not cross the boundary.
- Resources cross the boundary as validated opaque handles or compatibility-layer descriptors.
- Every fallible operation has a documented success value and stable error contract.
- The library exports an ABI major/minor version query.
- A major mismatch prevents application startup with a clear diagnostic.
- Backward-compatible additions increment the minor version.
- Breaking changes require a new major version and, if they change the application ABI, a new Rust target identifier.

When EOS gains a POSIX-like libc, the implementation of this library can forward to that libc. Its public ABI and Rust consumers remain unchanged.

### 4.3 EOS Rust SDK

The SDK packages:

- The host `rustc`, Cargo, rustdoc, and associated host tools.
- The prebuilt EOS sysroot.
- `libeos_rust_abi` and its development metadata.
- The EOS linker wrapper and application linker script.
- Required ARM EHABI unwind support from the pinned ARM GNU toolchain.
- Cargo configuration templates.
- ELF validation and authentication-packaging tools.
- Examples and the manual board-test bundle.
- Toolchain, ABI, capability, license, and source-revision manifests.

Normal application builds use the packaged target with ordinary Cargo commands. They do not require target JSON files or `-Z build-std`.

## 5. Rust target definition

The Rust target definition uses `armv7a-unknown-none-eabi` as the LLVM code-generation triple while exposing `armv7a-unknown-eos-eabi` as the Rust target name. Rust target options identify EOS as the operating system and EABI as the ABI.

Required target behaviors include:

- `cpu = "cortex-a9"`.
- ARMv7-A, Thumb-2 interworking, VFPv3-D32, and NEON features.
- Soft LLVM float ABI without the `+soft-float` code-generation feature.
- GNU C-compiler linker flavor routed through the EOS linker wrapper.
- Dynamic linking and executables enabled.
- Position-independent executables enabled.
- PIC relocation model.
- Unwind panic strategy.
- 64-bit maximum atomics.
- Small C enum compatibility.
- Native TLS disabled.
- Unix target family and `target_os = "eos"`.

Compiler tests compare emitted attributes and calling conventions against C objects compiled with:

```text
-march=armv7-a
-mtune=cortex-a9
-mfpu=neon-vfpv3
-mfloat-abi=softfp
-fPIC
```

## 6. Link and artifact contract

The build pipeline is:

```text
Cargo
  -> EOS rustc and prebuilt sysroot
  -> Rust and native objects
  -> EOS linker wrapper
  -> ELF32 ARM EABI5 ET_DYN PIE
  -> ELF validation
  -> EOS authentication trailer and package
```

The linker wrapper centralizes SDK paths and product-specific flags. It invokes the pinned ARM GNU linker driver with:

- The EOS application linker script.
- `-pie`.
- Section garbage collection.
- EOS startup and termination objects.
- `libeos_rust_abi`.
- Required EOS C, application, and support libraries.
- The selected ARM EHABI unwinder and GCC support library.
- The interpreter and shared-library conventions required by the EOS loader.

The final `.elf` is not an `ET_REL` object. It is an `ET_DYN` position-independent executable containing dynamic relocations so the EOS loader can place it at different addresses.

Before authentication data is appended, validation must prove:

- ELF class is ELF32.
- Machine is ARM.
- Data encoding is little-endian.
- EABI version is 5.
- File type is `ET_DYN`.
- The ELF flags identify the soft-float ABI.
- ARM attributes are compatible with ARMv7-A, VFPv3-D32, and NEON.
- Expected load, dynamic, and interpreter program headers exist.
- All runtime relocations are supported by the EOS loader.
- `.ARM.exidx` and any `.ARM.extab` data are allocated and retained.
- No text relocations exist.
- Undefined dynamic symbols are in an approved allowlist and have a providing EOS library.
- Required shared-library dependencies are recorded.
- The image has at least one valid loadable segment and non-marker content.

Authentication packaging runs only after validation. It appends the EOS authentication identifier and digest without rewriting bytes covered by the digest. The packager rejects a marker-only file.

## 7. Standard-library strategy

The target reuses Rust's Unix PAL. The EOS `libc` target module supplies the constants, types, and extern declarations expected by that PAL, mapping calls to `eos_rust_*` symbols where EOS does not natively export the corresponding POSIX symbol.

Direct use of private EOS `os_*` symbols from Rust `std` is prohibited. All such access belongs in `libeos_rust_abi`.

### 7.1 Memory

The compatibility ABI provides allocation, zeroed allocation, reallocation, aligned allocation, and free. It defines zero-size behavior, maximum alignment, overflow handling, and allocation-failure reporting. The Rust global allocator uses this ABI unless a later SDK configuration deliberately selects another allocator.

### 7.2 Files and descriptors

The compatibility library owns a process-local descriptor table that represents:

- EOS filesystem objects.
- Standard input, output, and error streams.
- Sockets.
- Pipes and process redirection endpoints.

The table provides validated allocation, close, duplication, close-on-exec state, descriptor-kind checks, and race-safe lifetime management. It supports sequential and positional I/O, seek, flush, metadata, directory iteration, creation, removal, rename, canonicalization, and permissions wherever the EOS filesystem has equivalent semantics.

### 7.3 Threads, synchronization, and TLS

The ABI provides thread create, join, detach, yield, sleep, name, stack size, and controlled termination. Rust-created threads begin in a containment trampoline and run TLS destructors before native thread destruction.

Synchronization covers mutexes, recursive mutexes where Rust requires them, condition variables, reader/writer locks, once initialization, and thread parking. Timeout operations use an absolute monotonic deadline internally to avoid cumulative conversion error.

Rust TLS uses one reserved EOS user-TLS slot per thread. That slot points to a compatibility-layer TLS root containing a dynamic key map, values, and destructor state. TLS destructor iteration follows Rust/Unix expectations and prevents unbounded reinstallation loops.

### 7.4 Time

The PAL exposes distinct monotonic and realtime clocks. Monotonic time never uses the wall clock and does not move backward. All tick-to-duration conversions use checked arithmetic and documented rounding. Realtime values preserve the EOS clock's accuracy and update state without affecting timeout calculations.

### 7.5 Networking

The compatibility ABI maps EOS networking services to the socket operations required by `std::net`: TCP and UDP creation, bind, connect, listen, accept, send, receive, shutdown, options, address conversion, polling, and DNS resolution. Nonblocking state and timeout behavior are properties of the compatibility descriptor rather than assumptions about an EOS handle.

### 7.6 Processes

The process ABI supports argument vectors, environment, working directory, redirected standard streams, spawn, wait/status, and termination. It preserves the distinction between loader failure, child failure, signal-like EOS termination, and a normal exit code. Descriptor inheritance follows close-on-exec state.

### 7.7 Runtime services

The PAL supplies arguments, environment access, current-directory operations, exit, abort, OS error strings, runtime initialization, and runtime cleanup. Standard descriptors are validated during initialization so later file opens cannot accidentally become standard I/O.

### 7.8 Capability policy

The SDK publishes a machine-readable capability matrix. If EOS cannot represent an operation, the PAL returns `Unsupported` or the documented native error. It never reports false success.

The initial capability audit explicitly covers symbolic and hard links, ownership and group operations, permission-bit fidelity, signals, memory mapping and protection, process replacement, dynamic loading, IPv6, multicast, and filesystem durability. An item is marked supported only after both a contract test and a board test pass.

No public `std::os::eos` API is added in release one. Portable `std` and applicable `std::os::unix` APIs take priority.

## 8. Error model

One reviewed translation table maps EOS status values and native networking/process errors to stable `errno` values. The implementation preserves these distinctions:

- End of file.
- Interrupted operation.
- Would block.
- Timeout.
- Connection closure.
- Connection refusal or reset.
- Invalid input.
- Permission denial.
- Missing resource.
- Resource exhaustion.
- Unsupported operation.

Partial reads and writes return the completed byte count. An error is returned only when no stronger partial-progress contract applies.

The compatibility layer maintains thread-local last-error state behind the EOS TLS adapter. It does not rely on the currently exported global `errno`. Unknown EOS statuses map to `EIO`; debug SDK builds also record the original status and operation name without changing release behavior.

## 9. Panic unwinding and backtraces

Release one supports Rust panic unwinding within Rust code. The SDK links the ARM EHABI unwinder from the pinned ARM GNU toolchain, satisfying the `_Unwind_*` interface already referenced by the EOS C++ ABI library.

The unwind contract is:

- Rust code is compiled with `panic=unwind`.
- Rust frames retain `.ARM.exidx` and `.ARM.extab` as required.
- `catch_unwind` catches Rust panics within Rust code.
- Every Rust entry point called through an ordinary EOS/C ABI has a containment wrapper.
- Containment converts a panic into the API's documented EOS failure, controlled task termination, or abort.
- EOS-created thread roots contain any otherwise uncaught Rust panic.
- Panics do not cross ordinary `extern "C"` boundaries.
- `extern "C-unwind"` is prohibited in release-one production code.
- C++ exceptions do not enter Rust frames.
- A panic during panic handling or destructor cleanup aborts.

Rust FFI code is built with `-D ffi-unwind-calls`. Repository checks also scan for unaudited `C-unwind` declarations.

`std::backtrace` initially guarantees frame-address capture for unwindable Rust frames. On-target symbolization is optional. The SDK retains an unstripped ELF and debug information for offline symbolization and publishes the exact build identifier needed to match a board trace to its ELF.

## 10. Randomness policy

The Zynq-7000 processing system has no built-in application-layer hardware RNG. Release one cannot require a programmable-logic TRNG and cannot rely on a persistent boot seed.

Therefore release one does not provide or claim cryptographically secure random output. It provides only strong non-cryptographic seed diversification for Rust hash tables. The EOS `std::sys::random` implementation supplies a dedicated `hashmap_random_keys()` path and does not route it through a fictitious secure byte generator. General random-byte filling reports that secure randomness is unsupported.

The seed service:

- Exports `eos_rust_hash_seed(uint64_t *key0, uint64_t *key1)`.
- Initializes process-local state from high-resolution timing, scheduler and interrupt timing observations, boot timing, application identity, address-layout variation, and fixed device diversification data.
- Serializes state updates, advances a 64-bit invocation counter, and applies domain-separated SplitMix64 finalization to produce the two keys.
- Treats fixed identifiers and ordinary timestamps as diversification, not credited entropy.
- Generates distinct per-process and per-request hash seeds under normal operation.
- Documents that an attacker capable of observing or controlling boot and event timing may predict seeds.
- Is tested for state separation, concurrency, non-repetition under varied inputs, and deterministic known-answer behavior with an injected test source.

A future EOS `getrandom` service remains a separate kernel subproject. Its stable API can later be consumed by `libeos_rust_abi`. A PL TRNG, external entropy device, or persistent seed can be added without changing the Rust target.

## 11. Testing strategy

### 11.1 Host contract tests

`libeos_rust_abi` is built against a mock EOS backend on the host. Tests cover:

- Descriptor allocation, reuse, duplication, and concurrent close/use races.
- File, socket, pipe, and standard-stream dispatch.
- Partial I/O and every error translation.
- Thread creation and teardown.
- TLS key allocation, isolation, destructor order, and destructor iteration.
- Mutex, condition-variable, reader/writer-lock, once, and parking behavior.
- Monotonic deadlines, realtime conversion, overflow, and rounding.
- Process state and descriptor inheritance.
- Hash-seed state separation.
- Panic-containment wrapper behavior.
- ABI version negotiation.

### 11.2 Compiler and ABI tests

The Rust fork tests:

- Target-list registration and target metadata.
- `cfg` values for architecture, OS, family, ABI, endianness, pointer width, and atomics.
- Data layout and C-compatible primitive/enum/aggregate layout.
- VFPv3-D32/NEON code generation with the soft procedure-call ABI.
- Calls in both directions between Rust and C fixtures.
- PIC generation and absence of unsupported relocations.
- Successful builds of `core`, `alloc`, `std`, `panic_unwind`, `proc_macro`, and `test`.
- Successful use of the prebuilt sysroot without `-Z build-std`.

### 11.3 Static ELF tests

Representative applications cover hello-world, allocation, filesystem, threads/TLS, networking, processes, unwinding, and FFI containment. `readelf`, `nm`, and `objdump` assertions enforce the artifact contract in section 6.

Static checks also verify that authentication packaging rejects a missing base ELF and that an authenticated image still parses as the same ELF with only the approved trailer appended.

### 11.4 Manual board tests

Every SDK release records a manual run on both XC7Z030 and XC7Z045 systems. The suite covers:

- Loading the same PIE at different addresses.
- Arguments, environment, working directory, and standard streams.
- Allocation and collection stress.
- File and directory operations.
- Threads, TLS, synchronization, parking, and once initialization.
- Monotonic/realtime clocks and timeout accuracy.
- TCP/UDP networking and DNS.
- Child process spawn, redirected I/O, wait, status, and termination.
- Rust panic catch, destructor execution, FFI containment, and backtrace capture.
- Debug and optimized builds.

The release record includes board model, EOS revision, Rust fork revision, ARM GNU revision, shim ABI version, linker-script revision, application build identifiers, capability results, and observed failures.

## 12. Release and maintenance policy

Rust 1.97.1 and ARM GNU 14.3.Rel1 are pinned for initial development. The SDK manifest records exact source revisions and hashes rather than relying only on human-readable version strings.

Rust upgrades occur on a dedicated branch. An upgrade is accepted only after:

1. The Rust patch set is rebased and reviewed.
2. Host, compiler, ABI, and static ELF tests pass.
3. Standard-library API and capability changes are reviewed.
4. The manual board matrix passes on XC7Z030 and XC7Z045.
5. SDK and ABI manifests are regenerated.

The target remains internal until its ABI, documentation, and continuous testing meet the Rust project's requirements for an upstream target proposal. Upstreaming is optional and does not block the internal SDK.

## 13. Acceptance criteria

The initial project is complete when all of the following are true:

- The custom SDK reports `armv7a-unknown-eos-eabi` as a built-in target.
- A normal Cargo application builds against a precompiled EOS `std` without target JSON or `-Z build-std`.
- Rust/C fixtures prove Cortex-A9 softfp ABI compatibility.
- The final application is an ELF32 little-endian ARM EABI5 `ET_DYN` PIE accepted by the EOS loader.
- The final image preserves required dynamic relocations and ARM EHABI unwind metadata.
- The authentication packager rejects marker-only output and packages a validated ELF.
- The supported `std` capability matrix passes its automated tests.
- Rust panic unwinding, `catch_unwind`, destructor cleanup, and ordinary-FFI containment pass on hardware.
- Unsupported operations return explicit errors.
- The non-cryptographic randomness limitation is visible in SDK documentation and release metadata.
- The full manual suite passes on both XC7Z030 and XC7Z045.

## 14. Supporting evidence

- AMD documents ARMv7-A NEON and VFPv3 support for the Zynq-7000 Cortex-A9 processing system: <https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/NEON>.
- AMD states that Zynq-7000 has no built-in application-layer RNG and that a generator can instead be implemented in programmable logic: <https://docs.amd.com/api/khub/documents/~r0Z3L3XXjpo2pmVrIvd_g/content>.
- Rust documents that custom target JSON is unstable and must be compiler-version-pinned: <https://doc.rust-lang.org/nightly/rustc/targets/custom.html>.
- The inspected EOS sibling application `msg_server.elf` is ELF32 ARM EABI5, soft-float ABI, `ET_DYN`, PIE, and dynamically relocated. Its source path during design was `/home/dev/code/gpt-test/lib/martos-smp-14.0.39/bin/msg_server.elf`.
- The supplied `gpttest.elf` contained only the EOS authentication marker, demonstrating the need to reject packaging without a valid base ELF. Its source path during design was `/home/dev/code/gpt-test/apps/gpttest/deploy/gpttest-0.0.1/gpttest.elf`.
