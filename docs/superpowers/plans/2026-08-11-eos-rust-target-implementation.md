# EOS Rust Target and Standard Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a pinned Rust 1.97.1 SDK whose built-in `armv7a-unknown-eos-eabi` target builds ordinary Cargo applications with a precompiled, near-complete `std` and produces authenticated ELF32 ARM EABI5 `ET_DYN` PIE images for both XC7Z030 and XC7Z045 boards.

**Architecture:** Maintain one Rust fork containing the compiler target, an EOS-specific `libc` module, narrow Unix PAL adaptations, a versioned native `libeos_rust_abi` translation layer, and SDK/link/validation tools. The public native boundary contains only fixed-layout `eos_rust_*` symbols; the current MARTOS-SMP 14.0.39 SDK is one private port beneath that boundary, so a future POSIX libc can replace the port without renaming the Rust target or changing application ABI.

**Tech Stack:** Rust 1.97.1 (`8bab26f4f68e0e26f0bb7960be334d5b520ea452`), `libc` 0.2.185 (`71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8`), C11, CMake/CTest, Python 3 standard library, ARM GNU 14.3.Rel1, GNU `readelf`/`nm`/`objdump`, Cargo, Rust bootstrap (`x.py`).

## Global Constraints

- `eos` is the permanent technical ABI identifier. The v1 target is exactly `armv7a-unknown-eos-eabi`; product naming never changes it.
- XC7Z030 and XC7Z045 are equal first-class deployment targets. They share one Cortex-A9 binary target and each must pass the complete manual release matrix.
- Generate A32 ARMv7-A PIC with Thumb-2 interworking, Cortex-A9 tuning, VFPv3-D32, and NEON. Use the soft procedure-call float ABI matching `-mfloat-abi=softfp`; do not enable LLVM `+soft-float` and do not add a hard-float target in v1.
- Final applications are ELF32 little-endian ARM EABI5 `ET_DYN` PIE files, not `ET_REL` files. Initial runtime relocation allowlist is `R_ARM_RELATIVE`, `R_ARM_ABS32`, and `R_ARM_JUMP_SLOT`, the set proven by the inspected EOS sibling image.
- Native ELF TLS remains disabled. Reserve EOS user TLS slot index 7 for one `libeos_rust_abi` TLS root; assert at compile time that `OS_THREAD_USER_TLS_CNT > 7`.
- Rust panics unwind only through Rust frames. Every ordinary C/EOS entry point catches the panic before returning or terminates the task. Production code rejects unaudited `extern "C-unwind"` and builds with `-Dffi-unwind-calls`.
- HashMap receives strong non-cryptographic seed diversification through `eos_rust_hash_seed`. General secure random-byte generation remains unsupported and must panic through Rust's unsupported random backend. Do not describe this as entropy or cryptographic randomness.
- The production port may include `martos_smp.h` and the shipped EOS C headers, but must not copy proprietary SDK headers into this repository. Configure their location with `EOS_SDK_ROOT`.
- Current MARTOS-SMP 14.0.39 services are the first port baseline. Missing native capabilities return `ENOTSUP` and stay `false` in `capabilities.toml`; neither the shim nor PAL may report false success.
- CI never deploys to a board. It performs host contract tests, cross compilation, ABI comparison, and static ELF inspection. Human operators deploy manually and record results for both part families.
- Each task follows red/green discipline: add the smallest failing test, run it and preserve the expected failure in the work log, implement only that contract, rerun the focused test, then run the listed regression command before committing.
- Never widen the native ABI, relocation allowlist, unwind boundary, or capability matrix merely to make a test pass. Such changes require evidence from EOS headers or both boards and a design review.

---

## Repository Layout Produced by This Plan

```text
compiler/rustc_target/src/spec/targets/armv7a_unknown_eos_eabi.rs
library/std/src/sys/random/eos.rs
library/std/src/sys/process/unix/eos.rs
src/doc/rustc/src/platform-support/armv7a-unknown-eos-eabi.md
src/tools/eos-abi/
  CMakeLists.txt
  include/eos_rust_abi.h
  include/eos_rust_abi_version.h
  src/
  ports/host/
  ports/martos_14_0_39/
  tests/
src/tools/eos-libc/
src/tools/eos-sdk/
  bin/
  linker/app_linker_script.ld
  manifests/
  templates/
  tests/
tests/eos/
  abi/
  apps/
  board/
  host/
docs/eos/
```

The current integration repository contains the approved design as commit `1cc913c`. Implementation work starts in an isolated worktree and imports the Rust 1.97.1 tree without rewriting that commit.

## Stable Native Contract Snapshot

The public header added in Task 3 is authoritative. Its v1 types use only C11 fixed-width fields:

```c
typedef int32_t eos_rust_fd_t;
typedef uint32_t eos_rust_thread_t;
typedef uint32_t eos_rust_tls_key_t;

typedef struct eos_rust_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} eos_rust_timespec;

typedef struct eos_rust_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    int64_t st_atime_sec;
    int64_t st_atime_nsec;
    int64_t st_mtime_sec;
    int64_t st_mtime_nsec;
    int64_t st_ctime_sec;
    int64_t st_ctime_nsec;
    uint64_t st_blocks;
    uint32_t st_blksize;
    uint32_t reserved[7];
} eos_rust_stat;
```

Opaque pthread-compatible values are fixed arrays whose all-zero representation is a valid lazy initializer. Native EOS pointers never appear in these public values:

```c
typedef struct eos_rust_pthread_attr { uint32_t words[4]; } eos_rust_pthread_attr;
typedef struct eos_rust_pthread_mutex { uint32_t words[4]; } eos_rust_pthread_mutex;
typedef struct eos_rust_pthread_mutexattr { uint32_t words[2]; } eos_rust_pthread_mutexattr;
typedef struct eos_rust_pthread_cond { uint32_t words[4]; } eos_rust_pthread_cond;
typedef struct eos_rust_pthread_condattr { uint32_t words[2]; } eos_rust_pthread_condattr;
typedef struct eos_rust_pthread_rwlock { uint32_t words[4]; } eos_rust_pthread_rwlock;
```

Public symbol groups are version/error, allocation/runtime, descriptors/files, threads/TLS, synchronization/time, networking, processes, and hash seeding. Every symbol begins `eos_rust_`; `src/tools/eos-abi/tests/expected-exports-v1.txt` rejects accidental exports.

---

### Task 1: Establish the Pinned Rust Fork and Reproducibility Manifest

**Files:**

- Preserve: `docs/superpowers/specs/2026-08-11-eos-rust-target-design.md`
- Create: `src/tools/eos-sdk/manifests/toolchain.lock.toml`
- Create: `tests/eos/host/test_toolchain_lock.py`
- Create: `docs/eos/source-baselines.md`

- [ ] **Step 1: Create the isolated Rust worktree**

  From the integration repository, use the worktree skill and run:

  ```bash
  git remote add rust-upstream https://github.com/rust-lang/rust.git
  git fetch rust-upstream refs/tags/1.97.1:refs/tags/rust-1.97.1
  git worktree add ../eos-rust-1.97.1 -b codex/eos-rust-target master
  cd ../eos-rust-1.97.1
  git merge --allow-unrelated-histories --no-edit rust-1.97.1
  git merge-base --is-ancestor 8bab26f4f68e0e26f0bb7960be334d5b520ea452 HEAD
  ```

  The last command must exit zero, and the approved design file must remain present.

- [ ] **Step 2: Add the failing lock test**

  Write `test_toolchain_lock.py` to load TOML with `tomllib` and require these exact values:

  ```python
  EXPECTED = {
      ("rust", "version"): "1.97.1",
      ("rust", "upstream_commit"): "8bab26f4f68e0e26f0bb7960be334d5b520ea452",
      ("libc", "version"): "0.2.185",
      ("libc", "upstream_commit"): "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8",
      ("arm_gnu", "release"): "14.3.Rel1",
      ("eos_sdk", "baseline"): "MARTOS-SMP-14.0.39",
      ("eos_rust_abi", "major"): 1,
      ("eos_rust_abi", "minor"): 0,
  }
  ```

- [ ] **Step 3: Run the test and confirm the expected failure**

  ```bash
  python3 -m unittest tests/eos/host/test_toolchain_lock.py -v
  ```

  Expected: failure because `toolchain.lock.toml` does not exist.

- [ ] **Step 4: Create the lock and baseline documentation**

  Add the exact values above. Document that package generation records SHA-256 digests for the installed ARM toolchain and EOS SDK archive in a generated release manifest; the source lock pins semantic releases and upstream commits.

- [ ] **Step 5: Verify and commit**

  ```bash
  python3 -m unittest tests/eos/host/test_toolchain_lock.py -v
  git diff --check
  git add src/tools/eos-sdk/manifests/toolchain.lock.toml tests/eos/host/test_toolchain_lock.py docs/eos/source-baselines.md
  git commit -m "build: pin EOS Rust toolchain baselines"
  ```

---

### Task 2: Add the Built-in `armv7a-unknown-eos-eabi` Compiler Target

**Files:**

- Modify: `compiler/rustc_target/src/spec/mod.rs`
- Create: `compiler/rustc_target/src/spec/targets/armv7a_unknown_eos_eabi.rs`
- Modify: `tests/assembly-llvm/targets/targets-elf.rs`
- Create: `tests/ui/target-cfg/eos.rs`
- Modify: `src/doc/rustc/src/platform-support.md`
- Create: `src/doc/rustc/src/platform-support/armv7a-unknown-eos-eabi.md`

- [ ] **Step 1: Add failing target references**

  Add the `armv7a_unknown_eos_eabi` revision to `targets-elf.rs` and a check-pass UI test with these directives and assertions:

  ```rust
  //@ check-pass
  //@ compile-flags: --target armv7a-unknown-eos-eabi
  //@ needs-llvm-components: arm

  #[cfg(not(all(
      target_arch = "arm",
      target_os = "eos",
      target_family = "unix",
      target_abi = "eabi",
      target_endian = "little",
      target_pointer_width = "32"
  )))]
  compile_error!("incorrect EOS target cfg");
  fn main() {}
  ```

- [ ] **Step 2: Run the focused test and confirm the target is unknown**

  ```bash
  ./x test tests/assembly-llvm/targets/targets-elf.rs tests/ui/target-cfg/eos.rs
  ```

  Expected: rustc reports that `armv7a-unknown-eos-eabi` is not a built-in target.

- [ ] **Step 3: Register the OS and target**

  Add `Eos = "eos"` to `Os`, register `("armv7a-unknown-eos-eabi", armv7a_unknown_eos_eabi)` in `supported_targets!`, and implement:

  ```rust
  use crate::spec::{
      Arch, Cc, CfgAbi, FloatAbi, FramePointer, LinkerFlavor, Lld, Os,
      PanicStrategy, RelocModel, Target, TargetMetadata, TargetOptions, cvs,
  };

  pub(crate) fn target() -> Target {
      Target {
          llvm_target: "armv7a-unknown-none-eabi".into(),
          metadata: TargetMetadata {
              description: Some("ARMv7-A Cortex-A9 with EOS".into()),
              tier: Some(3),
              host_tools: Some(false),
              std: Some(true),
          },
          pointer_width: 32,
          data_layout: "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64".into(),
          arch: Arch::Arm,
          options: TargetOptions {
              os: Os::Eos,
              families: cvs!["unix"],
              cfg_abi: CfgAbi::Eabi,
              linker: Some("eos-rust-link".into()),
              linker_flavor: LinkerFlavor::Gnu(Cc::Yes, Lld::No),
              cpu: "cortex-a9".into(),
              features: "+v7,+thumb2,+vfp3,+neon,+strict-align".into(),
              llvm_floatabi: Some(FloatAbi::Soft),
              dynamic_linking: true,
              position_independent_executables: true,
              relocation_model: RelocModel::Pic,
              panic_strategy: PanicStrategy::Unwind,
              default_uwtable: true,
              frame_pointer: FramePointer::Always,
              max_atomic_width: Some(64),
              c_enum_min_bits: Some(8),
              has_thread_local: false,
              has_thumb_interworking: true,
              emit_debug_gdb_scripts: false,
              ..Default::default()
          },
      }
  }
  ```

  Keep `+soft-float` absent. `+neon` implies VFPv3-D32 in LLVM; the assembly test must show NEON/VFP code while the ABI remains soft.

- [ ] **Step 4: Document internal Tier 3 support**

  The platform page must state the two supported Zynq part families, A32/softfp contract, native-TLS limitation, manual board testing, SDK linker requirement, and the fact that the target is maintained in this fork rather than promised by upstream Rust.

- [ ] **Step 5: Verify compiler consistency and code generation**

  ```bash
  ./x test compiler/rustc_target
  ./x test tests/assembly-llvm/targets/targets-elf.rs tests/ui/target-cfg/eos.rs
  ./build/host/stage1/bin/rustc --print target-list | rg '^armv7a-unknown-eos-eabi$'
  ./build/host/stage1/bin/rustc --print cfg --target armv7a-unknown-eos-eabi
  ```

- [ ] **Step 6: Commit**

  ```bash
  git add compiler/rustc_target tests/assembly-llvm/targets/targets-elf.rs tests/ui/target-cfg src/doc/rustc/src/platform-support.md src/doc/rustc/src/platform-support/armv7a-unknown-eos-eabi.md
  git commit -m "compiler: add built-in EOS Cortex-A9 target"
  ```

---

### Task 3: Establish `libeos_rust_abi` Versioning, Error Rules, and Port Boundary

**Files:**

- Create: `src/tools/eos-abi/CMakeLists.txt`
- Create: `src/tools/eos-abi/include/eos_rust_abi_version.h`
- Create: `src/tools/eos-abi/include/eos_rust_abi.h`
- Create: `src/tools/eos-abi/src/eos_abi.c`
- Create: `src/tools/eos-abi/src/eos_error.c`
- Create: `src/tools/eos-abi/src/eos_error.h`
- Create: `src/tools/eos-abi/src/eos_port.h`
- Create: `src/tools/eos-abi/ports/host/eos_port_host.c`
- Create: `src/tools/eos-abi/ports/martos_14_0_39/eos_port_martos.c`
- Create: `src/tools/eos-abi/tests/abi_contract_test.cpp`
- Create: `src/tools/eos-abi/tests/error_map_test.cpp`
- Create: `src/tools/eos-abi/tests/expected-exports-v1.txt`
- Create: `src/tools/eos-abi/tests/check_exports.py`

- [ ] **Step 1: Write failing ABI and error tests**

  Require `(major << 16) | minor == 0x0001_0000`, accept required minor `0`, reject major `2`, preserve errno per host thread, map every `os_status` enumerator from MARTOS-SMP 14.0.39, and map unknown status values to `EIO`.

- [ ] **Step 2: Confirm the native test target is absent**

  ```bash
  cmake -S src/tools/eos-abi -B build/eos-abi-host -DEOS_RUST_PORT=host -DBUILD_TESTING=ON
  cmake --build build/eos-abi-host
  ```

  Expected: configure fails because `src/tools/eos-abi/CMakeLists.txt` is not yet present.

- [ ] **Step 3: Define the stable header and private port**

  Public version entry points are:

  ```c
  #define EOS_RUST_ABI_MAJOR UINT32_C(1)
  #define EOS_RUST_ABI_MINOR UINT32_C(0)
  uint32_t eos_rust_abi_version(void);
  int32_t eos_rust_abi_require(uint32_t major, uint32_t minimum_minor);
  int32_t *eos_rust_errno_location(void);
  ```

  `eos_port.h` may use native pointers because it is private. It declares one operation per stable service and is implemented by exactly one selected port. Host tests link `ports/host`; cross builds link `ports/martos_14_0_39`. No `os_*` declaration is permitted outside that MARTOS port file.

- [ ] **Step 4: Implement complete status translation**

  Use one `switch` with reviewed categories: invalid parameter/object → `EINVAL`, missing object → `ENOENT`, exists → `EEXIST`, allocation → `ENOMEM`, ACL → `EACCES`, in use → `EBUSY`, read-only → `EROFS`, timeout → `ETIMEDOUT`, ISR would-block → `EWOULDBLOCK`, end-of-object → EOF sentinel at the caller, device read/write errors → `EIO`, unrepresentable service → `ENOTSUP`.

- [ ] **Step 5: Restrict exported symbols**

  Build one PIC static archive, `libeos_rust_abi.a`, with hidden-by-default implementation symbols and mark public declarations with `EOS_RUST_EXPORT`. `check_exports.py` compares `nm -g --defined-only` archive output to the version/error entries currently in `expected-exports-v1.txt`; later tasks deliberately extend the file. Static inclusion avoids adding a separately deployed runtime DSO while retaining version negotiation between the sysroot and selected archive.

- [ ] **Step 6: Verify and commit**

  ```bash
  cmake -S src/tools/eos-abi -B build/eos-abi-host -DEOS_RUST_PORT=host -DBUILD_TESTING=ON
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'abi_contract|error_map|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: establish versioned EOS Rust ABI"
  ```

---

### Task 4: Implement Allocation, Runtime State, Environment, and Hash Seeding

**Files:**

- Create: `src/tools/eos-abi/src/eos_alloc.c`
- Create: `src/tools/eos-abi/src/eos_runtime.c`
- Create: `src/tools/eos-abi/src/eos_hash_seed.c`
- Create: `src/tools/eos-abi/src/eos_splitmix64.h`
- Create: `src/tools/eos-abi/tests/alloc_test.cpp`
- Create: `src/tools/eos-abi/tests/runtime_env_test.cpp`
- Create: `src/tools/eos-abi/tests/hash_seed_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`
- Modify: `src/tools/eos-abi/tests/expected-exports-v1.txt`

- [ ] **Step 1: Add failing allocation and seed tests**

  Cover zero-size allocation, multiplication overflow in `calloc`, alignments 4 through 4096, realloc preservation, null-safe free, environment snapshot/update locking, deterministic injected seed sources, concurrent seed calls, and non-repetition across 100,000 calls.

- [ ] **Step 2: Run the focused tests and observe unresolved symbols**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'alloc|runtime_env|hash_seed'
  ```

- [ ] **Step 3: Implement the public allocation/runtime functions**

  Add `eos_rust_malloc`, `eos_rust_calloc`, `eos_rust_realloc`, `eos_rust_posix_memalign`, `eos_rust_free`, `eos_rust_abort`, `eos_rust_exit`, `eos_rust_getenv`, `eos_rust_setenv`, `eos_rust_unsetenv`, and `eos_rust_environ`. The MARTOS port uses `os_mem_alloc`, `os_mem_alloc_aligned`, `os_mem_realloc`, `os_mem_free`, and `os_system_env_*`. The compatibility layer owns the process-style `environ` vector and updates it atomically under its runtime lock.

- [ ] **Step 4: Implement non-cryptographic hash diversification**

  `eos_rust_hash_seed(uint64_t *key0, uint64_t *key1)` maintains locked 128-bit process state plus a 64-bit counter. Initialize from `os_timer_get_usec`, `os_tick_get_count`, application id/name, current thread identity, selected code/heap/stack addresses, and fixed device/application diversification. Credit none of these as entropy. Produce each key with domain-separated SplitMix64 finalization:

  ```c
  static uint64_t mix64(uint64_t x) {
      x += UINT64_C(0x9e3779b97f4a7c15);
      x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
      x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
      return x ^ (x >> 31);
  }
  ```

  Test-only source injection is compiled only with `EOS_RUST_HOST_TEST=1` and is not exported by the production library.

- [ ] **Step 5: Verify and commit**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'alloc|runtime_env|hash_seed|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS allocation and hash seed services"
  ```

---

### Task 5: Build the Race-safe Compatibility Descriptor Table

**Files:**

- Create: `src/tools/eos-abi/src/eos_fd_table.h`
- Create: `src/tools/eos-abi/src/eos_fd_table.c`
- Create: `src/tools/eos-abi/tests/fd_table_test.cpp`
- Create: `src/tools/eos-abi/tests/fd_race_test.cpp`

- [ ] **Step 1: Add failing allocation/reuse/race tests**

  Test fixed descriptors 0/1/2, lowest-free allocation, `dup`/`dup2`, close-on-exec state, kind validation, stale generation rejection, double close, table exhaustion, and a 32-thread close/read race under ThreadSanitizer where available.

- [ ] **Step 2: Observe failures**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'fd_table|fd_race'
  ```

- [ ] **Step 3: Implement descriptor ownership**

  Each slot stores kind, generation, reference count, flags, and a union of private native objects. An operation acquires a reference while holding the table mutex, releases the mutex before blocking in EOS, then drops the reference. `close` removes the slot from lookup and defers native destruction until the last acquired reference is released. Reuse increments generation and never exposes the native pointer as an integer descriptor.

- [ ] **Step 4: Initialize standard descriptors explicitly**

  `eos_rust_runtime_init` installs console-backed descriptors 0/1/2 before user code. If a standard stream cannot be established, initialization aborts with a diagnostic; a later file open can never acquire 0, 1, or 2 accidentally.

- [ ] **Step 5: Verify and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'fd_table|fd_race|abi_contract'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS compatibility descriptor table"
  ```

---

### Task 6: Implement Filesystem, Directory, Pipe, and Standard-I/O Services

**Files:**

- Create: `src/tools/eos-abi/src/eos_fs.c`
- Create: `src/tools/eos-abi/src/eos_dir.c`
- Create: `src/tools/eos-abi/src/eos_pipe.c`
- Create: `src/tools/eos-abi/src/eos_stdio.c`
- Create: `src/tools/eos-abi/tests/fs_test.cpp`
- Create: `src/tools/eos-abi/tests/dir_test.cpp`
- Create: `src/tools/eos-abi/tests/pipe_stdio_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`
- Modify: `src/tools/eos-abi/tests/expected-exports-v1.txt`

- [ ] **Step 1: Add failing POSIX-shaped contract tests**

  Cover `open`, `close`, `read`, `write`, `pread`, `pwrite`, `lseek`, `fsync`, `fstat`, `stat`, `lstat`, directory snapshot iteration, `mkdir`, `unlink`, `rmdir`, `rename`, `realpath`, `getcwd`, `chdir`, `fcntl`, `dup`, `dup2`, `pipe`, `isatty`, hostname, partial I/O, EOF, console streams, and explicit `ENOTSUP` for links/ownership where absent.

- [ ] **Step 2: Run and confirm unresolved filesystem symbols**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'fs|dir|pipe_stdio'
  ```

- [ ] **Step 3: Implement the current MARTOS filesystem mapping**

  Use `os_efs_file_open/init/read/write/seek/tell/flush/close`, `os_efs_entry_get_stats/remove`, `os_efs_directory_get_listing_count/get_listing`, and `os_efs_convert_to_absolute_path`. Use the shipped MARTOS C `rename` only inside the private port. Positional I/O serializes seek/operation/seek-back per descriptor. Directory handles own a snapshot so `readdir` does not expose `os_efs_entry_stats` layout.

- [ ] **Step 4: Implement pipes and redirection endpoints**

  The core pipe is a bounded byte queue with independent reader/writer reference counts, blocking and nonblocking modes, EOF when all writers close, and `EPIPE` when all readers close. Process redirection consumes the same descriptor type. Standard streams call `os_stdio_read_byte`, `os_stdio_write_byte`, and `os_stdio_write_error_byte` with partial-progress semantics. Opening `/dev/null` creates an internal null descriptor, so `Stdio::null()` needs no EOS device. The compatibility runtime owns a locked process working-directory string and resolves relative EFS paths against it.

- [ ] **Step 5: Verify the whole descriptor layer and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'fd_|fs|dir|pipe_stdio|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: implement EOS files descriptors and pipes"
  ```

---

### Task 7: Implement Threads, the Reserved TLS Root, and Destructors

**Files:**

- Create: `src/tools/eos-abi/src/eos_thread.c`
- Create: `src/tools/eos-abi/src/eos_tls.c`
- Create: `src/tools/eos-abi/src/eos_tls.h`
- Create: `src/tools/eos-abi/tests/thread_test.cpp`
- Create: `src/tools/eos-abi/tests/tls_test.cpp`
- Create: `src/tools/eos-abi/tests/tls_destructor_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`
- Modify: `src/tools/eos-abi/ports/martos_14_0_39/eos_port_martos.c`

- [ ] **Step 1: Add failing thread/TLS isolation tests**

  Cover create/join/detach, stack sizes, names, yield, unique ids, failed creation cleanup, per-thread key values, delete semantics, destructor order, value reinstallation, and a maximum of four POSIX destructor passes.

- [ ] **Step 2: Observe failures**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'thread|tls'
  ```

- [ ] **Step 3: Implement the one-slot TLS root**

  Reserve `EOS_RUST_TLS_SLOT = 7`. `os_thread_get_user_tls(NULL, 7, ...)` retrieves a lazily allocated root containing errno, dynamic pthread-key values, destructor registry state, and Rust runtime per-thread data. `os_thread_set_user_tls(NULL, 7, ...)` installs it. The thread termination trampoline runs destructors, clears slot 7, frees the root, records join status, and only then lets EOS destroy the native thread.

- [ ] **Step 4: Implement pthread-shaped thread entries**

  Export `eos_rust_pthread_create/join/detach/self/equal`, attribute init/destroy/stack-size operations, key create/delete/get/set, and name operations. `eos_rust_pthread_create` maps to `os_thread_create` with `OS_THREAD_DEFAULT_FEATURES`; join uses `os_thread_wait(..., OS_THREAD_EVENT_EXIT, ...)`. Detached thread records self-release at termination.

- [ ] **Step 5: Verify and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'thread|tls|error_map|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS threads and library TLS"
  ```

---

### Task 8: Implement Synchronization, Parking, and Clock Services

**Files:**

- Create: `src/tools/eos-abi/src/eos_mutex.c`
- Create: `src/tools/eos-abi/src/eos_condvar.c`
- Create: `src/tools/eos-abi/src/eos_rwlock.c`
- Create: `src/tools/eos-abi/src/eos_time.c`
- Create: `src/tools/eos-abi/tests/sync_test.cpp`
- Create: `src/tools/eos-abi/tests/time_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`

- [ ] **Step 1: Add failing synchronization/time tests**

  Test all-zero lazy initializers, normal mutex deadlock semantics, try-lock, condition signal/broadcast, timeout/spurious wake behavior, reader/writer exclusion, once under contention, monotonic non-regression, realtime separation, sub-tick rounding, saturation, and absolute-deadline conversion.

- [ ] **Step 2: Observe failures**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'sync|time'
  ```

- [ ] **Step 3: Implement native mappings**

  Mutex handles map to `os_mutex_*`; condition variables use an internal mutex, waiter generation, and counting semaphore; rwlocks use one mutex and two conditions. Timed operations convert an absolute `CLOCK_MONOTONIC` deadline to remaining EOS ticks on every wait, rounding a positive sub-tick interval upward to one tick.

- [ ] **Step 4: Implement clocks and sleeps**

  `CLOCK_MONOTONIC` derives from the highest-resolution monotonically increasing EOS timer available and extends any wrapping counter under a lock. `CLOCK_REALTIME` uses `os_utc_get_usec`. `nanosleep` uses `os_delay` for whole milliseconds plus `os_delay_usec` for the remainder with checked arithmetic.

- [ ] **Step 5: Verify and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'sync|time|thread|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS synchronization and clocks"
  ```

---

### Task 9: Implement the `std::net` Socket Surface

**Files:**

- Create: `src/tools/eos-abi/src/eos_socket.c`
- Create: `src/tools/eos-abi/src/eos_poll.c`
- Create: `src/tools/eos-abi/src/eos_addrinfo.c`
- Create: `src/tools/eos-abi/tests/socket_test.cpp`
- Create: `src/tools/eos-abi/tests/poll_test.cpp`
- Create: `src/tools/eos-abi/tests/addrinfo_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`

- [ ] **Step 1: Add failing TCP/UDP/poll tests**

  Cover create/bind/connect/listen/accept, send/recv/sendto/recvfrom, shutdown, local/peer address, nonblocking state, read/write timeout options, address conversion, descriptor-kind rejection, multi-socket poll, numeric `getaddrinfo`, and hostname lookup behavior.

- [ ] **Step 2: Observe failures**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'socket|poll|addrinfo'
  ```

- [ ] **Step 3: Map public socket operations**

  Translate fixed EOS ABI socket-address structures to `os_net_sockaddr` only in the MARTOS port. Map `os_net_socket_create/bind/connect/listen/accept/send/recv/sendto/recvfrom/shutdown/setsockopt` and socket-set polling. Convert EOS negative network status codes through the reviewed errno table and preserve partial counts.

- [ ] **Step 4: Handle DNS honestly**

  Numeric IPv4/IPv6 parsing succeeds without DNS. The MARTOS-SMP 14.0.39 public header does not expose a DNS lookup primitive, so hostname lookup returns `EAI_SYSTEM` with `errno = ENOTSUP` and `network.hostname_resolution = false` in the initial capability matrix. A later port may implement the same stable `eos_rust_getaddrinfo` symbol when EOS exposes DNS.

- [ ] **Step 5: Verify and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'socket|poll|addrinfo|fd_|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS networking services"
  ```

---

### Task 10: Implement Spawn/Wait/Kill and Process Redirection

**Files:**

- Create: `src/tools/eos-abi/src/eos_process.c`
- Create: `src/tools/eos-abi/src/eos_process.h`
- Create: `src/tools/eos-abi/tests/process_test.cpp`
- Create: `src/tools/eos-abi/tests/process_redirection_test.cpp`
- Modify: `src/tools/eos-abi/include/eos_rust_abi.h`

- [ ] **Step 1: Add failing process state-machine tests**

  Cover argument vectors, environment overlays, working directory, inherited/null/piped stdio, close-on-exec filtering, loader failure, normal exit, child failure, try-wait, blocking wait, kill, repeated wait, and process-handle reuse.

- [ ] **Step 2: Observe failures**

  ```bash
  cmake --build build/eos-abi-host
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'process'
  ```

- [ ] **Step 3: Define a fixed-layout process request**

  The stable ABI receives counted arrays, never EOS structs:

  ```c
  typedef struct eos_rust_spawn_request {
      const char *program;
      const char *const *argv;
      uint32_t argc;
      const char *const *envp;
      uint32_t envc;
      const char *cwd;
      eos_rust_fd_t stdin_fd;
      eos_rust_fd_t stdout_fd;
      eos_rust_fd_t stderr_fd;
      uint32_t flags;
      uint32_t reserved[7];
  } eos_rust_spawn_request;
  ```

  Export `eos_rust_spawn`, `eos_rust_process_wait`, `eos_rust_process_try_wait`, `eos_rust_process_kill`, and `eos_rust_process_close`.

- [ ] **Step 4: Implement the current EOS port without `fork`**

  Use an EOS worker thread and the current synchronous command/application primitive beneath the port, with an `os_stdio_redirection` bridge backed by compatibility pipes. The process table records loader status and exit status separately. Where the installed EOS SDK lacks per-child environment or working-directory isolation, reject only the requested unsupported option with `ENOTSUP`; never mutate the global environment around a concurrent spawn.

- [ ] **Step 5: Verify and commit**

  ```bash
  ctest --test-dir build/eos-abi-host --output-on-failure -R 'process|pipe_stdio|fd_|exports'
  git add src/tools/eos-abi
  git commit -m "runtime: add EOS process services"
  ```

---

### Task 11: Vendor `libc` 0.2.185 and Add the EOS Rust Bindings

**Files:**

- Create: `src/tools/eos-libc/` from upstream `libc` 0.2.185
- Create: `src/tools/eos-libc/src/eos/mod.rs`
- Modify: `src/tools/eos-libc/src/lib.rs`
- Modify: `src/tools/eos-libc/build.rs`
- Modify: `library/Cargo.toml`
- Modify: `library/Cargo.lock`
- Create: `tests/eos/abi/c_layout.c`
- Create: `tests/eos/abi/rust_layout.rs`
- Create: `tests/eos/abi/compare_layouts.py`
- Create: `tests/eos/host/test_libc_links.py`

- [ ] **Step 1: Import the exact libc source**

  Copy the tracked contents of commit `71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8` without its `.git` directory. Add this entry under `library/Cargo.toml`'s existing `[patch.crates-io]`:

  ```toml
  libc = { path = "../src/tools/eos-libc" }
  ```

  Regenerate `library/Cargo.lock` only through Cargo; its libc package remains version `0.2.185`.

- [ ] **Step 2: Add a failing link-name audit**

  `test_libc_links.py` extracts every `pub fn` in `src/eos/mod.rs` and asserts it has `#[link_name = "eos_rust_..."]`, except pure Rust helpers. It also rejects exported extern names lacking the prefix.

- [ ] **Step 3: Route EOS before generic Unix selection**

  In `src/lib.rs`, place `target_os = "eos"` before `cfg(unix)` and re-export `src/eos/mod.rs`. Add `eos` to `CHECK_CFG_EXTRA` in `build.rs`. The unconditional `new` module already supplies the common Unix standard-descriptor constants; the EOS module reuses rather than redeclares them. Do not route EOS through Linux, NuttX, newlib, or another platform's ABI.

- [ ] **Step 4: Define the exact EOS ABI surface**

  Mirror the fixed header layouts and the constants used by the Rust Unix PAL: errno values, file/open/fcntl modes, `stat`, `dirent`, `iovec`, pthread values, clocks/timespec, poll, IPv4/IPv6 socket structures and options, exit codes, and standard descriptors. Bind every callable item to its stable shim symbol. For example:

  ```rust
  unsafe extern "C" {
      #[link_name = "eos_rust_errno_location"]
      pub fn __errno() -> *mut c_int;
      #[link_name = "eos_rust_open"]
      pub fn open(path: *const c_char, flags: c_int, mode: mode_t) -> c_int;
      #[link_name = "eos_rust_pthread_create"]
      pub fn pthread_create(
          thread: *mut pthread_t,
          attr: *const pthread_attr_t,
          start: extern "C" fn(*mut c_void) -> *mut c_void,
          arg: *mut c_void,
      ) -> c_int;
  }
  ```

- [ ] **Step 5: Cross-check C and Rust layouts**

  Compile both fixtures for `armv7a-unknown-eos-eabi`; each emits `sizeof`, alignment, and field offsets as named object symbols. Compare them with `readelf -Ws` and `objdump -s`. Include `timespec`, `stat`, all pthread opaque types, `sockaddr_*`, `dirent`, and the spawn request.

- [ ] **Step 6: Verify and commit**

  ```bash
  python3 -m unittest tests/eos/host/test_libc_links.py -v
  ./x check library/std --target armv7a-unknown-eos-eabi
  python3 tests/eos/abi/compare_layouts.py build/eos-abi-layout
  git add src/tools/eos-libc library/Cargo.toml library/Cargo.lock tests/eos
  git commit -m "library: add EOS libc bindings"
  ```

---

### Task 12: Adapt the Unix PAL and Add EOS Runtime/Random Hooks

**Files:**

- Modify: `library/std/src/sys/pal/unix/mod.rs`
- Modify: `library/std/src/sys/io/error/unix.rs`
- Modify: `library/std/src/sys/args/unix.rs`
- Modify: `library/std/src/sys/env/unix.rs`
- Modify: `library/std/src/sys/env_consts.rs`
- Modify: `library/std/src/sys/fd/unix.rs`
- Modify: `library/std/src/sys/fs/unix.rs`
- Modify: `library/std/src/sys/fs/unix/dir.rs`
- Modify: `library/std/src/sys/paths/unix.rs`
- Modify: `library/std/src/sys/thread/unix.rs`
- Modify: `library/std/src/sys/random/mod.rs`
- Create: `library/std/src/sys/random/eos.rs`
- Create: `tests/eos/host/test_pal_cfg_scope.py`

- [ ] **Step 1: Add a failing EOS `std` smoke crate and cfg-scope audit**

  Create `tests/eos/apps/std-smoke` using `Vec`, `HashMap`, env/args, `File`, `Mutex`, `thread::spawn`, clocks, `TcpStream` type-checking, and `Command` type-checking. `test_pal_cfg_scope.py` rejects changes that add EOS to Linux-only syscall branches and requires each EOS-specific branch to include a comment naming the stable shim service it uses.

- [ ] **Step 2: Confirm the sysroot fails to compile**

  ```bash
  ./x build library/std --target armv7a-unknown-eos-eabi
  ```

  Expected: missing/incorrect EOS PAL constants or branches are reported.

- [ ] **Step 3: Add narrow Unix PAL routing**

  Store `argc/argv` using the existing Unix argument implementation. Route `environ()` through `eos_rust_environ`. Link errno to `eos_rust_errno_location`. Use pthread-backed Unix synchronization/thread/TLS paths, with native target TLS still false. Add EOS to fallback vectored-I/O branches rather than declaring `readv/writev` until implemented. Add an EOS `available_parallelism` branch backed by `eos_rust_cpu_count` and require it to report the two Cortex-A9 cores. Supply `sysconf` values for page size, minimum thread stack, host-name maximum, and online processors.

- [ ] **Step 4: Initialize and clean up the native ABI**

  At the beginning of Unix PAL `init`, call `eos_rust_abi_require(1, 0)` then `eos_rust_runtime_init(argc, argv)`. Skip POSIX signal manipulation for EOS because the public EOS interface does not expose signal handlers. Retain standard-fd sanitation through the shim. On PAL cleanup, call `eos_rust_runtime_cleanup` after Rust TLS cleanup.

- [ ] **Step 5: Add the explicit random policy**

  `random/eos.rs` is exactly:

  ```rust
  pub fn fill_bytes(_: &mut [u8]) {
      panic!("EOS v1 does not provide cryptographically secure random bytes");
  }

  pub fn hashmap_random_keys() -> (u64, u64) {
      let mut key0 = 0;
      let mut key1 = 0;
      let rc = unsafe { libc::eos_hash_seed(&mut key0, &mut key1) };
      assert_eq!(rc, 0, "EOS hash seed initialization failed");
      (key0, key1)
  }
  ```

  The libc name `eos_hash_seed` links to `eos_rust_hash_seed`; it is an EOS-only binding, not a public Rust API.

- [ ] **Step 6: Iterate until the EOS standard library compiles cleanly**

  For every compiler error, either add a stable shim binding or an explicit EOS PAL branch. Do not reuse another OS cfg to suppress it. Record unsupported functions in the capability matrix rather than stubbing success.

- [ ] **Step 7: Verify and commit**

  ```bash
  python3 -m unittest tests/eos/host/test_pal_cfg_scope.py -v
  ./x build library/std --target armv7a-unknown-eos-eabi
  ./x test library/std --target armv7a-unknown-eos-eabi --no-run
  git add library/std tests/eos
  git commit -m "library: port the Unix PAL to EOS"
  ```

---

### Task 13: Add the EOS-specific `std::process` Backend

**Files:**

- Modify: `library/std/src/sys/process/unix/mod.rs`
- Modify: `library/std/src/sys/process/unix/common.rs`
- Create: `library/std/src/sys/process/unix/eos.rs`
- Create: `tests/eos/apps/process-smoke/Cargo.toml`
- Create: `tests/eos/apps/process-smoke/src/main.rs`

- [ ] **Step 1: Add a failing process compile test**

  The sample must configure piped stdout, explicit args/env/cwd, spawn, call `try_wait`, kill on demand, then wait and decode the status. Cross-link it with the prebuilt shim.

- [ ] **Step 2: Route EOS away from `fork`/`exec`**

  Add this branch before the default Unix implementation:

  ```rust
  target_os = "eos" => {
      mod eos;
      use eos as imp;
  }
  ```

  Reuse `common::Command`, `Stdio`, `ChildPipes`, and `ExitCode`; do not implement fake `fork`, `exec`, `waitpid`, or signals in libc.

- [ ] **Step 3: Implement the EOS process methods**

  `Command::spawn` calls `setup_io`, converts argv/env/cwd to counted `CStringArray` data, fills `eos_rust_spawn_request`, and transfers child-side descriptor ownership only after successful spawn. `Process` owns the stable process handle. `wait`, `try_wait`, and `kill` call the matching ABI functions. `exec` returns `Unsupported`, because EOS v1 has no process-image replacement.

- [ ] **Step 4: Preserve status distinctions**

  `ExitStatus` stores a fixed EOS status record and reports `code()` only for a normal child exit. Loader failure becomes `io::Error` from `spawn`; termination maps to a non-success status without impersonating a Unix signal unless EOS explicitly identifies one.

- [ ] **Step 5: Verify and commit**

  ```bash
  ./x build library/std --target armv7a-unknown-eos-eabi
  RUSTC=./build/host/stage1/bin/rustc cargo build --manifest-path tests/eos/apps/process-smoke/Cargo.toml --target armv7a-unknown-eos-eabi
  git add library/std/src/sys/process tests/eos/apps/process-smoke
  git commit -m "library: add EOS process backend"
  ```

---

### Task 14: Enable ARM EHABI Panic Unwinding and Enforce FFI Containment

**Files:**

- Modify: `library/unwind/src/lib.rs`
- Verify: `library/std/src/sys/personality/mod.rs`
- Create: `tests/eos/apps/unwind/Cargo.toml`
- Create: `tests/eos/apps/unwind/src/main.rs`
- Create: `tests/eos/apps/ffi-containment/Cargo.toml`
- Create: `tests/eos/apps/ffi-containment/src/lib.rs`
- Create: `tests/eos/abi/ffi_caller.c`
- Create: `tests/eos/host/test_ffi_unwind_policy.py`

- [ ] **Step 1: Add failing unwind and policy tests**

  The unwind app uses `catch_unwind` and a drop guard. The containment library exports an ordinary `extern "C"` function whose body wraps Rust work in `catch_unwind` and returns a documented negative EOS error. The policy test scans production Rust sources for `C-unwind` and requires an empty allowlist.

- [ ] **Step 2: Confirm the initial link lacks unwind support**

  Build the unwind app and inspect the linker error for `_Unwind_*`/personality symbols or missing `.ARM.exidx` retention.

- [ ] **Step 3: Select the Unix GCC personality and ARM GNU unwinder**

  EOS already selects the Unix libunwind declarations and the GCC personality because its target family is Unix. Add an explicit EOS `#[link(name = "gcc")]` contract in `library/unwind/src/lib.rs`; the linker wrapper also appends the selected ARM GNU `libgcc.a` after Rust archives. Add a source-policy assertion to `test_ffi_unwind_policy.py` that `library/std/src/sys/personality/mod.rs` continues selecting `mod gcc` for EOS rather than editing that already-correct selector.

- [ ] **Step 4: Enforce boundary containment**

  Compile all EOS Rust/C FFI test crates with `-Dffi-unwind-calls`. Rust-created and EOS-created thread roots invoke a containment trampoline. A panic inside cleanup aborts. No C++ exception test is allowed to enter Rust; a C++ fixture proves the boundary wrapper returns normally when Rust contains its own panic.

- [ ] **Step 5: Verify static unwind metadata**

  ```bash
  python3 -m unittest tests/eos/host/test_ffi_unwind_policy.py -v
  RUSTC=./build/host/stage1/bin/rustc cargo build --manifest-path tests/eos/apps/unwind/Cargo.toml --target armv7a-unknown-eos-eabi
  arm-none-eabi-readelf -SW target/armv7a-unknown-eos-eabi/debug/unwind | rg '\.ARM\.(exidx|extab)'
  if arm-none-eabi-nm -u target/armv7a-unknown-eos-eabi/debug/unwind | rg '_Unwind_|__aeabi_unwind'; then exit 1; fi
  ```

  Expected: allocated `.ARM.exidx` exists, `.ARM.extab` exists when required, and no unresolved unwind symbol remains after final link.

- [ ] **Step 6: Commit**

  ```bash
  git add library/unwind tests/eos
  git commit -m "runtime: enable contained Rust unwinding on EOS"
  ```

---

### Task 15: Implement the EOS Linker Wrapper, ELF Validator, and Authentication Packager

**Files:**

- Create: `src/tools/eos-sdk/bin/eos-rust-link`
- Create: `src/tools/eos-sdk/bin/eos-elf-validate`
- Create: `src/tools/eos-sdk/bin/eos-auth-package`
- Create: `src/tools/eos-sdk/linker/app_linker_script.ld`
- Create: `src/tools/eos-sdk/manifests/allowed-dynamic-symbols.txt`
- Create: `src/tools/eos-sdk/tests/test_linker_args.py`
- Create: `src/tools/eos-sdk/tests/test_elf_validate.py`
- Create: `src/tools/eos-sdk/tests/test_auth_package.py`
- Create: `src/tools/eos-sdk/tests/fixtures/marker-only.elf`

- [ ] **Step 1: Add failing command/ELF/package tests**

  Use a fake compiler driver to capture wrapper arguments. Validator tests mutate ELF header fields and relocation names. Authentication tests reject a missing input, a marker-only file, and an invalid ELF before accepting a valid fixture.

- [ ] **Step 2: Implement the wrapper's deterministic command**

  Resolve all paths below `EOS_RUST_SDK_ROOT`, then invoke ARM GNU 14.3.Rel1 with:

  ```text
  -march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp
  -fPIC -pie -nostdlib -Wl,--gc-sections -Wl,-z,notext
  -Wl,--dynamic-linker=/usr/lib/ld.so.1
  -Wl,-T,<sdk>/linker/app_linker_script.ld
  <rustc objects and archives>
  <sdk>/lib/libeos_rust_abi.a
  -L<eos>/martos/lib -lmartos_app -lmartos_c++ -lmartos_c++abi -lmartos_c
  <absolute ARM GNU multilib libgcc.a>
  ```

  Preserve rustc's output, library-search, export, and object arguments. Reject `-static`, hard-float flags, a different linker script, and an output outside the requested path.

  Do not inject GCC `crt*.o` files: the proven EOS baseline links with `-nostdlib`, the application script uses `ENTRY(main)`, and the EOS loader enters the application `main` supplied by Rust's runtime object. The wrapper test must require that the Rust object defining `main` is preserved and precedes the runtime libraries.

- [ ] **Step 3: Retain ARM unwind metadata in the linker script**

  Base the script on MARTOS-SMP 14.0.39 `martos/builder/app_linker_script.ld`, but use `KEEP(*(.ARM.exidx*))`, `KEEP(*(.ARM.extab*))`, `KEEP(*(.gcc_except_table*))`, init/fini arrays, and an `ASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "native TLS is unsupported")`. Preserve `PT_INTERP`, RX and RW `PT_LOAD`, `PT_DYNAMIC`, and `PT_ARM_EXIDX`.

- [ ] **Step 4: Implement strict ELF validation**

  Parse `readelf -hW/-lW/-SW/-rW/-dW/-AW/-Ws` output and require:

  - ELF32, little endian, ARM, EABI5, `ET_DYN`, soft-float flag.
  - The ELF entry address resolves to the global `main` symbol selected by `ENTRY(main)`.
  - `/usr/lib/ld.so.1`, at least one RX and one RW load segment, dynamic segment, PIE flag.
  - ARMv7 Application profile, Thumb-2 interworking, VFPv3, NEONv1.
  - Allocated `.ARM.exidx` and retained `.ARM.extab` when emitted.
  - Relocations only from `R_ARM_RELATIVE`, `R_ARM_ABS32`, `R_ARM_JUMP_SLOT`.
  - No `TEXTREL`, no `PT_TLS`, no nonempty `.tdata/.tbss`.
  - `libmartos_app.so.1.0` dependency and undefined symbols contained in the reviewed allowlist.

- [ ] **Step 5: Implement authentication without corrupting the ELF**

  Hash the validated base ELF with SHA-256, then append exactly:

  ```text
  martos_smp_elf_authentication_block_sha2_256_adbc_1394_e532_101\n
  <64 lowercase hexadecimal SHA-256 characters>\n
  ```

  Verify the appended digest against the bytes preceding the marker and rerun the ELF validator with an allowed-trailer mode. Reject any input whose first four bytes are not `\x7fELF` or whose pre-trailer length is below the last ELF section/program extent.

- [ ] **Step 6: Verify and commit**

  ```bash
  python3 -m unittest discover -s src/tools/eos-sdk/tests -v
  git add src/tools/eos-sdk
  git commit -m "tools: add EOS link validation and authentication"
  ```

---

### Task 16: Build and Package the Precompiled EOS Sysroot and SDK

**Files:**

- Create: `src/tools/eos-sdk/templates/config.toml`
- Create: `src/tools/eos-sdk/templates/cargo-config.toml`
- Create: `src/tools/eos-sdk/bin/build-eos-sdk`
- Create: `src/tools/eos-sdk/bin/install-eos-sdk`
- Create: `src/tools/eos-sdk/manifests/sdk-layout.toml`
- Create: `src/tools/eos-sdk/tests/test_sdk_layout.py`
- Create: `tests/eos/apps/hello-std/Cargo.toml`
- Create: `tests/eos/apps/hello-std/src/main.rs`

- [ ] **Step 1: Add a failing installed-SDK smoke test**

  `test_sdk_layout.py` requires host `rustc`, Cargo, rustdoc, target `libcore`, `liballoc`, `libstd`, `libpanic_unwind`, `libproc_macro`, `libtest`, the native ABI library/header, wrapper, linker script, validators, manifests, licenses, and source revisions.

- [ ] **Step 2: Create the bootstrap configuration**

  Configure only the EOS target sysroot, set the cross compiler/ar/ranlib/linker paths, and set:

  ```toml
  [rust]
  backtrace = true
  std-features = ["panic-unwind", "backtrace", "backtrace-trace-only"]

  [target.armv7a-unknown-eos-eabi]
  linker = "eos-rust-link"
  crt-static = false
  ```

  `backtrace-trace-only` guarantees address capture without claiming on-target symbolization.

- [ ] **Step 3: Build the native port and Rust distribution**

  `build-eos-sdk` validates the lock, builds the PIC static `libeos_rust_abi.a` with `EOS_RUST_PORT=martos_14_0_39`, runs host tests, runs `./x build`/`./x dist` for the host and EOS target, and stages a single relocatable SDK directory. It records SHA-256 hashes of the ARM GNU installation and EOS SDK inputs.

- [ ] **Step 4: Install as a named toolchain**

  `install-eos-sdk` uses `rustup toolchain link eos-1.97.1 <sdk>` when rustup is available, or prints the explicit `RUSTC`/`CARGO` paths. The Cargo template contains only:

  ```toml
  [build]
  target = "armv7a-unknown-eos-eabi"

  [target.armv7a-unknown-eos-eabi]
  linker = "eos-rust-link"
  ```

  It contains no target JSON and no unstable `build-std` flags.

- [ ] **Step 5: Prove ordinary Cargo usage**

  ```bash
  cargo +eos-1.97.1 build --manifest-path tests/eos/apps/hello-std/Cargo.toml
  eos-elf-validate tests/eos/apps/hello-std/target/armv7a-unknown-eos-eabi/debug/hello-std
  ```

- [ ] **Step 6: Verify and commit**

  ```bash
  python3 -m unittest src/tools/eos-sdk/tests/test_sdk_layout.py -v
  git add src/tools/eos-sdk tests/eos/apps/hello-std
  git commit -m "dist: package the EOS Rust SDK and sysroot"
  ```

---

### Task 17: Add ABI, Static-ELF, and CI Release Gates

**Files:**

- Create: `tests/eos/apps/filesystem/Cargo.toml`
- Create: `tests/eos/apps/filesystem/src/main.rs`
- Create: `tests/eos/apps/threads-tls/Cargo.toml`
- Create: `tests/eos/apps/threads-tls/src/main.rs`
- Create: `tests/eos/apps/network/Cargo.toml`
- Create: `tests/eos/apps/network/src/main.rs`
- Create: `tests/eos/apps/process/Cargo.toml`
- Create: `tests/eos/apps/process/src/main.rs`
- Create: `tests/eos/apps/ffi-abi/Cargo.toml`
- Create: `tests/eos/apps/ffi-abi/src/main.rs`
- Create: `tests/eos/abi/softfp.c`
- Create: `tests/eos/abi/softfp.rs`
- Create: `tests/eos/host/test_static_elves.py`
- Create: `tests/eos/run-ci.sh`
- Create: `.github/workflows/eos.yml`

- [ ] **Step 1: Add representative applications and expected failures**

  Each app isolates one surface. The C/Rust ABI fixture passes/returns integers, small enums, `f32`, `f64`, and mixed aggregates in both directions. Build the C half with the exact approved GCC flags and the Rust half with the built-in target.

- [ ] **Step 2: Compare the softfp calling convention statically**

  Use `objdump -dr` and `readelf -A` to require VFP/NEON instructions where expected, no `Tag_ABI_VFP_args` hard-float declaration, EABI5 soft-float flags, and compatible aggregate argument/return sequences. A hard-float C fixture must fail the attribute comparison test.

- [ ] **Step 3: Validate every final artifact**

  Build debug and release variants, run `eos-elf-validate`, authenticate them, verify the trailer, and ensure the unstripped ELF/build-id pair is retained for offline symbolization.

- [ ] **Step 4: Define CI scope explicitly**

  `tests/eos/run-ci.sh` runs lock checks, Rust target tests, native host CTest, custom libc audits, Unix PAL policy scans, sysroot build, application cross-links, ABI comparison, ELF validation, and authentication tests. `.github/workflows/eos.yml` calls only this script and uploads SDK/static-test artifacts; it contains no credentials, board addresses, transfer commands, or deployment steps.

- [ ] **Step 5: Verify and commit**

  ```bash
  tests/eos/run-ci.sh
  git diff --check
  git add tests/eos .github/workflows/eos.yml
  git commit -m "ci: gate EOS ABI sysroot and ELF artifacts"
  ```

---

### Task 18: Publish the Capability Matrix and Manual Two-board Release Gate

**Files:**

- Create: `src/tools/eos-sdk/manifests/capabilities.toml`
- Create: `src/tools/eos-sdk/manifests/release-manifest.schema.json`
- Create: `tests/eos/board/board-test-manifest.toml`
- Create: `tests/eos/board/result.schema.json`
- Create: `tests/eos/board/check_results.py`
- Create: `docs/eos/capabilities.md`
- Create: `docs/eos/manual-board-test.md`
- Create: `docs/eos/releasing.md`

- [ ] **Step 1: Add a failing release-result checker**

  Require one signed result for `XC7Z030` and one for `XC7Z045`, the same SDK/toolchain/ABI/linker revisions, debug and release runs, and pass/fail entries for load relocation, args/env/cwd/stdio, allocation, files, threads, TLS, synchronization, time, TCP/UDP, numeric addresses, process control, panic catch/drop, FFI containment, and backtrace addresses.

- [ ] **Step 2: Publish initial capability truth**

  Mark proven shim services true. Mark `secure_random`, `native_elf_tls`, `hard_float_abi`, `process_exec_replacement`, `cross_language_unwind`, and `automatic_board_deployment` false. Mark DNS, IPv6, multicast, symlinks, hard links, ownership, mmap/protection, dynamic loading, per-child env, and per-child cwd true only after their contract and board rows pass; otherwise they remain false with the stable `Unsupported` behavior.

- [ ] **Step 3: Document manual deployment without automating it**

  The operator builds and authenticates the board bundle, transfers it using the organization's approved manual method, starts it through the EOS console, captures output and load addresses, and writes a result file matching the schema. The repository supplies no automatic board command.

- [ ] **Step 4: Execute the full matrix on both part families**

  Run identical authenticated binaries where possible. Explicitly load the PIE at two addresses on each board. Record board module/part, EOS version, SDK revision, Rust commit, ARM GNU release/hash, native ABI version, linker-script hash, application build ids, and all observed failures.

- [ ] **Step 5: Run final release verification**

  ```bash
  tests/eos/run-ci.sh
  python3 tests/eos/board/check_results.py \
    release-results/xc7z030.json \
    release-results/xc7z045.json
  src/tools/eos-sdk/bin/eos-elf-validate release-bundle/hello-std.elf --allow-auth-trailer
  ```

  Do not call the release complete until both result files pass. An unsupported optional capability may remain false; every v1 acceptance item in the approved design must pass.

- [ ] **Step 6: Commit documentation and release gates**

  ```bash
  git add src/tools/eos-sdk/manifests tests/eos/board docs/eos
  git commit -m "docs: add EOS capability and two-board release gates"
  ```

---

## Final Verification Checklist

- [ ] `rustc --print target-list` contains exactly one v1 EOS target and no hard-float sibling.
- [ ] Target cfg, LLVM features, data layout, float ABI, atomics, frame pointers, and PIC settings pass compiler tests.
- [ ] `libeos_rust_abi` exports only the reviewed v1 `eos_rust_*` symbols and rejects ABI-major mismatch at startup.
- [ ] Host contract tests pass with concurrency sanitizers where supported.
- [ ] The vendored libc surface contains explicit EOS layouts/link names and no accidental native `os_*` dependency.
- [ ] `core`, `alloc`, `std`, `panic_unwind`, `proc_macro`, and `test` are prebuilt in the installed target sysroot.
- [ ] A clean sample uses ordinary Cargo without target JSON or `-Z build-std`.
- [ ] C/Rust fixtures prove Cortex-A9 softfp ABI compatibility while VFPv3-D32/NEON instructions remain enabled.
- [ ] Rust `catch_unwind`, destructors, and ordinary-FFI containment cross-link with ARM EHABI metadata retained.
- [ ] Every final image is an ELF32 ARM EABI5 soft-float `ET_DYN` PIE with only loader-approved relocations and no native TLS/text relocations.
- [ ] Authentication rejects marker-only input and preserves a parseable validated ELF plus the exact SHA-256 trailer.
- [ ] The capability matrix reports every unavailable operation as unsupported rather than successful.
- [ ] CI passes without contacting either board.
- [ ] Manual release results pass on both XC7Z030 and XC7Z045 with matching SDK revisions.

## Execution Handoff

Two supported execution modes are available after this plan is committed:

1. **Subagent-Driven Development (recommended):** use `superpowers:subagent-driven-development` in this task, assigning one implementation task at a time and reviewing each red/green/commit checkpoint.
2. **Separate Plan Execution:** start a fresh task in the isolated Rust worktree and use `superpowers:executing-plans`, stopping at the review checkpoints after Tasks 2, 6, 10, 14, 17, and 18.

In either mode, start with `superpowers:using-git-worktrees`, keep the current integration branch recoverable, and use `superpowers:verification-before-completion` before reporting any milestone or release as complete.
