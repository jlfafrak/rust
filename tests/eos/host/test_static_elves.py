#!/usr/bin/env python3
"""Build and statically gate EOS Rust applications and C/Rust ABI objects."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
TARGET = "armv7a-unknown-eos-eabi"
APPLICATIONS = ("filesystem", "threads-tls", "network", "process", "ffi-abi")
PROFILES = ("debug", "release")
TARGET_FLAGS = (
    "-march=armv7-a",
    "-mtune=cortex-a9",
    "-mfpu=neon-vfpv3",
    "-mfloat-abi=softfp",
)
AUTH_MARKER = b"martos_smp_elf_authentication_block_sha2_256_adbc_1394_e532_101\n"
AUTH_TRAILER_LENGTH = len(AUTH_MARKER) + 64 + 1


def run(
    command: list[str | Path],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(part) for part in command],
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=600,
        check=False,
    )
    if check and result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(map(str, command))}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result


def sdk_root() -> Path:
    configured = os.environ.get("EOS_RUST_SDK_ROOT")
    if not configured:
        raise AssertionError("EOS_RUST_SDK_ROOT must name the reviewed Task 16 SDK")
    root = Path(configured).resolve(strict=True)
    required = (
        "bin/cargo",
        "bin/rustc",
        "bin/eos-rust-link",
        "bin/eos-elf-validate",
        "bin/eos-auth-package",
        "arm-gnu/bin/arm-none-eabi-gcc",
        "arm-gnu/bin/arm-none-eabi-readelf",
    )
    for relative in required:
        path = (root / relative).resolve(strict=True)
        path.relative_to(root)
        if not path.is_file() or not os.access(path, os.X_OK):
            raise AssertionError(f"required SDK tool is not executable: {path}")
    return root


def arm_objdump(sdk: Path) -> Path:
    configured = os.environ.get("EOS_ARM_GNU_OBJDUMP")
    candidates = (
        [Path(configured)]
        if configured
        else [sdk / "arm-gnu" / "bin" / "arm-none-eabi-objdump"]
    )
    for candidate in candidates:
        try:
            resolved = candidate.resolve(strict=True)
        except OSError:
            continue
        if not resolved.is_file() or not os.access(resolved, os.X_OK):
            continue
        version = run([resolved, "--version"], cwd=ROOT).stdout.splitlines()
        if version and re.search(r"\b2\.44(?:\.\d+)?\b", version[0]):
            return resolved
        raise AssertionError(f"ARM objdump is not pinned binutils 2.44: {resolved}")
    raise AssertionError(
        "pinned arm-none-eabi-objdump 2.44 is unavailable; set EOS_ARM_GNU_OBJDUMP"
    )


def arm_gcc(sdk: Path) -> Path:
    configured = os.environ.get("EOS_ARM_GNU_CC")
    candidate = Path(configured) if configured else sdk / "arm-gnu" / "bin" / "arm-none-eabi-gcc"
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise AssertionError(
            "complete ARM GNU compiler is unavailable; set EOS_ARM_GNU_CC"
        ) from error
    version = run([resolved, "--version"], cwd=ROOT).stdout.splitlines()
    if not version or re.search(r"\b14\.3\.1\b", version[0]) is None:
        raise AssertionError(f"ARM GCC is not pinned version 14.3.1: {resolved}")
    cc1 = run([resolved, "-print-prog-name=cc1"], cwd=ROOT).stdout.strip()
    cc1_path = Path(cc1)
    if not cc1_path.is_absolute():
        cc1_path = resolved.parent / cc1_path
    if not cc1_path.resolve(strict=False).is_file():
        raise AssertionError(
            f"ARM GCC has no usable cc1 compilation closure: {resolved}; set EOS_ARM_GNU_CC"
        )
    return resolved


def build_environment(sdk: Path, home: Path) -> dict[str, str]:
    environment = os.environ.copy()
    for name in (
        "CARGO_ENCODED_RUSTFLAGS",
        "CARGO_TARGET_DIR",
        "COMPILER_PATH",
        "LD_PRELOAD",
        "LIBRARY_PATH",
        "RUSTC",
        "RUSTFLAGS",
        "RUSTUP_HOME",
    ):
        environment.pop(name, None)
    environment.update(
        {
            "CARGO_HOME": str(home / "cargo-home"),
            "CARGO_NET_OFFLINE": "true",
            "CARGO_TARGET_ARMV7A_UNKNOWN_EOS_EABI_LINKER": str(
                sdk / "bin" / "eos-rust-link"
            ),
            "EOS_RUST_SDK_ROOT": str(sdk),
            "HOME": str(home),
            "LANG": "C",
            "LC_ALL": "C",
            "PATH": os.pathsep.join((str(sdk / "bin"), os.defpath)),
            "RUSTC": str(sdk / "bin" / "rustc"),
            "RUSTFLAGS": "-C link-arg=-Wl,--build-id=sha1",
            "RUSTUP_TOOLCHAIN": "eos-1.97.1",
        }
    )
    return environment


def build_id(readelf: Path, elf: Path, cwd: Path) -> str:
    notes = run([readelf, "-nW", elf], cwd=cwd).stdout
    match = re.search(r"Build ID:\s*([0-9a-f]+)", notes)
    if match is None:
        raise AssertionError(f"ELF has no GNU build ID: {elf}")
    return match.group(1)


def verify_authentication_trailer(base: Path, authenticated: Path) -> None:
    base_bytes = base.read_bytes()
    authenticated_bytes = authenticated.read_bytes()
    expected = (
        base_bytes
        + AUTH_MARKER
        + hashlib.sha256(base_bytes).hexdigest().encode("ascii")
        + b"\n"
    )
    if len(authenticated_bytes) != len(base_bytes) + AUTH_TRAILER_LENGTH:
        raise AssertionError("authenticated ELF trailer length is not exact")
    if authenticated_bytes != expected:
        raise AssertionError("authenticated ELF trailer bytes do not match the base ELF")


def build_application(
    sdk: Path, app_name: str, profile: str, work: Path, artifacts: Path
) -> tuple[Path, Path, str]:
    source = ROOT / "tests" / "eos" / "apps" / app_name
    if not source.is_dir():
        raise AssertionError(f"missing representative application: {source}")
    app = work / f"{app_name}-{profile}"
    shutil.copytree(source, app)
    environment = build_environment(sdk, app)
    command: list[str | Path] = [
        sdk / "bin" / "cargo",
        "build",
        "--offline",
        "--target",
        TARGET,
    ]
    if profile == "release":
        command.append("--release")
    run(command, cwd=app, env=environment)

    binary_name = app_name.replace("-", "_")
    candidate = app / "target" / TARGET / profile / app_name
    if not candidate.is_file():
        alternate = candidate.with_name(binary_name)
        if alternate.is_file():
            candidate = alternate
        else:
            raise AssertionError(f"Cargo did not produce the expected EOS ELF: {candidate}")

    validator = sdk / "bin" / "eos-elf-validate"
    packager = sdk / "bin" / "eos-auth-package"
    readelf = sdk / "arm-gnu" / "bin" / "arm-none-eabi-readelf"
    run([validator, candidate], cwd=app, env=environment)
    sections = run([readelf, "-SW", candidate], cwd=app).stdout
    if ".symtab" not in sections:
        raise AssertionError(f"base ELF was stripped before retention: {candidate}")
    identifier = build_id(readelf, candidate, app)

    artifacts.mkdir(parents=True, exist_ok=True)
    retained = artifacts / f"{app_name}-{profile}.elf"
    shutil.copy2(candidate, retained)
    identifier_file = retained.with_suffix(".build-id")
    identifier_file.write_text(identifier + "\n", encoding="ascii")
    authenticated = artifacts / f"{app_name}-{profile}.auth.elf"
    run([packager, retained, authenticated], cwd=artifacts, env=environment)
    run([validator, "--allow-auth-trailer", authenticated], cwd=artifacts, env=environment)
    verify_authentication_trailer(retained, authenticated)
    if build_id(readelf, retained, artifacts) != identifier:
        raise AssertionError("retained ELF build ID changed after copying")
    return retained, authenticated, identifier


def arm_attributes(readelf: Path, obj: Path, cwd: Path) -> tuple[str, str]:
    header = run([readelf, "-hW", obj], cwd=cwd).stdout
    attributes = run([readelf, "-AW", obj], cwd=cwd).stdout
    flags = re.search(r"Flags:\s+0x([0-9a-fA-F]+),\s+Version5 EABI", header)
    if flags is None or int(flags.group(1), 16) & 0xFF000000 != 0x05000000:
        raise AssertionError(f"object is not EABI5: {obj}")
    if int(flags.group(1), 16) & 0x400 or "Tag_ABI_VFP_args" in attributes:
        raise AssertionError(f"object declares hard-float argument passing: {obj}")
    return header, attributes


def build_softfp_objects(sdk: Path, work: Path, artifacts: Path) -> None:
    c_source = ROOT / "tests" / "eos" / "abi" / "softfp.c"
    rust_source = ROOT / "tests" / "eos" / "abi" / "softfp.rs"
    if not c_source.is_file() or not rust_source.is_file():
        raise AssertionError("missing bidirectional C/Rust softfp fixture")

    gcc = arm_gcc(sdk)
    objdump = arm_objdump(sdk)
    readelf = sdk / "arm-gnu" / "bin" / "arm-none-eabi-readelf"
    rustc = sdk / "bin" / "rustc"
    c_soft = work / "softfp-c.o"
    c_hard = work / "hardfp-c.o"
    rust = work / "softfp-rust.o"

    common_c = [
        gcc,
        "-march=armv7-a",
        "-mtune=cortex-a9",
        "-mfpu=neon-vfpv3",
        "-fPIC",
        "-ffunction-sections",
        "-fdata-sections",
        "-O2",
        "-std=c11",
        "-c",
        c_source,
    ]
    run([*common_c[:4], "-mfloat-abi=softfp", *common_c[4:], "-o", c_soft], cwd=work)
    run([*common_c[:4], "-mfloat-abi=hard", *common_c[4:], "-o", c_hard], cwd=work)
    run(
        [
            rustc,
            rust_source,
            "--crate-name",
            "eos_softfp",
            "--crate-type",
            "lib",
            "--edition=2024",
            "--target",
            TARGET,
            "--sysroot",
            sdk,
            "--emit=obj",
            "-C",
            "opt-level=2",
            "-C",
            "relocation-model=pic",
            "-C",
            "panic=abort",
            "-o",
            rust,
        ],
        cwd=work,
    )

    for obj in (c_soft, rust):
        arm_attributes(readelf, obj, work)
    hard_header = run([readelf, "-hW", c_hard], cwd=work).stdout
    hard_attributes = run([readelf, "-AW", c_hard], cwd=work).stdout
    if "Version5 EABI" not in hard_header or "Tag_ABI_VFP_args" not in hard_attributes:
        raise AssertionError("hard-float negative control does not declare VFP arguments")
    with unittest.TestCase().assertRaisesRegex(AssertionError, "hard-float"):
        arm_attributes(readelf, c_hard, work)

    c_disassembly = run([objdump, "-dr", c_soft], cwd=work).stdout
    rust_disassembly = run([objdump, "-dr", rust], cwd=work).stdout
    for label, text, callees in (
        ("C", c_disassembly, ("eos_rust_accept", "eos_rust_return")),
        ("Rust", rust_disassembly, ("eos_c_accept", "eos_c_return")),
    ):
        if re.search(r"\bv(?:add|mul|sub)\.f(?:32|64)\b", text) is None:
            raise AssertionError(f"{label} fixture emitted no VFP arithmetic")
        for callee in callees:
            if re.search(
                rf"R_ARM_(?:CALL|THM_CALL|JUMP24|THM_JUMP24)\s+{callee}\b", text
            ) is None:
                raise AssertionError(f"{label} fixture does not call {callee}")
    if re.search(r"\bvadd\.f32\s+q", c_disassembly) is None:
        raise AssertionError("C fixture emitted no expected NEON vector arithmetic")

    pair = work / "softfp-pair.o"
    run([gcc, *TARGET_FLAGS, "-nostdlib", "-r", c_soft, rust, "-o", pair], cwd=work)
    hard_pair = run(
        [gcc, *TARGET_FLAGS[:-1], "-mfloat-abi=hard", "-nostdlib", "-r", c_hard, rust, "-o", work / "hardfp-pair.o"],
        cwd=work,
        check=False,
    )
    if hard_pair.returncode == 0:
        raise AssertionError("hard-float C object was incorrectly accepted with Rust softfp")
    if "uses VFP register arguments" not in hard_pair.stderr:
        raise AssertionError(f"hard-float rejection was not an ABI mismatch: {hard_pair.stderr}")

    artifacts.mkdir(parents=True, exist_ok=True)
    for source, name in (
        (c_soft, "softfp-c.o"),
        (rust, "softfp-rust.o"),
        (pair, "softfp-pair.o"),
    ):
        shutil.copy2(source, artifacts / name)
    (artifacts / "softfp-c.objdump.txt").write_text(c_disassembly, encoding="utf-8")
    (artifacts / "softfp-rust.objdump.txt").write_text(rust_disassembly, encoding="utf-8")
    (artifacts / "softfp-c.attributes.txt").write_text(
        run([readelf, "-AW", c_soft], cwd=work).stdout, encoding="utf-8"
    )
    (artifacts / "softfp-rust.attributes.txt").write_text(
        run([readelf, "-AW", rust], cwd=work).stdout, encoding="utf-8"
    )


class StaticElfGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sdk = sdk_root()
        cls.temporary = tempfile.TemporaryDirectory(prefix="eos-static-elves-")
        cls.work = Path(cls.temporary.name)
        configured = os.environ.get("EOS_CI_ARTIFACT_DIR")
        cls.artifacts = (
            Path(configured).resolve() / "static-tests"
            if configured
            else cls.work / "artifacts" / "static-tests"
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_representative_applications_cross_link_debug_and_release(self) -> None:
        for app_name in APPLICATIONS:
            for profile in PROFILES:
                with self.subTest(application=app_name, profile=profile):
                    retained, authenticated, identifier = build_application(
                        self.sdk, app_name, profile, self.work, self.artifacts
                    )
                    self.assertTrue(retained.is_file())
                    self.assertTrue(authenticated.is_file())
                    self.assertRegex(identifier, r"^[0-9a-f]{40}$")

    def test_softfp_objects_match_and_hard_float_control_fails(self) -> None:
        build_softfp_objects(self.sdk, self.work, self.artifacts)
        self.assertTrue((self.artifacts / "softfp-pair.o").is_file())


class CiEntryPointPolicyTests(unittest.TestCase):
    def test_ci_entrypoint_fails_closed_without_reviewed_sdk(self) -> None:
        script = ROOT / "tests" / "eos" / "run-ci.sh"
        self.assertTrue(script.is_file(), "missing EOS CI entrypoint")
        self.assertTrue(os.access(script, os.X_OK), "EOS CI entrypoint is not executable")
        environment = os.environ.copy()
        environment.pop("EOS_RUST_SDK_ROOT", None)
        result = run([script], cwd=ROOT, env=environment, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("EOS_RUST_SDK_ROOT", result.stderr)

    def test_workflow_only_runs_ci_entrypoint_and_uploads_gate_artifacts(self) -> None:
        workflow = ROOT / ".github" / "workflows" / "eos.yml"
        self.assertTrue(workflow.is_file(), "missing EOS workflow")
        source = workflow.read_text(encoding="utf-8")
        commands = re.findall(r"^\s*run:\s*([^\n]+)$", source, re.MULTILINE)
        self.assertEqual(commands, ["tests/eos/run-ci.sh"])
        self.assertEqual(source.count("uses: actions/upload-artifact@"), 2)
        self.assertIn("name: eos-sdk", source)
        self.assertIn("name: eos-static-tests", source)
        self.assertNotRegex(
            source,
            re.compile(
                r"\b(?:password|credential|secret|board|deploy|flash|scp|sftp|rsync|ssh)\b"
                r"|(?:\d{1,3}\.){3}\d{1,3}",
                re.IGNORECASE,
            ),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
