# Task 4 Report — CI Gate for Manual Board Results

## Delivered gate

`tests/eos/run-ci.sh` retains the workflow's single-command and no-physical-board-action shape.
Immediately after the reviewed release-identity preflight and before static artifact generation,
it runs the immutable Task 18 board-result suite with the preflight-resolved interpreter:

```text
"$TRUSTED_PYTHON" -m unittest -v tests.eos.board.test_check_results
```

No target, ABI, linker, validator, authentication, unwind, capability, builder, installer,
layout, application, backtrace, or progress-ledger behavior changed. No hardware interaction,
result material, credentials, keys, deployment material, or SDK builder invocation occurred.

## RED, mutation control, and focused verification

The added `CiEntryPointPolicyTests` test parses `run-ci.sh`, requires the trusted board-checker
invocation after the identity preflight and before layout generation, and removes that line as a
mutation control. Before the production line existed, the test was RED with exactly
`AssertionError: CI entrypoint omits the board result gate`. With the line restored, the new test
was GREEN. The focused available command ran the immutable board suite and all policy cases that
need no reviewed SDK: 24 unittest methods passed (the board suite's 21 methods cover its recorded
42 behavioral checks, plus three policy tests). `bash -n tests/eos/run-ci.sh` and strict
`python3 -W error -m py_compile` for the host policy and board test modules passed. Staged
`git diff --check` passed; the production commit scope is exactly the CI script, host policy test,
and Task 17 report, with `run-ci.sh` retaining mode `100755`.

## Evidence boundary

The specified full focused command including the entire `CiEntryPointPolicyTests` class was run.
The board suite, fail-closed-without-SDK check, new board-gate policy check, and workflow policy
check passed. The two existing SDK identity-mutation tests could not initialize because
`EOS_RUST_SDK_ROOT` is unset and the environment reset removed
`/tmp/eos-task16-fix1-replacement-sdk`; both stopped at the resulting `KeyError`. The former
CMake runtime is also absent. No substitute SDK was fabricated and no SDK builder was run,
because the one additionally permitted real builder invocation is reserved for Task 5.

Accordingly, the historical old-SDK full `run-ci.sh` command was not re-run. This is not a gate
waiver: the exact integrated entrypoint must run against the new final SDK in Task 6. Previously
recorded fresh evidence remains Task 2 artifacts 42/42 and Task 3 focused/full SDK-board suites
35/35, 79/79, and 21/21.

## PREBUILD_HEAD

The clean production/test/report prebuild head is
`4f82cf559f03d424fe031775c4934072dd2ce0ca` (`ci: gate EOS manual release policy`).
