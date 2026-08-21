#!/usr/bin/env python3
"""Behavioral tests for the fail-closed manual board result gate."""

from __future__ import annotations

import base64
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from contextlib import redirect_stderr, redirect_stdout


REPO_ROOT = Path(__file__).resolve().parents[3]
CHECKER = REPO_ROOT / "tests" / "eos" / "board" / "check_results.py"
BOARD_MANIFEST = REPO_ROOT / "tests" / "eos" / "board" / "board-test-manifest.toml"
RESULT_SCHEMA = REPO_ROOT / "tests" / "eos" / "board" / "result.schema.json"
RELEASE_SCHEMA = (
    REPO_ROOT / "src" / "tools" / "eos-sdk" / "manifests" / "release-manifest.schema.json"
)
CAPABILITIES = (
    REPO_ROOT / "src" / "tools" / "eos-sdk" / "manifests" / "capabilities.toml"
)

ACCEPTANCE_ROWS = (
    "pie_load_relocation",
    "arguments",
    "environment",
    "current_working_directory",
    "standard_io",
    "allocation",
    "files_and_directories",
    "threads",
    "rust_tls",
    "synchronization",
    "monotonic_and_realtime_time",
    "tcp",
    "udp",
    "numeric_socket_addresses",
    "process_spawn_wait_kill",
    "panic_catch",
    "panic_destructor_cleanup",
    "ffi_panic_containment",
    "backtrace_addresses",
)
APPLICATIONS = (
    "hello-std",
    "filesystem",
    "threads-tls",
    "network",
    "process",
    "ffi-abi",
    "unwind",
)
UNSUPPORTED_CAPABILITIES = (
    "secure_random",
    "native_elf_tls",
    "hard_float_abi",
    "process_exec_replacement",
    "cross_language_unwind",
    "automatic_board_deployment",
    "dns",
    "ipv6",
    "multicast",
    "symlinks",
    "hard_links",
    "ownership",
    "permission_bit_fidelity",
    "mmap_protection",
    "dynamic_loading",
    "per_child_env",
    "per_child_cwd",
    "signals",
    "filesystem_durability",
)
SUPPORTED_CAPABILITIES = (
    "allocation",
    "arguments_environment_current_directory",
    "standard_io",
    "files_and_directories",
    "threads",
    "rust_tls",
    "synchronization",
    "monotonic_and_realtime_time",
    "tcp",
    "udp",
    "numeric_socket_addresses",
    "process_spawn_wait_kill",
    "rust_panic_unwind",
    "panic_destructor_cleanup",
    "ffi_panic_containment",
    "backtrace_addresses",
)

# Synthetic RSA-2048 fixture generated solely for these tests. It is not a release key.
TEST_KEY_ID = "task18-synthetic-test-only"
TEST_RSA_N = int(
    "a6feecc5d7c864170c0aaa45bdda2abf8bb4d849c927ccde4f7e7054ac1c855c"
    "a20aaf116a33735bacdfaecd1c6a2c8a281ddffa7f7589b8c3a150258fc0ec466"
    "9195871f1cc3f91ba2e12755f48879c10677720167cdc30d42c25f4d783e45e4"
    "14fa86958597c44d66642839f3a9ca803ea15929982b83bf53684c845d6bb7e0"
    "2ab7aea577396e319821724e387bbb1edd6e16f29ad9cbf2966f9a09f6ead76d"
    "99033a82ec16e8a697e64b948c458b8e049bc99bd0b6564df5e3a348e00eae21"
    "94baf4ed1aae28720668a61168d200807aec93d9f89c1617f93d99a2b9d60a4b"
    "24b7503fea9f1211ef08a44660cd5ead8138d241cd2f8b8eaf9931cfc323485",
    16,
)
TEST_RSA_D = int(
    "825756d6d4a543f9f91c19ea755463293e11d8ff3e5222452226bde658afaa27"
    "b31243b28401c47839661d395a84445f51108051344ab943cced8b70c5d2fe97"
    "b65062080d822ddbc0455582e6ebe56c9a2127899403c0991c01995e91181096c"
    "0c33bdf7d65dae89faf111b4a9ca4c93ec4e631963fc12c8b28125b7ea72b8e"
    "d5534cd9ea4472d55b166acf4405bdfa1a544a6525765590f4e737fba14295ebd"
    "e510e7984af68623e635c850caa23ad86d3b7ad54cd3e6e53c1b70586b7414f"
    "27d2949ba72ca78837a1255856cb9644106bb38c63d99b483a0058c8606f5ae2"
    "d06297f14d9a2b9479a7ff87f4104de1ac3ac165bf8b1a1fe6bab00866abb5c1",
    16,
)
SHA256_DIGEST_INFO = bytes.fromhex("3031300d060960864801650304020105000420")


def canonical_result(result: dict) -> bytes:
    unsigned = {key: value for key, value in result.items() if key != "signature"}
    return json.dumps(
        unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


def sign_result(result: dict) -> None:
    digest_info = SHA256_DIGEST_INFO + hashlib.sha256(canonical_result(result)).digest()
    width = (TEST_RSA_N.bit_length() + 7) // 8
    encoded = b"\x00\x01" + b"\xff" * (width - len(digest_info) - 3) + b"\x00" + digest_info
    signature = pow(int.from_bytes(encoded, "big"), TEST_RSA_D, TEST_RSA_N)
    result["signature"] = {
        "algorithm": "RSASSA-PKCS1-v1_5-SHA256",
        "key_id": TEST_KEY_ID,
        "value": base64.b64encode(signature.to_bytes(width, "big")).decode("ascii"),
    }


class BoardResultGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.release_manifest = self.root / "release-manifest.toml"
        self.release_manifest.write_text(
            textwrap.dedent(
                """\
                [release]
                version = "1.97.1"
                toolchain = "eos-1.97.1"
                target = "armv7a-unknown-eos-eabi"
                layout_version = 1

                [input_hashes]
                algorithm = "sha256-tree-v1"
                arm_gnu_installed = "a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d"
                eos_sdk = "5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910"

                [source_revisions]
                rust_fork = "baeb1a5b368d15d98a5433a3aadd216f472bf9e2"
                rust_upstream = "8bab26f4f68e0e26f0bb7960be334d5b520ea452"
                libc_upstream = "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8"
                backtrace = "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd"

                [distribution_hashes]
                "cargo-1.97.1-dev-x86_64-unknown-linux-gnu.tar.xz" = "c9506399dd578571b953cb12eb8e981ddd6eec472f76c45baa8011d343fab6c9"
                "rust-std-1.97.1-dev-armv7a-unknown-eos-eabi.tar.xz" = "52f51b6804a036f2b4f9388b8bc4463b44e9c58bb3258a9d7d65807718a4ee0b"
                "rustc-1.97.1-dev-x86_64-unknown-linux-gnu.tar.xz" = "26d4f1b0b0f4075d429e0e393f4e41fad37e89227c55156a0f8f328cad540490"
                """
            ),
            encoding="utf-8",
        )
        self.release_manifest_digest = hashlib.sha256(
            self.release_manifest.read_bytes()
        ).hexdigest()
        self.key_file = self.root / "trusted-public-key.json"
        self.key_file.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "key_id": TEST_KEY_ID,
                    "algorithm": "RSASSA-PKCS1-v1_5-SHA256",
                    "modulus_hex": format(TEST_RSA_N, "x"),
                    "public_exponent": 65537,
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def result(self, part: str) -> dict:
        result = {
            "schema_version": 1,
            "board": {
                "part": part,
                "module": f"organization-module-{part.lower()}",
                "eos_version": "MARTOS-SMP-14.0.39",
            },
            "captured_at_utc": "2026-08-21T12:00:00Z",
            "identities": {
                "sdk_package_sha256_tree_v1": "309cd5d682c09dfd65e1ff0f2c0ee5d87e390543bcdfd09af52b49f8327c5060",
                "release_manifest_sha256": self.release_manifest_digest,
                "sdk_version": "1.97.1",
                "toolchain": "eos-1.97.1",
                "target": "armv7a-unknown-eos-eabi",
                "layout_version": 1,
                "rust_fork": "baeb1a5b368d15d98a5433a3aadd216f472bf9e2",
                "rust_upstream": "8bab26f4f68e0e26f0bb7960be334d5b520ea452",
                "libc_upstream": "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8",
                "backtrace": "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd",
                "arm_gnu_release": "14.3.Rel1",
                "arm_gnu_sha256_tree_v1": "a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d",
                "eos_sdk_baseline": "MARTOS-SMP-14.0.39",
                "eos_sdk_sha256_tree_v1": "5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910",
                "native_abi_major": 1,
                "native_abi_minor": 0,
                "linker_script_sha256": "835c2afac09937fb1b6f7dc93027134c61b34bf645eb2809bbaf1ece4994a674",
            },
            "profiles": [],
            "capabilities": [
                {"id": capability, "status": "pass"}
                for capability in SUPPORTED_CAPABILITIES
            ]
            + [
                {"id": capability, "status": "unsupported", "error": "Unsupported"}
                for capability in UNSUPPORTED_CAPABILITIES
            ],
            "observed_failures": [],
        }
        for profile in ("debug", "release"):
            result["profiles"].append(
                {
                    "name": profile,
                    "runs": [
                        {
                            "load_address": address,
                            "acceptance": [
                                {
                                    "id": row,
                                    "status": "pass",
                                    "evidence": f"captured {profile} {address} {row}",
                                }
                                for row in ACCEPTANCE_ROWS
                            ],
                        }
                        for address in ("0x10000000", "0x18000000")
                    ],
                    "applications": [
                        {
                            "name": application,
                            "build_id": hashlib.sha1(
                                f"{application}-{profile}".encode("ascii")
                            ).hexdigest(),
                            "authenticated_sha256": hashlib.sha256(
                                f"{application}-{profile}".encode("ascii")
                            ).hexdigest(),
                        }
                        for application in APPLICATIONS
                    ],
                }
            )
        sign_result(result)
        return result

    def run_checker(
        self,
        first: dict,
        second: dict | None,
        *,
        release_manifest: Path | None = None,
        capabilities: Path = CAPABILITIES,
        policy_overrides: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        paths = []
        for index, result in enumerate((first, second)):
            if result is None:
                continue
            path = self.root / f"result-{index}.json"
            path.write_text(json.dumps(result), encoding="utf-8")
            paths.append(path)
        command = [
            sys.executable,
            str(CHECKER),
            "--release-manifest",
            str(release_manifest or self.release_manifest),
            "--trusted-key",
            str(self.key_file),
        ]
        if policy_overrides:
            command.extend(
                [
                    "--manifest",
                    str(BOARD_MANIFEST),
                    "--schema",
                    str(RESULT_SCHEMA),
                    "--release-schema",
                    str(RELEASE_SCHEMA),
                    "--capabilities",
                    str(capabilities),
                ]
            )
        command.extend(map(str, paths))
        if capabilities != CAPABILITIES and not policy_overrides:
            module_name = f"task18_checker_{id(self)}"
            spec = importlib.util.spec_from_file_location(module_name, CHECKER)
            assert spec is not None and spec.loader is not None
            checker = importlib.util.module_from_spec(spec)
            sys.modules[module_name] = checker
            spec.loader.exec_module(checker)
            policy = checker.PolicyPaths(
                manifest=BOARD_MANIFEST,
                result_schema=RESULT_SCHEMA,
                release_schema=RELEASE_SCHEMA,
                capabilities=capabilities,
            )
            stdout = io.StringIO()
            stderr = io.StringIO()
            internal_arguments = command[2:]
            with redirect_stdout(stdout), redirect_stderr(stderr):
                try:
                    returncode = checker._run_with_policy(internal_arguments, policy)
                except checker.GateError as error:
                    print(f"board result gate failed: {error}", file=sys.stderr)
                    returncode = 2
            return subprocess.CompletedProcess(
                command, returncode, stdout.getvalue(), stderr.getvalue()
            )
        return subprocess.run(command, text=True, capture_output=True, check=False)

    def assert_rejected(self, result: subprocess.CompletedProcess[str], phrase: str) -> None:
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(phrase, result.stderr)

    def test_accepts_two_complete_test_only_cryptographically_signed_results(self):
        result = self.run_checker(self.result("XC7Z030"), self.result("XC7Z045"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verified signed XC7Z030 and XC7Z045 board results", result.stdout)

    def test_public_cli_rejects_policy_override_options(self):
        result = self.run_checker(
            self.result("XC7Z030"), self.result("XC7Z045"), policy_overrides=True
        )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unrecognized arguments", result.stderr)

    def test_rejects_missing_or_duplicate_board_part(self):
        self.assert_rejected(self.run_checker(self.result("XC7Z030"), None), "exactly two")
        self.assert_rejected(
            self.run_checker(self.result("XC7Z030"), self.result("XC7Z030")),
            "exactly one signed result",
        )
        wrong = self.result("XC7Z030")
        wrong["board"]["part"] = "XC7Z020"
        sign_result(wrong)
        self.assert_rejected(
            self.run_checker(wrong, self.result("XC7Z045")), "board.part"
        )

    def test_rejects_mismatched_reviewed_identity(self):
        second = self.result("XC7Z045")
        second["identities"]["linker_script_sha256"] = "0" * 64
        sign_result(second)
        self.assert_rejected(
            self.run_checker(self.result("XC7Z030"), second), "linker_script_sha256"
        )

    def test_rejects_unsigned_and_invalid_signatures(self):
        unsigned = self.result("XC7Z030")
        del unsigned["signature"]
        self.assert_rejected(
            self.run_checker(unsigned, self.result("XC7Z045")), "signature"
        )

        invalid = self.result("XC7Z030")
        signature = bytearray(base64.b64decode(invalid["signature"]["value"]))
        signature[-1] ^= 1
        invalid["signature"]["value"] = base64.b64encode(signature).decode()
        self.assert_rejected(
            self.run_checker(invalid, self.result("XC7Z045")), "signature"
        )

    def test_rejects_noncanonical_signature_integer(self):
        noncanonical = self.result("XC7Z030")
        width = (TEST_RSA_N.bit_length() + 7) // 8
        for index in range(100):
            noncanonical["board"]["module"] = f"test-only-module-{index}"
            sign_result(noncanonical)
            signature = int.from_bytes(
                base64.b64decode(noncanonical["signature"]["value"]), "big"
            )
            if signature + TEST_RSA_N < 1 << (width * 8):
                break
        else:
            self.fail("test-only fixture could not form a noncanonical RSA signature")
        noncanonical["signature"]["value"] = base64.b64encode(
            (signature + TEST_RSA_N).to_bytes(width, "big")
        ).decode("ascii")

        self.assert_rejected(
            self.run_checker(noncanonical, self.result("XC7Z045")), "signature"
        )

    def test_rejects_missing_profile_or_non_distinct_load_address(self):
        missing = self.result("XC7Z030")
        missing["profiles"] = missing["profiles"][:1]
        sign_result(missing)
        self.assert_rejected(
            self.run_checker(missing, self.result("XC7Z045")), "profiles"
        )

        missing_address = self.result("XC7Z030")
        del missing_address["profiles"][0]["runs"][0]["load_address"]
        sign_result(missing_address)
        self.assert_rejected(
            self.run_checker(missing_address, self.result("XC7Z045")), "load_address"
        )

        duplicate = self.result("XC7Z030")
        duplicate["profiles"][0]["runs"][1]["load_address"] = duplicate["profiles"][0][
            "runs"
        ][0]["load_address"]
        sign_result(duplicate)
        self.assert_rejected(
            self.run_checker(duplicate, self.result("XC7Z045")), "distinct load addresses"
        )

        incomplete_matrix = self.result("XC7Z030")
        incomplete_matrix["profiles"][1]["runs"][1]["load_address"] = "0x22000000"
        sign_result(incomplete_matrix)
        self.assert_rejected(
            self.run_checker(incomplete_matrix, self.result("XC7Z045")),
            "same two load addresses",
        )

    def test_rejects_missing_acceptance_row_and_observed_failure_list(self):
        missing_row = self.result("XC7Z030")
        missing_row["profiles"][1]["runs"][1]["acceptance"] = missing_row["profiles"][1][
            "runs"
        ][1]["acceptance"][:-1]
        sign_result(missing_row)
        self.assert_rejected(
            self.run_checker(missing_row, self.result("XC7Z045")), "acceptance rows"
        )

        missing_failures = self.result("XC7Z030")
        del missing_failures["observed_failures"]
        sign_result(missing_failures)
        self.assert_rejected(
            self.run_checker(missing_failures, self.result("XC7Z045")),
            "observed_failures",
        )

    def test_rejects_observed_failure_or_failed_acceptance(self):
        observed = self.result("XC7Z030")
        observed["observed_failures"] = ["panic destructor did not run"]
        sign_result(observed)
        self.assert_rejected(
            self.run_checker(observed, self.result("XC7Z045")), "observed failures"
        )

        failed = self.result("XC7Z030")
        failed["profiles"][0]["runs"][1]["acceptance"][0]["status"] = "fail"
        sign_result(failed)
        self.assert_rejected(
            self.run_checker(failed, self.result("XC7Z045")), "did not pass"
        )

    def test_rejects_missing_application_build_id(self):
        missing = self.result("XC7Z030")
        del missing["profiles"][0]["applications"][0]["build_id"]
        sign_result(missing)
        self.assert_rejected(
            self.run_checker(missing, self.result("XC7Z045")), "build_id"
        )

    def test_rejects_nonidentical_authenticated_binary_across_boards(self):
        second = self.result("XC7Z045")
        second["profiles"][0]["applications"][0]["authenticated_sha256"] = "f" * 64
        sign_result(second)
        self.assert_rejected(
            self.run_checker(self.result("XC7Z030"), second),
            "authenticated binaries differ",
        )

    def test_false_optional_capabilities_require_matrix_false_and_unsupported(self):
        wrong_status = self.result("XC7Z030")
        secure_random = next(
            row for row in wrong_status["capabilities"] if row["id"] == "secure_random"
        )
        secure_random["status"] = "pass"
        del secure_random["error"]
        sign_result(wrong_status)
        self.assert_rejected(
            self.run_checker(wrong_status, self.result("XC7Z045")),
            "capability secure_random",
        )

        matrix = self.root / "capabilities.toml"
        matrix.write_text(
            CAPABILITIES.read_text(encoding="utf-8").replace(
                'id = "secure_random"\nsupported = false',
                'id = "secure_random"\nsupported = true',
            ),
            encoding="utf-8",
        )
        self.assert_rejected(
            self.run_checker(
                self.result("XC7Z030"), self.result("XC7Z045"), capabilities=matrix
            ),
            "capability secure_random",
        )

    def test_capability_matrix_requires_exact_supported_inventory_and_evidence(self):
        original = CAPABILITIES.read_text(encoding="utf-8")
        marker = '[[capability]]\nid = "allocation"\n'
        start = original.index(marker)
        next_start = original.index("[[capability]]", start + len(marker))
        allocation_block = original[start:next_start]
        mutations = {
            "missing-allocation": original[:start] + original[next_start:],
            "missing-contract-evidence": original.replace(
                allocation_block,
                allocation_block.replace(
                    "contract_evidence = true", "contract_evidence = false"
                ),
            ),
            "unexpected-capability": original
            + textwrap.dedent(
                """\

                [[capability]]
                id = "unreviewed_capability"
                supported = false
                contract_evidence = false
                board_evidence = false
                unsupported_error = "Unsupported"
                evidence = "test-only unreviewed entry"
                """
            ),
        }
        for name, text in mutations.items():
            with self.subTest(name=name):
                matrix = self.root / f"capabilities-{name}.toml"
                matrix.write_text(text, encoding="utf-8")
                self.assert_rejected(
                    self.run_checker(
                        self.result("XC7Z030"),
                        self.result("XC7Z045"),
                        capabilities=matrix,
                    ),
                    "capability",
                )

    def test_release_manifest_schema_rejects_bad_digest_and_archive_name(self):
        for name, replacement in (
            ("digest", 'arm_gnu_installed = "wrong"'),
            (
                "archive",
                '"../cargo.tar.xz" = "c9506399dd578571b953cb12eb8e981ddd6eec472f76c45baa8011d343fab6c9"',
            ),
        ):
            with self.subTest(name=name):
                corrupt = self.root / f"release-{name}.toml"
                text = self.release_manifest.read_text(encoding="utf-8")
                if name == "digest":
                    text = text.replace(
                        'arm_gnu_installed = "a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d"',
                        replacement,
                    )
                else:
                    text = text.replace(
                        '"cargo-1.97.1-dev-x86_64-unknown-linux-gnu.tar.xz" = "c9506399dd578571b953cb12eb8e981ddd6eec472f76c45baa8011d343fab6c9"',
                        replacement,
                    )
                corrupt.write_text(text, encoding="utf-8")
                self.assert_rejected(
                    self.run_checker(
                        self.result("XC7Z030"),
                        self.result("XC7Z045"),
                        release_manifest=corrupt,
                    ),
                    "release manifest",
                )

    def test_rejects_boolean_release_layout_version_end_to_end(self):
        self.release_manifest.write_text(
            self.release_manifest.read_text(encoding="utf-8").replace(
                "layout_version = 1", "layout_version = true"
            ),
            encoding="utf-8",
        )
        self.release_manifest_digest = hashlib.sha256(
            self.release_manifest.read_bytes()
        ).hexdigest()

        self.assert_rejected(
            self.run_checker(self.result("XC7Z030"), self.result("XC7Z045")),
            "release manifest",
        )

    def test_rejects_boolean_result_schema_version_end_to_end(self):
        first = self.result("XC7Z030")
        second = self.result("XC7Z045")
        first["schema_version"] = True
        second["schema_version"] = True
        sign_result(first)
        sign_result(second)

        self.assert_rejected(self.run_checker(first, second), "schema_version")

    def test_rejects_impossible_utc_timestamp_end_to_end(self):
        first = self.result("XC7Z030")
        first["captured_at_utc"] = "2026-99-99T99:99:99Z"
        sign_result(first)

        self.assert_rejected(
            self.run_checker(first, self.result("XC7Z045")), "captured_at_utc"
        )


if __name__ == "__main__":
    unittest.main()
