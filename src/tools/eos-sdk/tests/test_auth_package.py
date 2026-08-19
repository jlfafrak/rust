import hashlib
import os
from pathlib import Path
import tempfile
import unittest

from support import (
    AUTH_MARKER,
    AUTH_PACKAGER,
    ELF_VALIDATOR,
    install_fake_elf_tools,
    minimal_elf,
    minimal_elf_with_payload,
    run_python,
    valid_tool_outputs,
    write_executable,
)


class AuthenticationPackagerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "sdk"
        self.work = self.base / "work"
        self.work.mkdir()
        bin_dir = self.root / "bin"
        bin_dir.mkdir(parents=True)
        write_executable(
            bin_dir / "eos-elf-validate",
            f"""#!/usr/bin/env python3
import os
import sys
os.execv(sys.executable, [sys.executable, {str(ELF_VALIDATOR)!r}, *sys.argv[1:]])
""",
        )
        self.fixture = install_fake_elf_tools(self.root, valid_tool_outputs())
        manifest = self.root / "manifests" / "allowed-dynamic-symbols.txt"
        manifest.parent.mkdir(parents=True)
        manifest.write_text("os_app_get_id\n", encoding="utf-8")
        self.env = {
            "EOS_RUST_SDK_ROOT": str(self.root),
        }
        self.source = self.work / "application.elf"
        self.output = self.work / "application.auth.elf"

    def invoke(
        self,
        source: Path | None = None,
        output: Path | None = None,
        env: dict[str, str] | None = None,
    ):
        invocation_env = dict(self.env)
        if env:
            invocation_env.update(env)
        return run_python(
            AUTH_PACKAGER,
            [str(source or self.source), str(output or self.output)],
            cwd=self.work,
            env=invocation_env,
        )

    def test_rejects_missing_input(self):
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("input", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_marker_only_input(self):
        marker = Path(__file__).with_name("fixtures") / "marker-only.elf"
        result = self.invoke(source=marker)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ELF magic", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_invalid_elf_before_writing_output(self):
        self.source.write_bytes(b"\x7fELF" + b"invalid")
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ELF32 header", result.stderr)
        self.assertFalse(self.output.exists())

    def test_packages_exact_digest_trailer_and_validator_accepts_roundtrip(self):
        base = minimal_elf()
        self.source.write_bytes(base)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = base + AUTH_MARKER + hashlib.sha256(base).hexdigest().encode("ascii") + b"\n"
        self.assertEqual(self.output.read_bytes(), expected)
        validated = run_python(
            self.root / "bin" / "eos-elf-validate",
            ["--allow-auth-trailer", str(self.output)],
            cwd=self.work,
            env=self.env,
        )
        self.assertEqual(validated.returncode, 0, validated.stderr)

    def test_path_replacement_cannot_change_validated_or_packaged_bytes(self):
        initial = minimal_elf()
        self.source.write_bytes(initial)
        expected = self.work / "expected-initial.elf"
        expected.write_bytes(initial)
        replacement = self.work / "replacement.elf"
        replacement.write_bytes(b"attacker replacement")
        replaced = self.work / "replacement-triggered"
        validator = self.root / "bin" / "eos-elf-validate"
        write_executable(
            validator,
            f"""#!/usr/bin/env python3
from pathlib import Path
import os
import sys

image = Path(sys.argv[-1])
allow_trailer = "--allow-auth-trailer" in sys.argv[1:]
trigger = Path({str(replaced)!r})
if not allow_trailer and not trigger.exists():
    Path({str(self.source)!r}).write_bytes(Path({str(replacement)!r}).read_bytes())
    trigger.touch()
    if image.read_bytes() != Path({str(expected)!r}).read_bytes():
        print("base validator did not receive the owned initial snapshot", file=sys.stderr)
        raise SystemExit(9)
os.execv(sys.executable, [sys.executable, {str(ELF_VALIDATOR)!r}, *sys.argv[1:]])
""",
        )
        result = self.invoke()
        self.assertTrue(replaced.is_file(), "the path-replacement attack did not run")
        self.assertEqual(self.source.read_bytes(), replacement.read_bytes())
        self.assertEqual(result.returncode, 0, result.stderr)
        expected_package = (
            initial
            + AUTH_MARKER
            + hashlib.sha256(initial).hexdigest().encode("ascii")
            + b"\n"
        )
        self.assertEqual(self.output.read_bytes(), expected_package)

    def test_validator_receives_only_the_pinned_minimal_environment(self):
        self.source.write_bytes(minimal_elf())
        validator = self.root / "bin" / "eos-elf-validate"
        write_executable(
            validator,
            f"""#!/usr/bin/env python3
import os
import sys

for forbidden in ("LD_PRELOAD", "COMPILER_PATH", "LIBRARY_PATH", "EOS_ATTACKER_SENTINEL"):
    if forbidden in os.environ:
        print(f"host environment reached validator: {{forbidden}}", file=sys.stderr)
        raise SystemExit(8)
if os.environ.get("PATH") != os.defpath or os.environ.get("LC_ALL") != "C" or os.environ.get("LANG") != "C":
    print("validator environment is not minimal", file=sys.stderr)
    raise SystemExit(8)
if os.environ.get("EOS_RUST_SDK_ROOT") != {str(self.root)!r}:
    print("validator did not receive the pinned SDK root", file=sys.stderr)
    raise SystemExit(8)
os.execv(sys.executable, [sys.executable, {str(ELF_VALIDATOR)!r}, *sys.argv[1:]])
""",
        )
        result = self.invoke(
            env={
                "COMPILER_PATH": "/tmp/attacker-tools",
                "LIBRARY_PATH": "/tmp/attacker-libraries",
                "EOS_ATTACKER_SENTINEL": "must-not-reach-validator",
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_existing_trailer_and_same_input_output(self):
        base = minimal_elf()
        self.source.write_bytes(
            base + AUTH_MARKER + hashlib.sha256(base).hexdigest().encode("ascii") + b"\n"
        )
        existing = self.invoke()
        self.assertNotEqual(existing.returncode, 0)
        self.assertIn("authentication trailer", existing.stderr)

        self.source.write_bytes(base)
        same = self.invoke(source=self.source, output=self.source)
        self.assertNotEqual(same.returncode, 0)
        self.assertIn("different", same.stderr)
        self.assertEqual(self.source.read_bytes(), base)

    def test_internal_marker_bytes_are_hashed_as_part_of_the_base_elf(self):
        base = minimal_elf_with_payload(AUTH_MARKER + b"ordinary ELF payload bytes")
        self.source.write_bytes(base)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.output.read_bytes(),
            base
            + AUTH_MARKER
            + hashlib.sha256(base).hexdigest().encode("ascii")
            + b"\n",
        )

    def test_rejects_malformed_terminal_trailer_variants(self):
        base = minimal_elf()
        digest = hashlib.sha256(base).hexdigest().encode("ascii")
        cases = {
            "missing newline": digest,
            "extra newline": digest + b"\n\n",
            "short digest": digest[:-1] + b"\n",
            "long digest": digest + b"0\n",
            "extra suffix": digest + b"\nextra",
        }
        for label, suffix in cases.items():
            with self.subTest(label=label):
                self.source.write_bytes(base + AUTH_MARKER + suffix)
                result = self.invoke()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("authentication trailer", result.stderr)
                self.assertFalse(self.output.exists())

    def test_rejects_output_directory_symlink_escape(self):
        self.source.write_bytes(minimal_elf())
        outside = self.base / "outside"
        outside.mkdir()
        link = self.work / "linked-output"
        link.symlink_to(outside, target_is_directory=True)
        result = self.invoke(output=link / "application.elf")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("current working directory", result.stderr)
        self.assertFalse((outside / "application.elf").exists())

    def test_rejects_validator_symlink_escape(self):
        self.source.write_bytes(minimal_elf())
        validator = self.root / "bin" / "eos-elf-validate"
        validator.unlink()
        outside = self.base / "outside-validator"
        outside.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        outside.chmod(0o755)
        validator.symlink_to(outside)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside EOS_RUST_SDK_ROOT", result.stderr)


if __name__ == "__main__":
    unittest.main()
