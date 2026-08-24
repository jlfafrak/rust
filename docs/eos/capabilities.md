# EOS Rust v1 capabilities

The machine-readable source of truth is
`src/tools/eos-sdk/manifests/capabilities.toml`. A `supported = true` entry means the v1 shim or
toolchain contract is implemented and has the cited automated evidence. It does not replace the
manual XC7Z030 and XC7Z045 release evidence. A release still needs signed results from both part
families.

Each SDK contains the reviewed matrix at `manifests/capabilities.toml` and the manual procedure,
checker, schemas, policy, and exact application sources under `share/board-test/`. The bundle
contains no board result, signing key, credential, board location, transfer command, or deployment
automation.

An unavailable operation reports Rust `ErrorKind::Unsupported`, or the documented native
`ENOTSUP` that maps to it. It never reports success. The stable spelling recorded by the matrix
and board results is `Unsupported`.

## Implemented v1 surface

| Capability | Matrix value | Automated evidence boundary |
| --- | --- | --- |
| Allocation | true | Native host allocation contract tests |
| Arguments, environment, and current directory | true | Runtime and process host contract tests |
| Standard input, output, and error | true | Descriptor and standard-stream host tests |
| Basic files and directories | true | Filesystem and descriptor host tests |
| Threads | true | Thread lifecycle host tests |
| Rust TLS adapter | true | Reserved-slot TLS and destructor host tests |
| Synchronization and parking | true | Mutex, condition, rwlock, once, and parking tests |
| Monotonic and realtime clocks | true | Clock conversion and deadline tests |
| TCP and UDP | true | Socket host contract tests |
| Numeric socket addresses | true | Numeric parsing and formatting tests |
| Spawn, wait, status, and termination | true | Process and descriptor-inheritance tests |
| Rust panic unwinding and destructor cleanup | true | Rust unwind fixtures and ARM EHABI static gates |
| Ordinary-FFI panic containment | true | Containment contract and static gates |
| Backtrace frame addresses | true | Trace-only build and build-ID retention gates |

The matrix's `board_evidence = false` values are intentional: this repository does not contain
authentic signed hardware results. The manual release gate supplies that evidence without
changing implementation capability claims.

## Unavailable in v1

These release-one exclusions remain false and `Unsupported`:

- cryptographically secure random bytes;
- native ELF TLS;
- a hard-float procedure-call ABI target;
- in-process executable replacement;
- Rust panic or C++ exception unwinding across language boundaries; and
- automatic physical-board deployment.

HashMap seed diversification is non-cryptographic and is not a secure-random capability. Rust
TLS uses the EOS slot adapter and is not native ELF TLS. The one v1 target keeps the softfp
procedure-call ABI while still enabling VFPv3-D32 and NEON instructions.

The following audited optional capabilities also remain false until both their implementation
contract and signed two-board evidence exist:

- DNS, IPv6 transport, and multicast;
- symbolic links, hard links, ownership changes, permission-bit fidelity, and filesystem
  durability;
- memory mapping/protection and dynamic loading;
- per-child environment and per-child working-directory overrides; and
- signals.

Numeric IPv6 conversion tests do not establish IPv6 transport support. Likewise, static or host
tests alone do not permit an optional row to become true. Changing one of these entries requires
the contract evidence, signed passing board rows, and design review required by the release
policy.
