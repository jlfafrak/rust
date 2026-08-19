import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from support import (
    AUTH_MARKER,
    ELF_VALIDATOR,
    install_fake_elf_tools,
    minimal_elf,
    run_python,
    valid_tool_outputs,
    write_executable,
)


class ElfValidatorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "sdk"
        self.work = self.base / "work"
        self.work.mkdir()
        self.image = self.work / "application.elf"
        self.image.write_bytes(minimal_elf())
        self.outputs = valid_tool_outputs()
        self.fixture = install_fake_elf_tools(self.root, self.outputs)
        self.tool_control = self.root / "tool-control.json"
        manifest = self.root / "manifests" / "allowed-dynamic-symbols.txt"
        manifest.parent.mkdir(parents=True)
        manifest.write_text("# provided by libmartos_app.so.1.0\nos_app_get_id\n")
        self.env = {
            "EOS_RUST_SDK_ROOT": str(self.root),
        }

    def write_outputs(self, outputs: dict[str, str]):
        self.fixture.write_text(json.dumps(outputs), encoding="utf-8")

    def write_tool_control(self, **control: str):
        self.tool_control.write_text(json.dumps(control), encoding="utf-8")

    def invoke(self, *args: str, env: dict[str, str] | None = None):
        invocation_env = dict(self.env)
        if env:
            invocation_env.update(env)
        return run_python(ELF_VALIDATOR, list(args), cwd=self.work, env=invocation_env)

    def assert_rejected(self, outputs: dict[str, str], expected: str):
        self.write_outputs(outputs)
        result = self.invoke(str(self.image))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(expected, result.stderr)

    def test_accepts_complete_eos_elf_contract_and_runs_every_tool_boundary(self):
        result = self.invoke(str(self.image))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("validated EOS ELF", result.stdout)

    def test_external_tools_inspect_the_owned_initial_byte_snapshot(self):
        expected = self.work / "expected-initial.elf"
        expected.write_bytes(self.image.read_bytes())
        replacement = self.work / "replacement.elf"
        replacement.write_bytes(b"attacker replacement")
        replaced = self.work / "replacement-triggered"
        self.write_tool_control(
            original=str(self.image),
            replacement=str(replacement),
            replacement_once=str(replaced),
            expected=str(expected),
        )
        result = self.invoke(str(self.image))
        self.assertTrue(replaced.is_file(), "the path-replacement attack did not run")
        self.assertEqual(self.image.read_bytes(), replacement.read_bytes())
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_external_inspection_tools_receive_a_sanitized_environment(self):
        tool_source = f"""#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

for forbidden in ("LD_PRELOAD", "COMPILER_PATH", "LIBRARY_PATH", "EOS_ATTACKER_SENTINEL"):
    if forbidden in os.environ:
        print(f"host environment reached inspection tool: {{forbidden}}", file=sys.stderr)
        raise SystemExit(8)
if os.environ.get("PATH") != os.defpath or os.environ.get("LC_ALL") != "C" or os.environ.get("LANG") != "C":
    print("inspection environment is not the minimal pinned environment", file=sys.stderr)
    raise SystemExit(8)
outputs = json.loads(Path({str(self.fixture)!r}).read_text(encoding="utf-8"))
key = "nm" if Path(sys.argv[0]).name.endswith("nm") else sys.argv[1]
sys.stdout.write(outputs[key])
"""
        binary = self.root / "arm-gnu" / "bin"
        write_executable(binary / "arm-none-eabi-readelf", tool_source)
        write_executable(binary / "arm-none-eabi-nm", tool_source)
        result = self.invoke(
            str(self.image),
            env={
                "COMPILER_PATH": "/tmp/attacker-tools",
                "LIBRARY_PATH": "/tmp/attacker-libraries",
                "EOS_ATTACKER_SENTINEL": "must-not-reach-tools",
            },
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_mutated_elf_header_contracts(self):
        mutations = {
            "ELF class": ("ELF32", "ELF64", "ELF32"),
            "endianness": ("little endian", "big endian", "little endian"),
            "file type": ("DYN (Shared object file)", "EXEC (Executable file)", "ET_DYN"),
            "machine": ("Machine:                           ARM", "Machine:                           AArch64", "ARM machine"),
            "EABI": ("Version5 EABI", "Version4 EABI", "EABI5"),
            "float ABI": ("soft-float ABI", "hard-float ABI", "soft-float ABI"),
            "entry": ("0x1000", "0x1004", "entry address"),
        }
        for label, (old, new, expected) in mutations.items():
            with self.subTest(label=label):
                outputs = copy.deepcopy(self.outputs)
                outputs["-hW"] = outputs["-hW"].replace(old, new)
                self.assert_rejected(outputs, expected)

    def test_rejects_program_header_and_dynamic_policy_mutations(self):
        cases = {
            "interpreter": ("-lW", "/usr/lib/ld.so.1", "/tmp/ld.so", "interpreter"),
            "RX load": ("-lW", "R E 0x1000", "R   0x1000", "RX PT_LOAD"),
            "RW load": ("-lW", "RW  0x1000", "R   0x1000", "RW PT_LOAD"),
            "dynamic": ("-lW", "DYNAMIC", "NOTE   ", "PT_DYNAMIC"),
            "exidx segment": ("-lW", "ARM_EXIDX", "NOTE     ", "PT_ARM_EXIDX"),
            "TLS segment": ("-lW", "DYNAMIC", "TLS    \n  DYNAMIC", "PT_TLS"),
            "needed": ("-dW", "libmartos_app.so.1.0", "libother.so", "libmartos_app.so.1.0"),
            "unexpected needed": (
                "-dW",
                " 0x00000000 (NULL)",
                " 0x00000001 (NEEDED)                     Shared library: [libother.so]\n 0x00000000 (NULL)",
                "shared-library dependencies",
            ),
            "PIE": ("-dW", "Flags: PIE", "Flags: NOW", "PIE"),
            "TEXTREL": ("-dW", "(NULL)", "(TEXTREL)", "TEXTREL"),
        }
        for label, (key, old, new, expected) in cases.items():
            with self.subTest(label=label):
                outputs = copy.deepcopy(self.outputs)
                outputs[key] = outputs[key].replace(old, new)
                self.assert_rejected(outputs, expected)

    def test_rejects_arm_attribute_mutations(self):
        cases = {
            "architecture": ("Tag_CPU_arch: v7", "Tag_CPU_arch: v6", "ARMv7"),
            "profile": ("Application", "Microcontroller", "Application profile"),
            "A32": ("Tag_ARM_ISA_use: Yes", "Tag_ARM_ISA_use: No", "A32"),
            "Thumb-2": ("Thumb-2", "Thumb-1", "Thumb-2"),
            "VFPv3": ("VFPv3", "VFPv2", "VFPv3"),
            "NEONv1": ("NEONv1", "NEONv2", "NEONv1"),
        }
        for label, (old, new, expected) in cases.items():
            with self.subTest(label=label):
                outputs = copy.deepcopy(self.outputs)
                outputs["-AW"] = outputs["-AW"].replace(old, new)
                self.assert_rejected(outputs, expected)

    def test_rejects_unknown_relocation(self):
        outputs = copy.deepcopy(self.outputs)
        outputs["-rW"] += "0000200c  00000115 R_ARM_GLOB_DAT 00000000 os_app_get_id\n"
        self.assert_rejected(outputs, "R_ARM_GLOB_DAT")

    def test_rejects_missing_or_unallocated_ehabi_and_nonempty_native_tls(self):
        cases = {
            "missing exidx": (".ARM.exidx", ".not_exidx", ".ARM.exidx"),
            "unallocated exidx": ("00  AL  1", "00   L  1", "allocated .ARM.exidx"),
            "unallocated extab": ("  [ 3] .dynamic", "  [ 3] .ARM.extab        PROGBITS        00001018 000118 000004 00      0   0  4\n  [ 4] .dynamic", "allocated .ARM.extab"),
            "tdata": ("  [ 3] .dynamic", "  [ 3] .tdata            PROGBITS        00002000 000200 000004 00 WAT  0   0  4\n  [ 4] .dynamic", "native TLS section .tdata"),
            "tbss": ("  [ 3] .dynamic", "  [ 3] .tbss             NOBITS          00002000 000200 000004 00 WAT  0   0  4\n  [ 4] .dynamic", "native TLS section .tbss"),
        }
        for label, (old, new, expected) in cases.items():
            with self.subTest(label=label):
                outputs = copy.deepcopy(self.outputs)
                outputs["-SW"] = outputs["-SW"].replace(old, new)
                self.assert_rejected(outputs, expected)

    def test_rejects_entry_symbol_and_dynamic_allowlist_mutations(self):
        cases = {
            "local main": ("GLOBAL DEFAULT    1 main", "LOCAL  DEFAULT    1 main", "global main"),
            "undefined main": ("GLOBAL DEFAULT    1 main", "GLOBAL DEFAULT  UND main", "global main"),
            "unexpected undefined": ("os_app_get_id", "attacker_symbol", "attacker_symbol"),
        }
        for label, (old, new, expected) in cases.items():
            with self.subTest(label=label):
                outputs = copy.deepcopy(self.outputs)
                outputs["-Ws"] = outputs["-Ws"].replace(old, new)
                if label == "unexpected undefined":
                    outputs["-rW"] = outputs["-rW"].replace(old, new)
                self.assert_rejected(outputs, expected)

        outputs = copy.deepcopy(self.outputs)
        outputs["nm"] = "00001004 T main\n"
        self.assert_rejected(outputs, "nm main")

    def test_rejects_marker_by_default_and_checks_exact_allowed_trailer(self):
        base = self.image.read_bytes()
        digest = hashlib.sha256(base).hexdigest().encode("ascii")
        self.image.write_bytes(base + AUTH_MARKER + digest + b"\n")
        default = self.invoke(str(self.image))
        self.assertNotEqual(default.returncode, 0)
        self.assertIn("authentication trailer", default.stderr)
        allowed = self.invoke("--allow-auth-trailer", str(self.image))
        self.assertEqual(allowed.returncode, 0, allowed.stderr)

        data = self.image.read_bytes()
        self.image.write_bytes(data[:-2] + b"0\n")
        invalid = self.invoke("--allow-auth-trailer", str(self.image))
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("digest", invalid.stderr)

    def test_internal_marker_bytes_are_not_misclassified_as_a_trailer(self):
        embedded = minimal_elf() + AUTH_MARKER + b"ordinary ELF payload bytes"
        self.image.write_bytes(embedded)
        result = self.invoke(str(self.image))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_truncated_elf_extent_and_marker_only_fixture(self):
        self.image.write_bytes(minimal_elf()[:-1])
        truncated = self.invoke(str(self.image))
        self.assertNotEqual(truncated.returncode, 0)
        self.assertIn("extent", truncated.stderr)

        marker = Path(__file__).with_name("fixtures") / "marker-only.elf"
        marker_only = self.invoke(str(marker))
        self.assertNotEqual(marker_only.returncode, 0)
        self.assertIn("ELF magic", marker_only.stderr)

    def test_fails_closed_on_tool_error_and_root_symlink_escape(self):
        self.write_tool_control(failure="-rW")
        failure = self.invoke(str(self.image))
        self.assertNotEqual(failure.returncode, 0)
        self.assertIn("readelf -rW failed", failure.stderr)
        self.tool_control.unlink()

        readelf = self.root / "arm-gnu" / "bin" / "arm-none-eabi-readelf"
        readelf.unlink()
        outside = self.base / "outside-readelf"
        outside.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        outside.chmod(0o755)
        readelf.symlink_to(outside)
        escaped = self.invoke(str(self.image))
        self.assertNotEqual(escaped.returncode, 0)
        self.assertIn("outside EOS_RUST_SDK_ROOT", escaped.stderr)

    def test_rejects_malformed_allowlist(self):
        manifest = self.root / "manifests" / "allowed-dynamic-symbols.txt"
        manifest.write_text("os_app_get_id\nbad symbol\n", encoding="utf-8")
        result = self.invoke(str(self.image))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("malformed allowlist", result.stderr)


class RealMartosElfIntegrationTests(unittest.TestCase):
    def test_actual_gnu_tools_parse_the_pinned_sibling_elf(self):
        tool_bin = Path(
            os.environ.get(
                "EOS_TASK15_REAL_ARM_GNU",
                "/home/dev/code/arm-toolchain-build/custom-arm-libs/bin",
            )
        )
        image = Path(
            "/home/dev/code/gpt-test/lib/martos-smp-14.0.39/bin/msg_server.elf"
        )
        if not (
            image.is_file()
            and (tool_bin / "arm-none-eabi-readelf").is_file()
            and (tool_bin / "arm-none-eabi-nm").is_file()
        ):
            self.skipTest("pinned real MARTOS/toolchain artifacts are unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "sdk"
            work = Path(directory) / "work"
            work.mkdir()
            binary = root / "arm-gnu" / "bin"
            binary.mkdir(parents=True)
            shutil.copy2(tool_bin / "arm-none-eabi-readelf", binary)
            shutil.copy2(tool_bin / "arm-none-eabi-nm", binary)
            symbols = subprocess.run(
                [str(tool_bin / "arm-none-eabi-readelf"), "--dyn-syms", "-W", str(image)],
                text=True,
                stdout=subprocess.PIPE,
                check=True,
            ).stdout
            undefined = []
            for line in symbols.splitlines():
                fields = line.split()
                if len(fields) >= 8 and fields[6] == "UND" and fields[4] != "LOCAL":
                    undefined.append(fields[7])
            manifest = root / "manifests" / "allowed-dynamic-symbols.txt"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("\n".join(sorted(set(undefined))) + "\n", encoding="utf-8")
            result = run_python(
                ELF_VALIDATOR,
                ["--allow-auth-trailer", str(image)],
                cwd=work,
                env={"EOS_RUST_SDK_ROOT": str(root)},
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
