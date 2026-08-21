# Releasing the EOS Rust SDK

An EOS Rust release has two independent evidence layers: automated host/static gates and manual,
organization-signed hardware results. Neither layer substitutes for the other. The release is not
complete until both signed board results pass the checker.

## Automated evidence

Run the exact Task 17 entrypoint with the reviewed SDK, complete reviewed ARM GNU tools, and the
reviewed CMake runtime. Preserve its SDK and static evidence artifacts. The entrypoint performs
no physical-board action.

The release manifest passed to the board checker is the unmodified Task 16
`manifests/release-manifest.toml`. Its parsed data must satisfy
`src/tools/eos-sdk/manifests/release-manifest.schema.json`, including the exact four top-level
tables, fixed release identity, `sha256-tree-v1` input hashes, source-revision shapes, safe
distribution archive names, and lowercase SHA-256 digests.

## Manual evidence

Follow `docs/eos/manual-board-test.md`. Organization operators manually transfer and start the
same authenticated binaries on XC7Z030 and XC7Z045, capture two distinct PIE placements per
profile, record the complete row set separately for each profile/address run plus all observed
failures, and obtain one organization signature for each result. No unsigned, partially
populated, mismatched, or synthetic result is release evidence.

## Final gates

With authentic files available, run:

```text
tests/eos/run-ci.sh

python3 tests/eos/board/check_results.py \
  --release-manifest <reviewed-sdk>/manifests/release-manifest.toml \
  --trusted-key <organization-public-key.json> \
  release-results/xc7z030.json \
  release-results/xc7z045.json

src/tools/eos-sdk/bin/eos-elf-validate \
  release-bundle/hello-std.elf \
  --allow-auth-trailer
```

The checker fails closed unless it receives exactly one signed result for each part family. Both
results must match the reviewed SDK, Rust, libc, backtrace, ARM GNU, EOS baseline, native ABI,
linker script, and release-manifest identities. They must contain debug and release profiles,
identical cross-board application artifacts, two numerically distinct canonical lowercase
load-address runs per profile, the exact application/build-ID set, every passing v1 acceptance
row in all four runs, the exact complete capability inventory, and an explicit empty
observed-failure list.

Every reviewed v1 optional capability remains false, and both board results must report
`unsupported` with `Unsupported`, matching the capability matrix. Promoting one requires a
reviewed policy change backed by both contract and board evidence. The fixed v1 exclusions
remain false.

If either physical board, either authentic result, the organization public key, or any matching
release artifact is unavailable, record that exact environmental boundary and stop. Do not copy
test fixtures into `release-results`, invent a signature or outcome, omit a failure, or describe
the overall release as complete.
