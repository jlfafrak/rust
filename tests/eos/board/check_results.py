#!/usr/bin/env python3
"""Fail-closed validator for organization-signed EOS manual board results."""

from __future__ import annotations

import argparse
import base64
import binascii
from datetime import datetime
import hashlib
import hmac
import json
from pathlib import Path
import re
import sys
import tomllib
from typing import Any, NamedTuple


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MANIFEST = Path(__file__).with_name("board-test-manifest.toml")
DEFAULT_RESULT_SCHEMA = Path(__file__).with_name("result.schema.json")
DEFAULT_RELEASE_SCHEMA = (
    REPO_ROOT / "src" / "tools" / "eos-sdk" / "manifests" / "release-manifest.schema.json"
)
DEFAULT_CAPABILITIES = (
    REPO_ROOT / "src" / "tools" / "eos-sdk" / "manifests" / "capabilities.toml"
)
SHA256_DIGEST_INFO = bytes.fromhex("3031300d060960864801650304020105000420")
SUPPORTED_SIGNATURE = "RSASSA-PKCS1-v1_5-SHA256"


class PolicyPaths(NamedTuple):
    manifest: Path
    result_schema: Path
    release_schema: Path
    capabilities: Path


CHECKED_IN_POLICY = PolicyPaths(
    manifest=DEFAULT_MANIFEST,
    result_schema=DEFAULT_RESULT_SCHEMA,
    release_schema=DEFAULT_RELEASE_SCHEMA,
    capabilities=DEFAULT_CAPABILITIES,
)


class GateError(Exception):
    """A release input failed a closed validation gate."""


def duplicate_rejecting_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise GateError(f"duplicate JSON member {key!r}")
        result[key] = value
    return result


def load_json(path: Path, description: str) -> Any:
    try:
        if not path.is_file():
            raise GateError(f"missing {description}: {path}")
        return json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=duplicate_rejecting_object
        )
    except GateError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateError(f"unable to read {description} {path}: {error}") from error


def load_toml(path: Path, description: str) -> dict[str, Any]:
    try:
        if not path.is_file():
            raise GateError(f"missing {description}: {path}")
        value = tomllib.loads(path.read_text(encoding="utf-8"))
    except GateError:
        raise
    except (OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        raise GateError(f"unable to read {description} {path}: {error}") from error
    if not isinstance(value, dict):
        raise GateError(f"{description} root must be an object")
    return value


def load_toml_snapshot(path: Path, description: str) -> tuple[dict[str, Any], bytes]:
    try:
        if not path.is_file():
            raise GateError(f"missing {description}: {path}")
        data = path.read_bytes()
        value = tomllib.loads(data.decode("utf-8"))
    except GateError:
        raise
    except (OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        raise GateError(f"unable to read {description} {path}: {error}") from error
    if not isinstance(value, dict):
        raise GateError(f"{description} root must be an object")
    return value, data


def json_type_matches(value: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    raise GateError(f"schema uses unsupported type {expected!r}")


def json_schema_equal(left: Any, right: Any) -> bool:
    """Compare JSON values without treating booleans as the integers zero and one."""
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    return left == right


def validate_schema(value: Any, schema: dict[str, Any], path: str = "$") -> None:
    """Validate the JSON Schema subset used by the two checked-in v1 schemas."""
    if not isinstance(schema, dict):
        raise GateError(f"invalid schema node at {path}")
    if "const" in schema and not json_schema_equal(value, schema["const"]):
        raise GateError(f"{path} must equal {schema['const']!r}")
    if "enum" in schema and not any(
        json_schema_equal(value, candidate) for candidate in schema["enum"]
    ):
        raise GateError(f"{path} is not one of {schema['enum']!r}")
    expected_type = schema.get("type")
    if expected_type is not None:
        if not isinstance(expected_type, str) or not json_type_matches(value, expected_type):
            raise GateError(f"{path} must have JSON type {expected_type}")

    if isinstance(value, dict):
        required = schema.get("required", [])
        if not isinstance(required, list) or any(not isinstance(item, str) for item in required):
            raise GateError(f"schema has invalid required list at {path}")
        missing = [item for item in required if item not in value]
        if missing:
            raise GateError(f"{path} is missing required member(s): {', '.join(missing)}")
        minimum = schema.get("minProperties")
        if minimum is not None and len(value) < minimum:
            raise GateError(f"{path} requires at least {minimum} properties")
        properties = schema.get("properties", {})
        if not isinstance(properties, dict):
            raise GateError(f"schema has invalid properties at {path}")
        property_names = schema.get("propertyNames")
        if property_names is not None:
            for key in value:
                validate_schema(key, property_names, f"{path}.<property-name>")
        additional = schema.get("additionalProperties", True)
        for key, member in value.items():
            if key in properties:
                validate_schema(member, properties[key], f"{path}.{key}")
            elif additional is False:
                raise GateError(f"{path} has unexpected member {key!r}")
            elif isinstance(additional, dict):
                validate_schema(member, additional, f"{path}.{key}")
            elif additional is not True:
                raise GateError(f"schema has invalid additionalProperties at {path}")

    if isinstance(value, list):
        minimum = schema.get("minItems")
        maximum = schema.get("maxItems")
        if minimum is not None and len(value) < minimum:
            raise GateError(f"{path} requires at least {minimum} items")
        if maximum is not None and len(value) > maximum:
            raise GateError(f"{path} allows at most {maximum} items")
        if schema.get("uniqueItems"):
            encoded = [
                json.dumps(item, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
                for item in value
            ]
            if len(encoded) != len(set(encoded)):
                raise GateError(f"{path} must contain unique items")
        item_schema = schema.get("items")
        if item_schema is not None:
            for index, item in enumerate(value):
                validate_schema(item, item_schema, f"{path}[{index}]")

    if isinstance(value, str):
        minimum = schema.get("minLength")
        if minimum is not None and len(value) < minimum:
            raise GateError(f"{path} must contain at least {minimum} characters")
        pattern = schema.get("pattern")
        if pattern is not None:
            try:
                matches = re.search(pattern, value)
            except re.error as error:
                raise GateError(f"schema has invalid pattern at {path}: {error}") from error
            if matches is None:
                raise GateError(f"{path} does not match {pattern!r}")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        minimum = schema.get("minimum")
        maximum = schema.get("maximum")
        if minimum is not None and value < minimum:
            raise GateError(f"{path} must be at least {minimum}")
        if maximum is not None and value > maximum:
            raise GateError(f"{path} must be at most {maximum}")


def require_exact_string_list(container: dict[str, Any], name: str) -> tuple[str, ...]:
    value = container.get(name)
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(item, str) or not item for item in value)
        or len(value) != len(set(value))
    ):
        raise GateError(f"board test manifest {name} must be a nonempty unique string list")
    return tuple(value)


def load_board_manifest(path: Path) -> dict[str, Any]:
    manifest = load_toml(path, "board test manifest")
    required = {
        "schema_version",
        "parts",
        "profiles",
        "applications",
        "acceptance_rows",
        "supported_capabilities",
        "required_false_capabilities",
        "optional_capabilities",
        "expected",
        "signature",
    }
    if set(manifest) != required or manifest.get("schema_version") != 1:
        raise GateError("board test manifest has unexpected v1 structure")
    if require_exact_string_list(manifest, "parts") != ("XC7Z030", "XC7Z045"):
        raise GateError("board test manifest must name XC7Z030 and XC7Z045 exactly")
    if require_exact_string_list(manifest, "profiles") != ("debug", "release"):
        raise GateError("board test manifest must name debug and release profiles exactly")
    for name in (
        "applications",
        "acceptance_rows",
        "supported_capabilities",
        "required_false_capabilities",
        "optional_capabilities",
    ):
        require_exact_string_list(manifest, name)
    capability_groups = (
        manifest["supported_capabilities"],
        manifest["required_false_capabilities"],
        manifest["optional_capabilities"],
    )
    reviewed_capabilities = [item for group in capability_groups for item in group]
    if len(reviewed_capabilities) != len(set(reviewed_capabilities)):
        raise GateError("board test manifest capability inventories must be disjoint")
    expected = manifest.get("expected")
    signature = manifest.get("signature")
    if not isinstance(expected, dict) or not expected:
        raise GateError("board test manifest expected identities are missing")
    if signature != {
        "algorithm": SUPPORTED_SIGNATURE,
        "minimum_rsa_bits": 2048,
        "canonicalization": "python-json-sort-keys-ascii-v1",
    }:
        raise GateError("board test manifest signature policy is not the reviewed v1 policy")
    return manifest


def validate_release_manifest(
    path: Path, schema_path: Path, expected: dict[str, Any]
) -> tuple[dict[str, Any], str]:
    release, data = load_toml_snapshot(path, "release manifest")
    schema = load_json(schema_path, "release-manifest JSON schema")
    try:
        validate_schema(release, schema)
    except GateError as error:
        raise GateError(f"release manifest does not satisfy its schema: {error}") from error

    distributions = release["distribution_hashes"]
    names = tuple(distributions)
    target = release["release"]["target"]
    if not any("rustc" in name for name in names):
        raise GateError("release manifest has no rustc distribution archive")
    if not any("cargo" in name for name in names):
        raise GateError("release manifest has no Cargo distribution archive")
    if not any("rust-std" in name and target in name for name in names):
        raise GateError("release manifest has no target rust-std distribution archive")

    digest = hashlib.sha256(data).hexdigest()
    if expected.get("release_manifest_sha256") != digest:
        raise GateError("release manifest does not match exact reviewed release-manifest digest")

    derived = {
        "sdk_version": release["release"]["version"],
        "toolchain": release["release"]["toolchain"],
        "target": target,
        "layout_version": release["release"]["layout_version"],
        "rust_fork": release["source_revisions"]["rust_fork"],
        "rust_upstream": release["source_revisions"]["rust_upstream"],
        "libc_upstream": release["source_revisions"]["libc_upstream"],
        "backtrace": release["source_revisions"]["backtrace"],
        "arm_gnu_sha256_tree_v1": release["input_hashes"]["arm_gnu_installed"],
        "eos_sdk_sha256_tree_v1": release["input_hashes"]["eos_sdk"],
    }
    for name, value in derived.items():
        if expected.get(name) != value:
            raise GateError(
                f"release manifest {name} does not match reviewed board-test identity"
            )
    return release, digest


def load_capabilities(path: Path, manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    document = load_toml(path, "capability matrix")
    if set(document) != {"matrix", "capability"} or document.get("matrix") != {
        "schema_version": 1,
        "target": "armv7a-unknown-eos-eabi",
        "unsupported_error": "Unsupported",
    }:
        raise GateError("capability matrix has unexpected v1 structure")
    entries = document.get("capability")
    if not isinstance(entries, list) or not entries:
        raise GateError("capability matrix has no entries")
    capabilities: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise GateError(f"capability matrix entry {index} is not a table")
        required = {"id", "supported", "contract_evidence", "board_evidence", "evidence"}
        allowed = required | {"unsupported_error"}
        if set(entry) - allowed or not required.issubset(entry):
            raise GateError(f"capability matrix entry {index} has unexpected structure")
        capability_id = entry["id"]
        if not isinstance(capability_id, str) or not capability_id or capability_id in capabilities:
            raise GateError(f"capability matrix entry {index} has a duplicate or invalid id")
        if any(
            not isinstance(entry[field], bool)
            for field in ("supported", "contract_evidence", "board_evidence")
        ) or not isinstance(entry["evidence"], str) or not entry["evidence"]:
            raise GateError(f"capability matrix entry {capability_id} is malformed")
        if not entry["supported"] and entry.get("unsupported_error") != "Unsupported":
            raise GateError(f"capability {capability_id} lacks stable Unsupported semantics")
        capabilities[capability_id] = entry

    reviewed = tuple(
        manifest["supported_capabilities"]
        + manifest["required_false_capabilities"]
        + manifest["optional_capabilities"]
    )
    if set(capabilities) != set(reviewed):
        missing = sorted(set(reviewed) - set(capabilities))
        extra = sorted(set(capabilities) - set(reviewed))
        raise GateError(
            f"capability matrix differs from exact reviewed inventory "
            f"(missing={missing}, extra={extra})"
        )
    for capability_id in manifest["supported_capabilities"]:
        entry = capabilities[capability_id]
        if not entry["supported"] or not entry["contract_evidence"]:
            raise GateError(
                f"capability {capability_id} must be true with contract evidence"
            )
        if "unsupported_error" in entry:
            raise GateError(f"supported capability {capability_id} cannot claim Unsupported")
    false_capabilities = tuple(
        manifest["required_false_capabilities"] + manifest["optional_capabilities"]
    )
    for capability_id in false_capabilities:
        entry = capabilities[capability_id]
        if entry["supported"] or entry.get("unsupported_error") != "Unsupported":
            raise GateError(
                f"capability {capability_id} must remain false with stable Unsupported semantics"
            )
        if entry["contract_evidence"] and entry["board_evidence"]:
            raise GateError(
                f"capability {capability_id} is false despite complete evidence"
            )
    return capabilities


def canonical_result(result: dict[str, Any]) -> bytes:
    unsigned = {key: value for key, value in result.items() if key != "signature"}
    return json.dumps(
        unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


def load_trusted_keys(
    paths: list[Path], minimum_bits: int
) -> dict[str, tuple[int, int]]:
    keys: dict[str, tuple[int, int]] = {}
    for path in paths:
        key = load_json(path, "trusted public key")
        required = {
            "schema_version",
            "key_id",
            "algorithm",
            "modulus_hex",
            "public_exponent",
        }
        if not isinstance(key, dict) or set(key) != required:
            raise GateError(f"trusted public key {path} has unexpected structure")
        if key["schema_version"] != 1 or key["algorithm"] != SUPPORTED_SIGNATURE:
            raise GateError(f"trusted public key {path} uses an unsupported policy")
        key_id = key["key_id"]
        modulus_hex = key["modulus_hex"]
        exponent = key["public_exponent"]
        if (
            not isinstance(key_id, str)
            or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", key_id) is None
            or key_id in keys
            or not isinstance(modulus_hex, str)
            or re.fullmatch(r"[0-9a-f]+", modulus_hex) is None
            or modulus_hex.startswith("0")
            or not isinstance(exponent, int)
            or isinstance(exponent, bool)
            or exponent < 3
            or exponent % 2 == 0
        ):
            raise GateError(f"trusted public key {path} is malformed")
        modulus = int(modulus_hex, 16)
        if modulus.bit_length() < minimum_bits:
            raise GateError(f"trusted public key {key_id} is smaller than {minimum_bits} bits")
        keys[key_id] = (modulus, exponent)
    return keys


def verify_signature(
    result: dict[str, Any], trusted_keys: dict[str, tuple[int, int]], source: Path
) -> None:
    signature = result["signature"]
    key_id = signature["key_id"]
    if signature["algorithm"] != SUPPORTED_SIGNATURE or key_id not in trusted_keys:
        raise GateError(f"result {source} signature key is not trusted for the v1 algorithm")
    modulus, exponent = trusted_keys[key_id]
    width = (modulus.bit_length() + 7) // 8
    try:
        signature_bytes = base64.b64decode(signature["value"], validate=True)
    except (ValueError, binascii.Error) as error:
        raise GateError(f"result {source} signature is not valid base64") from error
    if len(signature_bytes) != width:
        raise GateError(f"result {source} signature has the wrong RSA width")
    signature_integer = int.from_bytes(signature_bytes, "big")
    if signature_integer >= modulus:
        raise GateError(f"result {source} signature is not a canonical RSA integer")
    encoded = pow(signature_integer, exponent, modulus).to_bytes(width, "big")
    digest_info = SHA256_DIGEST_INFO + hashlib.sha256(canonical_result(result)).digest()
    expected = b"\x00\x01" + b"\xff" * (width - len(digest_info) - 3) + b"\x00" + digest_info
    if not hmac.compare_digest(encoded, expected):
        raise GateError(f"result {source} signature verification failed")


def exact_rows(rows: list[dict[str, Any]], description: str) -> dict[str, dict[str, Any]]:
    indexed: dict[str, dict[str, Any]] = {}
    for row in rows:
        row_id = row["id"]
        if row_id in indexed:
            raise GateError(f"duplicate {description} row {row_id}")
        indexed[row_id] = row
    return indexed


def validate_result_semantics(
    result: dict[str, Any],
    source: Path,
    manifest: dict[str, Any],
    capabilities: dict[str, dict[str, Any]],
) -> None:
    captured_at = result["captured_at_utc"]
    try:
        parsed_timestamp = datetime.strptime(captured_at, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise GateError(f"result {source} captured_at_utc is not a valid UTC timestamp") from error
    if parsed_timestamp.strftime("%Y-%m-%dT%H:%M:%SZ") != captured_at:
        raise GateError(f"result {source} captured_at_utc is not canonical")

    expected = dict(manifest["expected"])
    identities = result["identities"]
    if set(identities) != set(expected):
        missing = sorted(set(expected) - set(identities))
        extra = sorted(set(identities) - set(expected))
        raise GateError(f"result {source} identity fields differ (missing={missing}, extra={extra})")
    for name, value in expected.items():
        if identities[name] != value:
            raise GateError(f"result {source} identity {name} does not match reviewed release")
    if result["board"]["eos_version"] != expected["eos_sdk_baseline"]:
        raise GateError(f"result {source} EOS version does not match the reviewed baseline")

    profiles = {profile["name"]: profile for profile in result["profiles"]}
    if set(profiles) != set(manifest["profiles"]) or len(profiles) != len(result["profiles"]):
        raise GateError(f"result {source} must contain debug and release exactly once")
    expected_applications = set(manifest["applications"])
    profile_load_addresses: dict[str, set[int]] = {}
    for name, profile in profiles.items():
        load_addresses = [run["load_address"] for run in profile["runs"]]
        numeric_load_addresses = [int(address, 16) for address in load_addresses]
        if len(load_addresses) != 2 or len(set(numeric_load_addresses)) != 2:
            raise GateError(f"result {source} {name} profile needs two distinct load addresses")
        profile_load_addresses[name] = set(numeric_load_addresses)
        applications = {item["name"]: item for item in profile["applications"]}
        if (
            set(applications) != expected_applications
            or len(applications) != len(profile["applications"])
        ):
            raise GateError(f"result {source} {name} profile application/build_id set is incomplete")
        for run in profile["runs"]:
            acceptance = exact_rows(run["acceptance"], "acceptance")
            if set(acceptance) != set(manifest["acceptance_rows"]):
                raise GateError(
                    f"result {source} {name} {run['load_address']} does not contain "
                    "the exact v1 acceptance rows"
                )
            failed = sorted(
                row_id for row_id, row in acceptance.items() if row["status"] != "pass"
            )
            if failed:
                raise GateError(
                    f"result {source} {name} {run['load_address']} acceptance rows "
                    f"did not pass: {', '.join(failed)}"
                )
    if len({frozenset(addresses) for addresses in profile_load_addresses.values()}) != 1:
        raise GateError(
            f"result {source} debug and release profiles must use the same two load addresses"
        )
    if result["observed_failures"]:
        raise GateError(f"result {source} records observed failures and cannot release")

    audited = tuple(
        manifest["supported_capabilities"]
        + manifest["required_false_capabilities"]
        + manifest["optional_capabilities"]
    )
    reported = exact_rows(result["capabilities"], "capability")
    if set(reported) != set(audited):
        raise GateError(f"result {source} does not contain the exact audited capability rows")
    for capability_id in audited:
        matrix = capabilities[capability_id]
        row = reported[capability_id]
        if matrix["supported"]:
            valid = row["status"] == "pass" and "error" not in row
        else:
            valid = row["status"] == "unsupported" and row.get("error") == "Unsupported"
        if not valid:
            raise GateError(
                f"result {source} capability {capability_id} conflicts with the matrix"
            )


def compare_board_artifacts(
    first: dict[str, Any], second: dict[str, Any], manifest: dict[str, Any]
) -> None:
    first_profiles = {profile["name"]: profile for profile in first["profiles"]}
    second_profiles = {profile["name"]: profile for profile in second["profiles"]}
    for profile_name in manifest["profiles"]:
        first_apps = {
            application["name"]: application
            for application in first_profiles[profile_name]["applications"]
        }
        second_apps = {
            application["name"]: application
            for application in second_profiles[profile_name]["applications"]
        }
        for application in manifest["applications"]:
            if first_apps[application] != second_apps[application]:
                raise GateError(
                    "authenticated binaries differ across boards for "
                    f"{application} {profile_name}"
                )


def parse_args(arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify exactly one signed XC7Z030 and XC7Z045 manual result."
    )
    parser.add_argument("--release-manifest", type=Path, required=True)
    parser.add_argument(
        "--trusted-key",
        type=Path,
        action="append",
        required=True,
        help="organization-controlled public-key JSON; repeat for key rotation",
    )
    parser.add_argument("results", type=Path, nargs="+")
    return parser.parse_args(arguments)


def _run_with_policy(arguments: list[str], policy: PolicyPaths) -> int:
    options = parse_args(arguments)
    if len(options.results) != 2:
        raise GateError("exactly two board result paths are required")
    manifest = load_board_manifest(policy.manifest)
    result_schema = load_json(policy.result_schema, "board-result JSON schema")
    validate_release_manifest(
        options.release_manifest, policy.release_schema, manifest["expected"]
    )
    capabilities = load_capabilities(policy.capabilities, manifest)
    trusted_keys = load_trusted_keys(
        options.trusted_key, manifest["signature"]["minimum_rsa_bits"]
    )

    results: list[tuple[Path, dict[str, Any]]] = []
    for path in options.results:
        result = load_json(path, "board result")
        if not isinstance(result, dict):
            raise GateError(f"board result {path} root must be an object")
        try:
            validate_schema(result, result_schema)
        except GateError as error:
            raise GateError(f"board result {path} does not satisfy its schema: {error}") from error
        verify_signature(result, trusted_keys, path)
        validate_result_semantics(
            result, path, manifest, capabilities
        )
        results.append((path, result))

    by_part: dict[str, dict[str, Any]] = {}
    for path, result in results:
        part = result["board"]["part"]
        if part in by_part:
            raise GateError(f"expected exactly one signed result for each board; duplicate {part}")
        by_part[part] = result
    required_parts = set(manifest["parts"])
    if set(by_part) != required_parts:
        raise GateError(
            "expected exactly one signed result for each board: XC7Z030 and XC7Z045"
        )
    compare_board_artifacts(by_part["XC7Z030"], by_part["XC7Z045"], manifest)
    print("verified signed XC7Z030 and XC7Z045 board results against reviewed release inputs")
    return 0


def run(arguments: list[str]) -> int:
    return _run_with_policy(arguments, CHECKED_IN_POLICY)


def main() -> int:
    try:
        return run(sys.argv[1:])
    except GateError as error:
        print(f"board result gate failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
