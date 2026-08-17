# Task 9 Report — EOS Networking Services

## Outcome

Task 9 adds the fixed networking ABI consumed by Rust 1.97.1's Unix PAL: IPv4/IPv6 TCP and
UDP sockets, socket descriptor `read`/`write`/`fcntl`, socket options and timeouts, `poll`,
numeric address conversion, and numeric/null-node `getaddrinfo`. The implementation keeps all
host and MARTOS socket types, constants, calls, and native error capture behind the port
boundary.

The accepted 93-symbol ABI grows by exactly 20 symbols to 113. Production archives contain no
host-test seam. MARTOS 14.0.39 has no public hostname resolver, so DNS and named-service lookups
fail honestly with `EAI_SYSTEM` and compatibility `ENOTSUP`; numeric resolution remains fully
common and deterministic. No Task 10 or later work is included.

The implementation commit is `ecda1aa984834655dadee47b2005cba70597a33f`
(`runtime: add EOS networking services`). Independent exact-delta review found no Critical and
five Important issues; every verified issue was reproduced, fixed with TDD, and reverified in
the separate commits recorded below.

## TDD and debugging evidence

The three required tests were created and wired while `eos_socket.c`, `eos_poll.c`, and
`eos_addrinfo.c` were absent. Each target was built independently so one failure could not mask
another:

- `socket_test` failed at link with exit status 2 and unresolved socket, bind, listen,
  getsockname, connect, accept, send, receive, shutdown, and related socket symbols.
- `poll_test` failed at link with exit status 2 and unresolved `eos_rust_poll`.
- `addrinfo_test` failed at link with exit status 2 and unresolved `inet_pton`, `inet_ntop`,
  `getaddrinfo`, `freeaddrinfo`, and `gai_strerror` compatibility symbols.

The MARTOS mapping fake was then compiled against the private mapping contract rather than a
test reimplementation. Its missing-contract compile RED preceded the shared mapping header and
real-port integration.

Systematic debugging separated environment and product failures. The managed sandbox rejects
host `AF_INET` creation with native `EPERM`; the same binaries pass unchanged when granted only
loopback-socket execution outside that restriction. Socket creation was traced through public
validation, descriptor publication, normalized port calls, and the native host call before
classifying this as environmental.

Further deterministic RED/GREEN rounds found and fixed:

- A combined UDP poll fixture included an always-writable sender, allowing native poll to
  return before datagram delivery. Splitting readable-receiver and writable-sender assertions
  removed test scheduling dependence without changing production.
- A zero-byte receive-from on orderly stream closure could attempt to translate a family-less
  native address. Host and MARTOS ports now return a zeroed normalized address for that case;
  common code reports source length zero.
- The strengthened direct consumer initially failed to compile because its test source used a
  nonexistent namespaced `INET_ADDRSTRLEN` macro. Using the contract's literal minimum capacity
  16 corrected only the consumer test.
- Running three complete CTest trees concurrently produced a pre-existing fixed-path fixture
  collision in `fs_tsan` (`close-failure fixture unlink failed`). Sequential reruns passed;
  complete matrices are therefore intentionally recorded from isolated sequential execution.
- A deterministic normalized IPv6-output test failed with
  `normalized IPv6 output must preserve the scope identifier`. Common conversion now copies
  the normalized scope id; the MARTOS port still rejects nonzero input scope and produces zero
  output scope because its native structure has no field.
- A bounded invalid-descriptor poll test failed with
  `POLLNVAL must return immediately without honoring the timeout`. Once any entry is already
  ready with `POLLNVAL`, the native poll is now a zero-time readiness snapshot rather than a
  finite or infinite wait.

Both final audit fixes passed `socket`, `poll`, and their fully instrumented TSan variants 4/4
before the complete verification matrices were refreshed.

Independent review over `e18afb49..ecda1aa9` then found five Important issues. Separate
RED/GREEN fix commits resolved them:

- `95d448ec` distinguishes valid non-socket descriptors (`ENOTSOCK`) from stale/invalid
  descriptors (`EBADF`) while retaining the descriptor lease through the kind check.
- `4f516ea1` maps host `EACCES` and `EPERM` to stable `EACCES` rather than the unknown-error
  `EIO` fallback.
- `2e4d72d9` publishes the result of every connect attempt under the shared state lock, clearing
  an obsolete pending error after a successful retry.
- `c88447bc` gates MARTOS `TCP_CLOSED` hangup reporting on shared connection-started state.
  Fresh/listening sockets are ineligible; attempted connects and accepted sockets are eligible,
  and duplicates observe the same open-file-description state.
- `bfde2832` pins all documented MARTOS negative network mappings, IPv6 native roundtrip and
  scope policy, exact timeout `setsockopt` arguments, and socketset create/add/select/query/delete
  success, fault, zero-entry, cleanup, and fail-fast paths. It also distinguishes a null
  socketset allocation result (`ENOBUFS`) from the invalid sentinel (`EIO`).

The last three TDD REDs were independently discriminating: the MARTOS contract executable
exited 18 on the invalid-sentinel mismatch; the timeout adapter fixture failed strict compilation
on the missing shared adapter; and the eligibility assertions failed link on the absent
host-test seam. The corrected mapping, socket, poll, socket TSan, and poll TSan targets passed
5/5 before the full post-review matrices.

## Stable ABI, layouts, constants, and exports

Only fixed-width scalars and fixed public layouts cross the ABI. Ordinary pointers remain
ordinary target pointers.

| Type | Size | Alignment | Required layout |
| --- | ---: | ---: | --- |
| `eos_rust_socklen_t` | 4 | 4 | unsigned 32-bit scalar |
| `eos_rust_in_addr` | 4 | 4 | address at 0 |
| `eos_rust_in6_addr` | 16 | 1 | 16 address bytes at 0 |
| `eos_rust_sockaddr` | 16 | 2 | 16-bit family at 0, 14 data bytes at 2 |
| `eos_rust_sockaddr_in` | 16 | 4 | family 0, port 2, address 4, padding 8 |
| `eos_rust_sockaddr_in6` | 28 | 4 | family 0, port 2, flow 4, address 8, scope 24 |
| `eos_rust_sockaddr_storage` | 32 | 4 | family 0, fixed storage, alignment word 28 |
| `eos_rust_timeval` | 16 | 8 | signed 64-bit seconds 0, microseconds 8 |
| `eos_rust_pollfd` | 8 | 4 | fd 0, events 4, revents 6 |
| `eos_rust_addrinfo` (ARM) | 32 | 4 | scalars 0..16, pointers 20, 24, 28 |

Permanent compatibility constants pin `AF_UNSPEC/INET/INET6`, stream/datagram types, IP/TCP/UDP
protocols, shutdown modes, supported message flags, socket levels/options, poll bits, supported
AI flags, and EAI values. Values resemble Linux where useful but are not aliases for native
MARTOS values. Public ports and addresses are in network byte order. Stable network errno values
were added to the private compatibility map, and real-SDK assertions pin every MARTOS value used.

The 20 new exports are:

`eos_rust_socket`, `eos_rust_bind`, `eos_rust_connect`, `eos_rust_listen`, `eos_rust_accept`,
`eos_rust_send`, `eos_rust_recv`, `eos_rust_sendto`, `eos_rust_recvfrom`, `eos_rust_shutdown`,
`eos_rust_getsockname`, `eos_rust_getpeername`, `eos_rust_setsockopt`,
`eos_rust_getsockopt`, `eos_rust_poll`, `eos_rust_inet_pton`, `eos_rust_inet_ntop`,
`eos_rust_getaddrinfo`, `eos_rust_freeaddrinfo`, and `eos_rust_gai_strerror`.

The exact manifest contains 113 unique lines. Both host and MARTOS archives match it exactly.

## Socket ownership, state, and error policy

Each published socket descriptor references a heap compatibility object containing the native
socket handle, normalized domain/type/protocol, shared status flags, configured receive/send
timeouts, pending compatibility error, shutdown state, and a private state mutex. The descriptor
open object owns that allocation and native socket exactly once; duplicated descriptors share
the open object and state, while `FD_CLOEXEC` remains descriptor-local. Every operation holds an
FD lease, so close/reuse cannot destroy or retarget an in-flight operation. Final close performs
native close, mutex destruction, and allocation free outside the descriptor-table lock and
fails fast if irreversible cleanup fails.

Create and accept allocate/configure the compatibility object before descriptor publication.
Allocation, lock creation, timeout application, native nonblocking setup, and descriptor-table
exhaustion all close the unpublished native socket and release private resources. Accept failure
does not mutate the listening socket.

`F_GETFL` reports shared `O_RDWR | O_NONBLOCK`; `F_SETFL` accepts access-mode bits but changes
only nonblocking state. Enabling/disabling nonblocking applies both native timeouts plus the host
native flag transactionally. Second-option or native-flag failure rolls already-applied options
back, and failed state is never published. Configured blocking timeouts are retained while
nonblocking and restored when blocking resumes. Accepted sockets inherit shared status and both
timeouts. Positive sub-tick values round upward, zero means wait forever, and nonrepresentable
finite waits return `EOVERFLOW`.

`SO_RCVTIMEO`, `SO_SNDTIMEO`, `SO_SNDBUF`, `SO_RCVBUF`, and `SO_ERROR` are supported. `SO_ERROR`
returns and clears the compatibility pending error and queries native connection state for an
asynchronous connect. Options without an honest common/MARTOS implementation return
`ENOPROTOOPT`; host-only success is never advertised.

Transfers reject counts above `INT32_MAX` and invalid buffer/count combinations before
descriptor or native access. Nonnegative partial progress outranks any later error. The host
port captures native errno immediately and maps known networking failures, including native
permission failures to stable `EACCES`; MARTOS maps its
documented `EWOULDBLOCK`, `EINVAL`, `EADDRNOTAVAIL`, `EADDRINUSE`, `ENOBUFS`, `ENOPROTOOPT`, and
`ECLOSED` results operation-wise. Receive closure is zero bytes, other closure is connection
reset, connect would-block is in-progress, and unknown statuses conservatively become `EIO`.
Successful connects clear any older compatibility pending error.

## Address, poll, and resolver contracts

Common code validates public family and length before native access and converts through a fixed
private address: 16-bit normalized family, network-order port, flow information, 16 address
bytes, and scope id. Output is zero-initialized and bounded by the caller's length while always
reporting the required length. Host translation uses only host socket structures inside the host
port. MARTOS translation sets `sin_len`, its one-byte family, port, flowinfo, and address union;
nonzero IPv6 input scope returns `ENOTSUP`, and output scope is zero.

`poll` supports at most the 64-entry descriptor-table capacity. It ignores negative entries,
reports closed/stale/non-socket entries as `POLLNVAL` without changing caller errno, counts
duplicates independently, and leases every valid socket through result extraction. Read, write,
priority, error, and hangup are mapped. Host tests deterministically pause after native result to
prove close/reuse cannot retarget extraction. The MARTOS port creates a private socketset for
every call (including zero-entry calls), adds requested plus mandatory exception/interrupt bits,
selects, queries each entry, and deletes the set on every path; cleanup failure after an
irreversible operation is fail-fast. No readiness credit or socketset survives a call.
`TCP_CLOSED` becomes hangup only for sockets that have started a connection; this eligibility is
shared by duplicates and initialized for accepted sockets, so a fresh unconnected MARTOS TCP
socket does not report a false hangup.

IPv4 and IPv6 text parsing/formatting is strict common code, including leading/overflow IPv4
rejection, compression, embedded IPv4, longest-zero canonicalization, unsupported-family errno,
buffer bounds, and terminating null. `getaddrinfo` supports numeric IPv4/IPv6, null-node
wildcard/loopback, family filtering, stream/datagram and TCP/UDP consistency, numeric services,
`AI_PASSIVE`, `AI_CANONNAME`, `AI_NUMERICHOST`, and `AI_NUMERICSERV`. Ordering is IPv6 before
IPv4 for an unspecified null node, then stream before datagram. Each result, address, and optional
canonical string is a compatibility allocation; partial failure frees the entire private chain.
`freeaddrinfo(NULL)` is harmless, and independent calls share no mutable result storage.

## SDK and mutation evidence

The authoritative MARTOS 14.0.39 header was compiled directly with strict warnings. Target ELF
disassembly resolved the ambiguous `os_net_get_local_address` contract: success is the returned
native structure size (24), not status zero. Remote address uses its documented signed result.
Pointer sentinels distinguish no-socket from invalid-socket. MARTOS exposes no public DNS
primitive.

The shared MARTOS fake pins exact numeric constants, IPv4 and IPv6 normalized/native byte order
and fields, IPv6 scope rejection, all seven documented negative results plus generic/unknown
fallbacks, create/accept and socketset pointer sentinels, the actual receive/send timeout adapter's
socket/level/name/value/length arguments, local/remote returns, priority/error/hangup bits, and
create/add/select/query/delete socketset lifecycle including zero entries, each failure stage,
and cleanup fail-fast behavior.

Host-only deterministic seams cover delayed allocation at every resolver/publication point,
create/connect errors, exactly-once native close, partial send/receive, two-option timeout failure
and rollback, timeout argument logging, and poll result-extraction pause. Mutation-sensitive tests
cover descriptor exhaustion, accept publication rollback, duplicated shared flags/timeouts/error,
descriptor-local close-on-exec, nonblocking enable/restore rollback, partial-count precedence,
resolver partial allocation, and close/reuse.

## Final verification after independent-review fixes

All commands below were freshly configured or rerun after `bfde2832`:

- focused socket/poll/addrinfo/fd/pipe/ABI/MARTOS mapping/TSan/export suite: 15/15;
- fresh normal Release host configure/build/CTest: 39/39 in 101.27 seconds;
- fresh strict Debug `-O0 -Wall -Wextra -Werror -pedantic` C/C++
  configure/build/CTest: 39/39 in 103.61 seconds;
- fresh strict Release `-O3 -Wall -Wextra -Werror -pedantic` C/C++
  configure/build/CTest: 39/39 in 30.35 seconds;
- real MARTOS `BUILD_TESTING=OFF` Release build against
  `/home/dev/code/gpt-test/lib/martos-smp-14.0.39` with `-Wall -Wextra -Werror`: passed;
- exact host and MARTOS export checks: 113/113;
- MARTOS undefined audit: no global `errno`, `__errno_location`, `pthread_*`,
  `os_thread_wait`, or `os_thread_delete`;
- direct fresh `BUILD_TESTING=OFF` consumer compiling public network layouts and linking only
  `EOS::RustABI`: passed;
- ARM Cortex-A9 softfp/PIC probe: ELF32 little-endian ARM, EABI5, v7-A Application, Thumb-2,
  VFPv3, `REL`;
- `git diff --check`: clean.

The three networking TSan executables instrument both the compatibility library and executable.
All passed in every matrix without a race report. Socket-using host runs required only the scoped
sandbox bypass described above.

## Self-review and limitations

Self-review retraced create/accept publication, each descriptor lease, duplicated ownership,
native destruction, all nonblocking/timeout rollback edges, pending-error clear, bounded address
output, zero-byte receive closure, poll invalid/duplicate/result races, socketset cleanup, resolver
chain ownership, partial progress, and error precedence. It also checked all common sources for
native socket APIs/types, production archives for test seams, both exact export sets, MARTOS
undefineds, SDK drift assertions, new-file modes, and Task 9-only scope.

Deliberate limitations remain: MARTOS has no DNS API or IPv6 scope field; named hostname/service
resolution is unsupported; only options with an honest common contract are implemented; poll is
bounded by the 64-descriptor compatibility table; resolver results are numeric/null-node only;
and deployment to physical Zynq-7000 hardware remains manual. These are explicit v1 policies,
not silent host fallbacks.
