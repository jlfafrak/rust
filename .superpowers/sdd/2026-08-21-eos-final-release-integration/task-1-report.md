# Task 1 Report — Bind Results to One Exact Release-Manifest Snapshot

## Status

Task 1 binds signed EOS board results to one exact reviewed release-manifest byte snapshot. The
policy pins SHA-256
`fac60315f383ea6341e874a01d2b802e09419f1e5137e5b20129e3f9a160c4b5`, the digest of the reviewed
six-entry Task 16 manifest at
`/tmp/eos-task16-fix1-replacement-sdk/manifests/release-manifest.toml`.

## Root cause and RED evidence

`validate_release_manifest()` previously parsed the release TOML with `load_toml()` and later
hashed `path.read_bytes()` separately. It did not compare that digest with a trusted board-policy
constant. Thus a caller could replace the manifest after parsing or submit a changed valid manifest
while re-signing both result documents with that changed digest.

The first RED was run before production changes:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task1-red-pycache python3 -m unittest -v \
  tests.eos.board.test_check_results.BoardResultGateTests.test_rejects_resigned_results_for_substituted_release_manifest \
  tests.eos.board.test_check_results.BoardResultGateTests.test_release_manifest_parse_and_digest_share_one_byte_snapshot
```

It ran two tests and failed both as intended. The public checker accepted substituted,
correctly re-signed results with exit `0`; the same-path control observed `2 != 1` manifest reads.

## GREEN evidence

`load_toml_snapshot()` now reads bytes once, parses those bytes, and returns both values. The
release validator validates the parsed value, hashes those exact bytes, compares the digest with
the checked-in policy, and only then compares derived identities. Result semantics use the fixed
policy identity rather than assigning a caller-provided digest. The focused control GREEN command
passed 2/2:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task1-green-controls-pycache python3 -m unittest -v \
  tests.eos.board.test_check_results.BoardResultGateTests.test_rejects_resigned_results_for_substituted_release_manifest \
  tests.eos.board.test_check_results.BoardResultGateTests.test_release_manifest_parse_and_digest_share_one_byte_snapshot
```

The required full focused suite then passed 20/20 in 27.371 seconds:

```text
PYTHONPYCACHEPREFIX=/tmp/eos-final-task1-pycache \
python3 -m unittest -v tests.eos.board.test_check_results
```

## Files and scope

Changed only the Task 1 checker, policy/schema, synthetic fixture/tests, manual/release guidance,
Task 18 report, and this Task 1 report. The signed board-result schema no longer contains
`sdk_package_sha256_tree_v1`; it has an exact 64-hex `release_manifest_sha256`. The Task 17 SDK
tree pin, target, ABI, linker, authentication, capabilities, and hardware boundary were not
altered.

## Commit and concerns

The implementation is committed with subject `fix: bind EOS board results to release bytes`.
No authentic signed results, board hardware, transfer operation, or release bundle is present;
the overall release remains blocked on that external manual evidence.
