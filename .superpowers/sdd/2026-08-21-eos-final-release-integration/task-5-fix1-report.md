# Task 5 Fix Round 1 Report — EOS Release Snapshot Mutation

## Status and scope

Fix Round 1 is complete in commit
`0e79761ce22b6c788bf3483278373a85907c097d` with the required subject
`tests: restore EOS release snapshot mutation` and exact parent
`37100cb7de475d1f3a066865b1aba39e85038aca`.

The functional fix is limited to
`tests/eos/board/test_check_results.py`: the same-snapshot test now replaces the exact
`2e7ec8fc262ba7fed2e08fb77f7dc5d518a4580adbddf6ae6f6930033f1f7983` digest present in its
release-manifest fixture and explicitly asserts that `replacement_bytes != parsed_bytes` before
exercising the one-read snapshot behavior. The same commit amends only the Task 5 report to state
the mutation history accurately and document the reviewer's intermediate-archive evidence
boundary. No checker, builder, or other production behavior changed. The progress ledger was not
edited.

## Focused RED control

Before any edit, a focused control loaded the real `BoardResultGateTests` fixture with private
`PYTHONPYCACHEPREFIX=/tmp/eos-task5-fix1-red-pycache`, applied the then-current stale
`c9506399dd578571b953cb12eb8e981ddd6eec472f76c45baa8011d343fab6c9` mutation literal, and required
one occurrence plus changed replacement bytes. It exited nonzero for the intended reason:

~~~text
stale_digest_occurrences=0
replacement_changed=False
AssertionError: RED: same-snapshot mutation literal is absent, so replacement is a no-op
~~~

This proved the review finding directly: the pre-fix test's replacement did not mutate its
fixture, so its intended concurrency control was not actually exercised.

## GREEN and regression evidence

After the minimal test edit:

~~~text
focused same-snapshot unittest                         1/1 PASS (0.068s)
tests.eos.board.test_check_results                    21/21 PASS (12.005s)
python3 -Wall -Werror -m py_compile                    PASS
real retained SDK/static identity control              PASS
real retained release/capability identity control      PASS
source board policy == packaged board policy           PASS
scoped git diff --check                                PASS
~~~

The strict compilation covered `tests/eos/board/check_results.py` and
`tests/eos/board/test_check_results.py` with a private
`PYTHONPYCACHEPREFIX=/tmp/eos-task5-fix1-compile-pycache`. The real static identity control
recomputed exact SDK sha256-tree-v1
`919c17065b7b33626ade510f63c4febb2fda56413c6215f9ebeca019e67f7ae7` and ARM GNU input
sha256-tree-v1 `a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d`. The board control
validated release-manifest SHA-256
`6aab31c83e79e891dc86b37df843f645d39720e48d0eba0351e4666b75a01150`, all 35 reviewed
capabilities, and exact backtrace revision
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`. The checked-in and packaged
`board-test-manifest.toml` files compared byte-for-byte equal.

## No-rebuild and source-identity boundary

No `build-eos-sdk` process was launched and no SDK build or distribution step was repeated. The
existing `/tmp/eos-final-release-sdk` was preserved in place and used read-only for the real
identity controls. Its directory mtime remained `2026-08-24 12:46:53.549407300 -0600`, and no
live `build-eos-sdk` process existed during verification.

The backtrace gitlink and checked-out nested repository both remained exactly
`02ef1b533157e8ddbd0f9295c867e79b59e9bbbd`; the nested porcelain was empty.

## Reviewer concern disposition

The load-bearing review finding is addressed: the mutation literal now matches the pinned fixture
digest, the explicit inequality assertion prevents the same no-op from silently returning, and
the focused plus full board tests pass.

The reviewer's Minor evidence-boundary concern is documented without overstating payload
verification. The six intermediate distribution archives were cleaned with build scratch and are
not packaged in the final SDK, so their final payload hashes cannot now be independently
recomputed. Their archive names and digests originate from builder-side hashing and are
cryptographically bound by the independently verified generated release manifest. The evidence
therefore supports the recorded manifest identities, not a post-build rehash of archive payloads.
