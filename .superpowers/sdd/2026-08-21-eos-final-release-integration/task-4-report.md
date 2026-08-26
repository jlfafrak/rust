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

## Fix Round 1 — executable board-gate control and build-head boundary

The original policy test inspected matching source lines, which a valid quoted heredoc could
preserve while preventing the shell from executing the board checker. The independent mutation
replaced the executable command with such a heredoc: the old text-only test incorrectly stayed
GREEN, proving the false-positive. The replacement control copies `run-ci.sh` to a temporary
fixture, supplies only a fake SDK directory, ARM path files, and initial-path commands required
to reach the gate, and resolves a fake initial `python3` as `TRUSTED_PYTHON`. That shim logs every
argument vector and succeeds only for the Python version check, identity preflight, and exact
board-suite invocation. The control requires the identity call before the exact
`-m unittest -v tests.eos.board.test_check_results` call. The temporary quoted-heredoc mutation
then omits that logged invocation and is RED; the restored real entrypoint is GREEN. The fixture
fails only after the gate at its intentionally incomplete later tool check.

The full requested unittest command returned nonzero solely from the two SDK-dependent existing
setup errors (`test_ci_entrypoint_rejects_mutated_sdk_before_running_tools` and
`test_ci_entrypoint_rejects_mutation_before_cmake_python_can_run`), both caused by the absent
`EOS_RUST_SDK_ROOT`; the 24 environment-available tests passed.

There is no frozen Task 5 build hash in this report. Task 5 must use the final Task 4 Fix Round
head recorded in the SDD ledger and revalidate that head immediately before its one permitted
builder invocation. This Fix Round makes one combined test/report commit and no docs-only child.
