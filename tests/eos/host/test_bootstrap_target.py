#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SANITY = ROOT / "src" / "bootstrap" / "src" / "core" / "sanity.rs"
HELPERS = ROOT / "src" / "bootstrap" / "src" / "utils" / "helpers.rs"
TARGET = "armv7a-unknown-eos-eabi"


class BootstrapTargetTest(unittest.TestCase):
    def test_eos_is_allowlisted_until_stage0_knows_the_target(self) -> None:
        source = SANITY.read_text(encoding="utf-8")
        missing_targets = re.search(
            r"const STAGE0_MISSING_TARGETS: &\[&str\] = &\[(.*?)\];",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(missing_targets)
        self.assertIn(f'"{TARGET}"', missing_targets.group(1))

    def test_eos_uses_its_target_spec_linker(self) -> None:
        source = HELPERS.read_text(encoding="utf-8")
        use_host_linker = re.search(
            r"pub fn use_host_linker\(.*?\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(use_host_linker)
        self.assertIn('target.contains("eos")', use_host_linker.group(0))


if __name__ == "__main__":
    unittest.main()
