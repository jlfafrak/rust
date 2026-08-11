"""Contract tests for the EOS Rust toolchain source lock."""

from pathlib import Path
import tomllib
import unittest


EXPECTED = {
    ("rust", "version"): "1.97.1",
    ("rust", "upstream_commit"): "8bab26f4f68e0e26f0bb7960be334d5b520ea452",
    ("libc", "version"): "0.2.185",
    ("libc", "upstream_commit"): "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8",
    ("arm_gnu", "release"): "14.3.Rel1",
    ("eos_sdk", "baseline"): "MARTOS-SMP-14.0.39",
    ("eos_rust_abi", "major"): 1,
    ("eos_rust_abi", "minor"): 0,
}

LOCK_PATH = (
    Path(__file__).resolve().parents[3]
    / "src/tools/eos-sdk/manifests/toolchain.lock.toml"
)


class ToolchainLockTests(unittest.TestCase):
    def test_source_lock_pins_the_supported_toolchain_baselines(self):
        with LOCK_PATH.open("rb") as lock_file:
            lock = tomllib.load(lock_file)

        for (section, key), expected_value in EXPECTED.items():
            with self.subTest(section=section, key=key):
                self.assertEqual(lock[section][key], expected_value)


if __name__ == "__main__":
    unittest.main()
