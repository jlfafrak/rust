#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
STD = ROOT / "library" / "std" / "src"

PAL_FILES = (
    STD / "sys" / "alloc" / "unix.rs",
    STD / "sys" / "pal" / "unix" / "mod.rs",
    STD / "sys" / "io" / "error" / "unix.rs",
    STD / "sys" / "io" / "io_slice" / "iovec.rs",
    STD / "sys" / "args" / "unix.rs",
    STD / "sys" / "env" / "unix.rs",
    STD / "sys" / "env_consts.rs",
    STD / "sys" / "fd" / "unix.rs",
    STD / "sys" / "fs" / "unix.rs",
    STD / "sys" / "fs" / "unix" / "dir.rs",
    STD / "sys" / "fs" / "mod.rs",
    STD / "sys" / "net" / "connection" / "socket" / "unix.rs",
    STD / "sys" / "net" / "hostname" / "unix.rs",
    STD / "sys" / "paths" / "unix.rs",
    STD / "sys" / "pipe" / "unix.rs",
    STD / "sys" / "process" / "unix" / "common.rs",
    STD / "sys" / "process" / "unix" / "eos.rs",
    STD / "sys" / "process" / "unix" / "mod.rs",
    STD / "sys" / "thread" / "unix.rs",
    STD / "sys" / "random" / "mod.rs",
    STD / "sys" / "random" / "eos.rs",
    STD / "os" / "unix" / "mod.rs",
    STD / "os" / "unix" / "process.rs",
)

REQUIRED_SHIM_COMMENTS = {
    "eos_rust_abi_require": STD / "sys" / "pal" / "unix" / "mod.rs",
    "eos_rust_runtime_init": STD / "sys" / "pal" / "unix" / "mod.rs",
    "eos_rust_runtime_cleanup": STD / "sys" / "pal" / "unix" / "mod.rs",
    "eos_rust_errno_location": STD / "sys" / "io" / "error" / "unix.rs",
    "eos_rust_environ": STD / "sys" / "env" / "unix.rs",
    "eos_rust_cpu_count": STD / "sys" / "thread" / "unix.rs",
    "eos_rust_hash_seed": STD / "sys" / "random" / "mod.rs",
}


def cfg_attributes(source: str) -> list[str]:
    return re.findall(r"#\s*!?\s*\[cfg(?:_attr)?\((.*?)\)\]", source, re.DOTALL)


class PalCfgScopeTest(unittest.TestCase):
    def test_eos_is_not_folded_into_linux_cfg_attributes(self) -> None:
        offenders = []
        for path in PAL_FILES:
            source = path.read_text(encoding="utf-8")
            for attribute in cfg_attributes(source):
                if 'target_os = "eos"' in attribute and 'target_os = "linux"' in attribute:
                    offenders.append(f"{path.relative_to(ROOT)}: {attribute.strip()}")
        self.assertEqual(
            offenders,
            [],
            "EOS must use its own PAL branch, not a Linux cfg attribute:\n"
            + "\n".join(offenders),
        )

    def test_linux_only_modules_never_select_eos(self) -> None:
        offenders = []
        for directory in (STD / "sys" / "pal" / "unix" / "linux", STD / "os" / "linux"):
            for path in directory.rglob("*.rs"):
                if 'target_os = "eos"' in path.read_text(encoding="utf-8"):
                    offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [], "Linux-only source must not mention EOS")

    def test_required_eos_routes_name_the_stable_shim_service(self) -> None:
        missing = []
        for service, path in REQUIRED_SHIM_COMMENTS.items():
            if not path.exists():
                missing.append(f"{path.relative_to(ROOT)} is missing")
                continue
            source = path.read_text(encoding="utf-8")
            if not re.search(rf"//[^\n]*\b{re.escape(service)}\b", source):
                missing.append(
                    f"{path.relative_to(ROOT)} lacks an EOS comment naming {service}"
                )
        self.assertEqual(missing, [], "\n".join(missing))

    def test_eos_random_policy_is_literal_and_separate(self) -> None:
        path = STD / "sys" / "random" / "eos.rs"
        self.assertTrue(path.exists(), "EOS must have a separate random policy module")
        source = path.read_text(encoding="utf-8")
        self.assertIn(
            'panic!("EOS v1 does not provide cryptographically secure random bytes")',
            source,
        )
        self.assertIn("libc::eos_hash_seed(&mut key0, &mut key1)", source)

    def test_eos_fixed_open_signature_does_not_change_unix_varargs_promotion(self) -> None:
        source = (STD / "sys" / "fs" / "unix.rs").read_text(encoding="utf-8")
        self.assertIn(
            '#[cfg(not(target_os = "eos"))]\n'
            '        let mode = opts.mode as c_int;',
            source,
        )
        self.assertIn(
            '#[cfg(target_os = "eos")]\n'
            '        let mode = opts.mode as mode_t;',
            source,
        )
        self.assertIn("open64(path.as_ptr(), flags, mode)", source)


if __name__ == "__main__":
    unittest.main()
