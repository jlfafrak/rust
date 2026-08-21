# Manual EOS Rust two-board test

This procedure is intentionally manual. The repository contains no command that transfers an
image to physical hardware or starts an application on a board. Use only the organization's
approved transfer and EOS console procedures. Keep credentials, board locations, and local
operational details outside the repository and outside result files.

## Inputs

Use one reviewed SDK package and its unmodified `manifests/release-manifest.toml`. Retain the
unstripped ELF, GNU build ID, and authenticated ELF SHA-256 value for every application in both
debug and release profiles. The required application names, identities, rows, and part families
are fixed by `tests/eos/board/board-test-manifest.toml`.

Use the identical authenticated application bytes on XC7Z030 and XC7Z045 wherever the shared
target permits it. The checker compares every corresponding build ID and authenticated SHA-256
value across the two results and fails if they differ.

## Capture procedure

For each part family:

1. Record the board module/part and its EOS version.
2. Transfer the authenticated debug and release bundles by the organization's approved manual
   method.
3. Through the EOS console, start each profile at two distinct organization-approved PIE load
   addresses. Record each as canonical lowercase `0x` plus eight hexadecimal digits; differently
   cased spellings of one numeric address are not distinct. Do not put board network locations or
   credentials in the result.
4. At each of the four profile/address combinations, capture a complete console evidence set for
   every v1 acceptance row:
   `pie_load_relocation`, `arguments`, `environment`, `current_working_directory`,
   `standard_io`, `allocation`, `files_and_directories`, `threads`, `rust_tls`,
   `synchronization`, `monotonic_and_realtime_time`, `tcp`, `udp`,
   `numeric_socket_addresses`, `process_spawn_wait_kill`, `panic_catch`,
   `panic_destructor_cleanup`, `ffi_panic_containment`, and `backtrace_addresses`.
5. Record every observed failure. Use an empty `observed_failures` array only when none occurred;
   never omit the field or discard a failure.
6. Record every audited capability row. A false matrix entry must be observed as `unsupported`
   with error `Unsupported`. The reviewed v1 optional rows remain false; promoting one requires
   a reviewed policy change backed by both contract and board evidence.
7. Write one JSON result that satisfies `tests/eos/board/result.schema.json`.

Both debug and release profiles require exactly two `runs`, keyed by their distinct load
addresses. Each run contains the exact complete v1 acceptance set; one global set is not evidence
for the four separate combinations. Each profile also records a GNU build ID plus
authenticated-file SHA-256 for every listed application. A failed acceptance row or any observed
failure blocks the release; it is not converted to an unsupported optional feature.

## Signing boundary

The repository contains no release signing key. Submit each completed result to the
organization-controlled signing boundary. It signs the UTF-8 bytes produced by:

```python
json.dumps(
    {key: value for key, value in result.items() if key != "signature"},
    sort_keys=True,
    separators=(",", ":"),
    ensure_ascii=True,
).encode("ascii")
```

The signature algorithm is `RSASSA-PKCS1-v1_5-SHA256`. Add exactly one `signature` object with
the algorithm, the approved public-key identifier, and the base64 signature value. The checker
accepts only RSA keys of at least 2048 bits.

An organization-controlled public-key JSON file has this shape:

```json
{
  "schema_version": 1,
  "key_id": "organization-key-identifier",
  "algorithm": "RSASSA-PKCS1-v1_5-SHA256",
  "modulus_hex": "lowercase-public-modulus-without-a-leading-zero",
  "public_exponent": 65537
}
```

The public key is supplied to the checker at release time and is not embedded in the repository.
Private key material must never be passed to the checker or stored with the results.

## Local verification

Run the checker with the exact reviewed release manifest, organization-supplied public key, and
the two independently captured result paths:

```text
python3 tests/eos/board/check_results.py \
  --release-manifest <reviewed-sdk>/manifests/release-manifest.toml \
  --trusted-key <organization-public-key.json> \
  <xc7z030-result.json> \
  <xc7z045-result.json>
```

Repeat `--trusted-key` only when the organization is rotating approved public keys. The checker
validates the immutable checked-in JSON schemas and board/capability policies, the exact Task 16
TOML release-manifest shape, reviewed release identities, signatures,
profile/address/application sets, every v1 row in every run, capability semantics, and
cross-board binary identity. Its public CLI accepts only the release manifest, repeatable trusted
public keys, and exactly two result paths. Synthetic fixtures in the unit tests are explicitly
test-only and are not hardware or release evidence.
