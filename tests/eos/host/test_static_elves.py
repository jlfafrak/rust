#!/usr/bin/env python3
"""Build and statically gate EOS Rust applications and C/Rust ABI objects."""

from __future__ import annotations

import functools
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
TARGET = "armv7a-unknown-eos-eabi"
APPLICATIONS = (
    "hello-std",
    "filesystem",
    "threads-tls",
    "network",
    "process",
    "ffi-abi",
    "unwind",
    "ffi-containment",
)
PROFILES = ("debug", "release")
TARGET_FLAGS = (
    "-march=armv7-a",
    "-mtune=cortex-a9",
    "-mfpu=neon-vfpv3",
    "-mfloat-abi=softfp",
)
AUTH_MARKER = b"martos_smp_elf_authentication_block_sha2_256_adbc_1394_e532_101\n"
AUTH_TRAILER_LENGTH = len(AUTH_MARKER) + 64 + 1
REVIEWED_SDK_TREE_SHA256 = (
    "309cd5d682c09dfd65e1ff0f2c0ee5d87e390543bcdfd09af52b49f8327c5060"
)
REVIEWED_ARM_GNU_TREE_SHA256 = (
    "a407c7186f68473d2fb7a0bae59407261adef62f959fc181e5cd7eb7636a584d"
)
REVIEWED_EOS_INPUT_TREE_SHA256 = (
    "5e6c7db4d67a971307f59797a3bf092a516a8dfcd156a2706b7c337e19119910"
)
REVIEWED_RELEASE = {
    "version": "1.97.1",
    "toolchain": "eos-1.97.1",
    "target": TARGET,
    "layout_version": 1,
}
REVIEWED_SOURCE_REVISIONS = {
    "rust_fork": "baeb1a5b368d15d98a5433a3aadd216f472bf9e2",
    "rust_upstream": "8bab26f4f68e0e26f0bb7960be334d5b520ea452",
    "libc_upstream": "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8",
    "backtrace": "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd",
}


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


def sha256_tree_v1(root: Path) -> str:
    digest = hashlib.sha256()
    try:
        entries = sorted(
            root.rglob("*"), key=lambda entry: entry.relative_to(root).as_posix()
        )
        for path in entries:
            relative = path.relative_to(root).as_posix().encode("utf-8")
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                digest.update(
                    b"L\0"
                    + relative
                    + b"\0"
                    + os.readlink(path).encode("utf-8")
                    + b"\0"
                )
            elif stat.S_ISREG(mode):
                digest.update(b"F\0" + relative + b"\0")
                with path.open("rb") as source:
                    while chunk := source.read(1024 * 1024):
                        digest.update(chunk)
                digest.update(b"\0")
            elif stat.S_ISDIR(mode):
                digest.update(b"D\0" + relative + b"\0")
            else:
                raise AssertionError(f"unsupported release-tree entry: {path}")
    except OSError as error:
        raise AssertionError(f"unable to hash release tree {root}: {error}") from error
    return digest.hexdigest()


def configured_tool(variable: str, description: str) -> Path:
    configured = os.environ.get(variable)
    if not configured:
        raise AssertionError(f"{variable} must name the reviewed {description}")
    try:
        tool = Path(configured).resolve(strict=True)
    except OSError as error:
        raise AssertionError(f"reviewed {description} is unavailable: {configured}") from error
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise AssertionError(f"reviewed {description} is not executable: {tool}")
    return tool


@functools.cache
def verify_release_identity(sdk: Path, gcc: Path, objdump: Path) -> Path:
    if sha256_tree_v1(sdk) != REVIEWED_SDK_TREE_SHA256:
        raise AssertionError(f"reviewed Task 16 SDK tree identity mismatch: {sdk}")
    manifest_path = sdk / "manifests" / "release-manifest.toml"
    try:
        manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        raise AssertionError(f"reviewed Task 16 release manifest is invalid: {error}") from error
    expected_input_hashes = {
        "algorithm": "sha256-tree-v1",
        "arm_gnu_installed": REVIEWED_ARM_GNU_TREE_SHA256,
        "eos_sdk": REVIEWED_EOS_INPUT_TREE_SHA256,
    }
    if (
        manifest.get("release") != REVIEWED_RELEASE
        or manifest.get("input_hashes") != expected_input_hashes
        or manifest.get("source_revisions") != REVIEWED_SOURCE_REVISIONS
    ):
        raise AssertionError("reviewed Task 16 release metadata identity mismatch")

    arm_root = gcc.parent.parent
    try:
        expected_gcc = (arm_root / "bin" / "arm-none-eabi-gcc").resolve(
            strict=True
        )
        expected_objdump = (arm_root / "bin" / "arm-none-eabi-objdump").resolve(
            strict=True
        )
    except OSError as error:
        raise AssertionError(f"ARM GNU root is incomplete: {arm_root}: {error}") from error
    if gcc != expected_gcc or objdump != expected_objdump:
        raise AssertionError("ARM GCC and objdump must come from one reviewed ARM GNU root")
    actual_arm_hash = sha256_tree_v1(arm_root)
    if actual_arm_hash != manifest["input_hashes"]["arm_gnu_installed"]:
        raise AssertionError(
            f"ARM GNU tree identity mismatch: {actual_arm_hash} != "
            f"{manifest['input_hashes']['arm_gnu_installed']}"
        )
    return arm_root


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
    verify_release_identity(
        root,
        configured_tool("EOS_ARM_GNU_CC", "ARM GNU 14.3.1 compiler"),
        configured_tool("EOS_ARM_GNU_OBJDUMP", "binutils 2.44 objdump"),
    )
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
        verify_release_identity(
            sdk,
            configured_tool("EOS_ARM_GNU_CC", "ARM GNU 14.3.1 compiler"),
            resolved,
        )
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
    verify_release_identity(
        sdk,
        resolved,
        configured_tool("EOS_ARM_GNU_OBJDUMP", "binutils 2.44 objdump"),
    )
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


def retain_application_artifacts(
    sdk: Path,
    candidate: Path,
    app_name: str,
    profile: str,
    cwd: Path,
    environment: dict[str, str],
    artifacts: Path,
) -> tuple[Path, Path, str]:
    validator = sdk / "bin" / "eos-elf-validate"
    packager = sdk / "bin" / "eos-auth-package"
    readelf = sdk / "arm-gnu" / "bin" / "arm-none-eabi-readelf"
    run([validator, candidate], cwd=cwd, env=environment)
    sections = run([readelf, "-SW", candidate], cwd=cwd).stdout
    if ".symtab" not in sections:
        raise AssertionError(f"base ELF was stripped before retention: {candidate}")
    identifier = build_id(readelf, candidate, cwd)

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
    return retain_application_artifacts(
        sdk, candidate, app_name, profile, app, environment, artifacts
    )


def build_ffi_containment_application(
    sdk: Path, profile: str, work: Path, artifacts: Path
) -> tuple[Path, Path, str]:
    source = ROOT / "tests" / "eos" / "apps" / "ffi-containment"
    caller_source = ROOT / "tests" / "eos" / "abi" / "ffi_caller.c"
    if not source.is_dir() or not caller_source.is_file():
        raise AssertionError("missing Rust staticlib or real C caller containment fixture")

    app = work / f"ffi-containment-{profile}"
    shutil.copytree(source, app)
    caller = app / "ffi_caller.c"
    shutil.copy2(caller_source, caller)
    environment = build_environment(sdk, app)
    cargo_command: list[str | Path] = [
        sdk / "bin" / "cargo",
        "build",
        "--offline",
        "--target",
        TARGET,
    ]
    if profile == "release":
        cargo_command.append("--release")
    run(cargo_command, cwd=app, env=environment)

    staticlib = app / "target" / TARGET / profile / "libffi_containment.a"
    if not staticlib.is_file():
        raise AssertionError(f"Cargo did not produce the reviewed Rust staticlib: {staticlib}")
    caller_object = app / "ffi_caller.o"
    run(
        [
            arm_gcc(sdk),
            *TARGET_FLAGS,
            "-fPIC",
            "-ffunction-sections",
            "-fdata-sections",
            "-O2",
            "-std=c11",
            "-c",
            caller,
            "-o",
            caller_object,
        ],
        cwd=app,
    )

    candidate = app / "ffi-containment.elf"
    run(
        [
            sdk / "bin" / "eos-rust-link",
            "-Wl,--build-id=sha1",
            "-o",
            candidate,
            caller_object,
            staticlib,
        ],
        cwd=app,
        env=environment,
    )
    return retain_application_artifacts(
        sdk,
        candidate,
        "ffi-containment",
        profile,
        app,
        environment,
        artifacts,
    )


def arm_attributes(
    readelf: Path,
    obj: Path,
    cwd: Path,
    *,
    expected_cpu_name: str | None = None,
) -> tuple[str, str]:
    header = run([readelf, "-hW", obj], cwd=cwd).stdout
    attributes = run([readelf, "-AW", obj], cwd=cwd).stdout
    flags = re.search(r"Flags:\s+0x([0-9a-fA-F]+),\s+Version5 EABI", header)
    if flags is None or int(flags.group(1), 16) & 0xFF000000 != 0x05000000:
        raise AssertionError(f"object is not EABI5: {obj}")
    if int(flags.group(1), 16) & 0x400 or "Tag_ABI_VFP_args" in attributes:
        raise AssertionError(f"object declares hard-float argument passing: {obj}")
    required_attributes = (
        (r"^\s*Tag_CPU_arch:\s+v7\s*$", "ARMv7-A architecture"),
        (
            r"^\s*Tag_CPU_arch_profile:\s+Application\s*$",
            "ARMv7-A Application profile",
        ),
        (r"^\s*Tag_ARM_ISA_use:\s+Yes\s*$", "ARM instruction set"),
        (r"^\s*Tag_THUMB_ISA_use:\s+Thumb-2\s*$", "Thumb-2"),
        (r"^\s*Tag_FP_arch:\s+VFPv3\s*$", "VFPv3"),
        (r"^\s*Tag_Advanced_SIMD_arch:\s+NEONv1\s*$", "NEONv1"),
    )
    for pattern, description in required_attributes:
        if re.search(pattern, attributes, re.MULTILINE) is None:
            raise AssertionError(f"object lacks exact {description} attribute: {obj}")
    if expected_cpu_name is not None and re.search(
        rf'^\s*Tag_CPU_name:\s+"{re.escape(expected_cpu_name)}"\s*$',
        attributes,
        re.MULTILINE,
    ) is None:
        raise AssertionError(
            f"object lacks expected CPU name {expected_cpu_name}: {obj}"
        )
    return header, attributes


def require_gcc_target_configuration(gcc: Path, cwd: Path) -> str:
    configuration = run([gcc, "-Q", "--help=target", *TARGET_FLAGS], cwd=cwd).stdout
    required = (
        (r"^\s*-march=\s+armv7-a\+simd\s*$", "ARMv7-A architecture"),
        (r"^\s*-mtune=\s+cortex-a9\s*$", "cortex-a9 tuning"),
        (r"^\s*-mfpu=\s+neon-vfpv3\s*$", "NEON/VFPv3 selection"),
        (r"^\s*-mfloat-abi=\s+softfp\s*$", "softfp ABI"),
    )
    for pattern, description in required:
        if re.search(pattern, configuration, re.MULTILINE) is None:
            raise AssertionError(f"ARM GCC does not select exact {description}")
    return configuration


def function_body(disassembly: str, symbol: str) -> str:
    match = re.search(
        rf"^[0-9a-f]+ <{re.escape(symbol)}>:\n(?P<body>.*?)(?=^Disassembly of section|\Z)",
        disassembly,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"aggregate ABI function is absent from disassembly: {symbol}")
    return match.group("body")


def stack_field_sequence(
    body: str, base: str, entry_bias: int
) -> tuple[tuple[int, str], ...]:
    fields: list[tuple[int, str]] = []
    memory = re.compile(
        r"\b(?P<opcode>vldr|ldr)\s+"
        r"(?P<register>[sd][0-9]+|r(?:1[0-5]|[0-9])|ip),\s*"
        r"\[(?P<base>sp|fp)(?:,\s*#(?P<offset>[0-9]+))?\]"
    )
    for match in memory.finditer(body):
        if match.group("base") != base:
            continue
        offset = int(match.group("offset") or 0) - entry_bias
        register = match.group("register")
        kind = (
            "f32"
            if register.startswith("s")
            else "f64"
            if register.startswith("d")
            else "i32"
        )
        fields.append((offset, kind))
    return tuple(sorted(fields))


def aggregate_return_sequence(body: str) -> tuple[tuple[int, str], ...]:
    fields: list[tuple[int, str]] = []
    memory = re.compile(
        r"\b(?P<opcode>vstr|str)\s+"
        r"(?P<register>[sd][0-9]+|r(?:1[0-5]|[0-9])|ip),\s*"
        r"\[r0(?:,\s*#(?P<offset>[0-9]+))?\]"
    )
    for match in memory.finditer(body):
        register = match.group("register")
        kind = (
            "f32"
            if register.startswith("s")
            else "f64"
            if register.startswith("d")
            else "i32"
        )
        fields.append((int(match.group("offset") or 0), kind))
    return tuple(sorted(fields))


def softfp_abi_sequence(disassembly: str, prefix: str) -> dict[str, tuple]:
    accept = function_body(disassembly, f"eos_{prefix}_accept")
    returned = function_body(disassembly, f"eos_{prefix}_return")
    stack_base = "sp" if prefix == "c" else "fp"
    entry_bias = 0 if prefix == "c" else 8

    accept_register_evidence = (
        re.search(r"\badd\s+r[01],\s*r[01],\s*r[01]\b", accept) is not None,
        re.search(r"\bvmov\s+s[0-9]+,\s*r2\b", accept) is not None,
    )
    return_register_evidence = (
        re.search(r"\badd\s+r1,\s*r1,\s*r2\b", returned) is not None,
        re.search(r"\bvmov\s+s[0-9]+,\s*r3\b", returned) is not None,
    )
    if not all(accept_register_evidence):
        raise AssertionError(
            f"{prefix} aggregate ABI accept register sequence is incompatible"
        )
    if not all(return_register_evidence):
        raise AssertionError(
            f"{prefix} aggregate ABI return register sequence is incompatible"
        )

    return {
        "accept_registers": (("r0", "i32"), ("r1", "enum"), ("r2", "f32")),
        "accept_stack": stack_field_sequence(accept, stack_base, entry_bias),
        "return_registers": (
            ("r0", "aggregate-sret"),
            ("r1", "i32"),
            ("r2", "enum"),
            ("r3", "f32"),
        ),
        "return_stack": stack_field_sequence(returned, stack_base, entry_bias),
        "aggregate_return": aggregate_return_sequence(returned),
    }


def compare_softfp_abi_sequences(
    c_disassembly: str, rust_disassembly: str
) -> dict[str, tuple]:
    c_sequence = softfp_abi_sequence(c_disassembly, "c")
    rust_sequence = softfp_abi_sequence(rust_disassembly, "rust")
    expected = {
        "accept_registers": (("r0", "i32"), ("r1", "enum"), ("r2", "f32")),
        "accept_stack": ((0, "f64"), (8, "i32"), (12, "f32"), (16, "f64")),
        "return_registers": (
            ("r0", "aggregate-sret"),
            ("r1", "i32"),
            ("r2", "enum"),
            ("r3", "f32"),
        ),
        "return_stack": ((0, "f64"), (8, "i32"), (12, "f32"), (16, "f64")),
        "aggregate_return": ((0, "i32"), (4, "f32"), (8, "f64")),
    }
    if c_sequence != rust_sequence:
        raise AssertionError(
            f"C/Rust aggregate ABI sequences differ: "
            f"C={c_sequence!r}, Rust={rust_sequence!r}"
        )
    if c_sequence != expected:
        raise AssertionError(
            f"C/Rust aggregate ABI sequence is not the approved softfp layout: {c_sequence!r}"
        )
    return c_sequence


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
    gcc_configuration = require_gcc_target_configuration(gcc, work)

    common_c = [
        gcc,
        *TARGET_FLAGS,
        "-fPIC",
        "-ffunction-sections",
        "-fdata-sections",
        "-O2",
        "-std=c11",
        "-c",
        c_source,
    ]
    run([*common_c, "-o", c_soft], cwd=work)
    hard_c = [
        "-mfloat-abi=hard" if flag == "-mfloat-abi=softfp" else flag
        for flag in common_c
    ]
    run([*hard_c, "-o", c_hard], cwd=work)
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

    arm_attributes(readelf, c_soft, work, expected_cpu_name="7-A")
    arm_attributes(readelf, rust, work, expected_cpu_name="cortex-a9")
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
        if re.search(r"\bv(?:add|mul|sub)\.f32\s+s", text) is None:
            raise AssertionError(f"{label} fixture emitted no scalar f32 VFP arithmetic")
        if re.search(r"\bv(?:add|mul|sub)\.f64\s+d", text) is None:
            raise AssertionError(f"{label} fixture emitted no scalar f64 VFP arithmetic")
        for callee in callees:
            if re.search(
                rf"R_ARM_(?:CALL|THM_CALL|JUMP24|THM_JUMP24)\s+{callee}\b", text
            ) is None:
                raise AssertionError(f"{label} fixture does not call {callee}")
    if re.search(r"\bvadd\.f32\s+q", c_disassembly) is None:
        raise AssertionError("C fixture emitted no expected NEON vector arithmetic")
    abi_sequence = compare_softfp_abi_sequences(c_disassembly, rust_disassembly)

    pair = work / "softfp-pair.o"
    run([gcc, *TARGET_FLAGS, "-nostdlib", "-r", c_soft, rust, "-o", pair], cwd=work)
    hard_pair = run(
        [
            gcc,
            *TARGET_FLAGS[:-1],
            "-mfloat-abi=hard",
            "-nostdlib",
            "-r",
            c_hard,
            rust,
            "-o",
            work / "hardfp-pair.o",
        ],
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
    (artifacts / "softfp-c.target-options.txt").write_text(
        gcc_configuration, encoding="utf-8"
    )
    (artifacts / "softfp-abi-sequence.json").write_text(
        json.dumps(abi_sequence, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


class ApplicationEvidencePolicyTests(unittest.TestCase):
    def application_source(self, name: str) -> str:
        return (
            ROOT / "tests" / "eos" / "apps" / name / "src" / "main.rs"
        ).read_text(encoding="utf-8")

    def test_process_probe_self_spawns_without_unsupported_child_overrides(self) -> None:
        source = self.application_source("process")
        self.assertIn("current_exe()", source)
        self.assertIn('"--child"', source)
        self.assertNotIn("/eos/rust-process-child", source)
        self.assertNotIn(".env(", source)
        self.assertNotIn(".current_dir(", source)

    def test_network_probe_runs_numeric_transport_before_fallible_dns(self) -> None:
        source = self.application_source("network")
        self.assertNotIn("to_socket_addrs()?", source)
        self.assertLess(source.index("TcpStream::connect"), source.index("to_socket_addrs()"))
        self.assertLess(source.index("UdpSocket::bind"), source.index("to_socket_addrs()"))

    def test_unwind_probe_force_captures_backtrace_evidence(self) -> None:
        source = self.application_source("unwind")
        self.assertIn("Backtrace::force_capture()", source)

    def test_static_application_set_equals_board_policy(self) -> None:
        manifest = tomllib.loads(
            (
                ROOT / "tests" / "eos" / "board" / "board-test-manifest.toml"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(set(APPLICATIONS), set(manifest["applications"]))


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
                    if app_name == "ffi-containment":
                        retained, authenticated, identifier = (
                            build_ffi_containment_application(
                                self.sdk, profile, self.work, self.artifacts
                            )
                        )
                    else:
                        retained, authenticated, identifier = build_application(
                            self.sdk, app_name, profile, self.work, self.artifacts
                        )
                    self.assertTrue(retained.is_file())
                    self.assertTrue(authenticated.is_file())
                    self.assertTrue(retained.with_suffix(".build-id").is_file())
                    self.assertRegex(identifier, r"^[0-9a-f]{40}$")

    def test_softfp_objects_match_and_hard_float_control_fails(self) -> None:
        build_softfp_objects(self.sdk, self.work, self.artifacts)
        self.assertTrue((self.artifacts / "softfp-pair.o").is_file())

    def isolated_abi_fixture(
        self,
        name: str,
        *,
        c_replacement: tuple[str, str] | None = None,
        rust_replacement: tuple[str, str] | None = None,
    ) -> Path:
        fixture_root = self.work / name
        fixture = fixture_root / "tests" / "eos" / "abi"
        fixture.mkdir(parents=True)
        for language, replacement in (
            ("c", c_replacement),
            ("rs", rust_replacement),
        ):
            source = (ROOT / "tests" / "eos" / "abi" / f"softfp.{language}").read_text(
                encoding="utf-8"
            )
            if replacement is not None:
                old, new = replacement
                self.assertIn(old, source)
                source = source.replace(old, new)
            (fixture / f"softfp.{language}").write_text(source, encoding="utf-8")
        return fixture_root

    def test_softfp_gate_rejects_mixed_aggregate_layout_mutation(self) -> None:
        original = """typedef struct eos_mixed {
    int32_t integer;
    float single;
    double double_value;
} eos_mixed;"""
        incompatible = """typedef struct eos_mixed {
    float single;
    int32_t integer;
    double double_value;
} eos_mixed;"""
        fixture_root = self.isolated_abi_fixture(
            "mixed-layout-mutation", c_replacement=(original, incompatible)
        )
        work = self.work / "mixed-layout-work"
        work.mkdir()
        with mock.patch.dict(globals(), {"ROOT": fixture_root}):
            with self.assertRaisesRegex(AssertionError, "aggregate ABI"):
                build_softfp_objects(
                    self.sdk,
                    work,
                    self.work / "mixed-layout-artifacts",
                )

    def test_softfp_gate_requires_scalar_f32_in_both_languages(self) -> None:
        fixture_root = self.isolated_abi_fixture(
            "missing-scalar-f32",
            c_replacement=("single + mixed.single", "0.0f"),
            rust_replacement=("single + mixed.single", "0.0"),
        )
        work = self.work / "missing-f32-work"
        work.mkdir()
        with mock.patch.dict(globals(), {"ROOT": fixture_root}):
            with self.assertRaisesRegex(AssertionError, "scalar f32"):
                build_softfp_objects(
                    self.sdk,
                    work,
                    self.work / "missing-f32-artifacts",
                )

    def test_softfp_gate_requires_scalar_f64_in_both_languages(self) -> None:
        fixture_root = self.isolated_abi_fixture(
            "missing-scalar-f64",
            c_replacement=("double_value + mixed.double_value", "0.0"),
            rust_replacement=("double_value + mixed.double_value", "0.0"),
        )
        work = self.work / "missing-f64-work"
        work.mkdir()
        with mock.patch.dict(globals(), {"ROOT": fixture_root}):
            with self.assertRaisesRegex(AssertionError, "scalar f64"):
                build_softfp_objects(
                    self.sdk,
                    work,
                    self.work / "missing-f64-artifacts",
                )

    def test_attribute_gate_rejects_wrong_architecture_and_missing_neon(self) -> None:
        source = self.work / "attribute-negative.c"
        source.write_text(
            "float eos_attribute_probe(float x) { return x + 1.0f; }\n",
            encoding="utf-8",
        )
        gcc = arm_gcc(self.sdk)
        readelf = self.sdk / "arm-gnu" / "bin" / "arm-none-eabi-readelf"
        controls = (
            ("armv6", ("-march=armv6", "-mfpu=vfp"), "ARMv7-A"),
            ("no-neon", ("-march=armv7-a", "-mfpu=vfpv3"), "NEONv1"),
        )
        for name, flags, message in controls:
            with self.subTest(control=name):
                obj = self.work / f"attribute-{name}.o"
                run(
                    [gcc, *flags, "-mfloat-abi=softfp", "-O2", "-c", source, "-o", obj],
                    cwd=self.work,
                )
                with self.assertRaisesRegex(AssertionError, message):
                    arm_attributes(readelf, obj, self.work)

    def test_softfp_gate_rejects_wrong_cortex_tuning(self) -> None:
        work = self.work / "wrong-tune-work"
        work.mkdir()
        wrong_flags = tuple(
            "-mtune=cortex-a8" if flag.startswith("-mtune=") else flag
            for flag in TARGET_FLAGS
        )
        with mock.patch.dict(globals(), {"TARGET_FLAGS": wrong_flags}):
            with self.assertRaisesRegex(AssertionError, "cortex-a9"):
                build_softfp_objects(
                    self.sdk,
                    work,
                    self.work / "wrong-tune-artifacts",
                )


class ReleaseIdentityGateTests(unittest.TestCase):
    def test_sdk_shape_does_not_accept_a_same_name_substitute(self) -> None:
        with tempfile.TemporaryDirectory(prefix="eos-sdk-substitute-") as temporary:
            substitute = Path(temporary)
            for relative in (
                "bin/cargo",
                "bin/rustc",
                "bin/eos-rust-link",
                "bin/eos-elf-validate",
                "bin/eos-auth-package",
                "arm-gnu/bin/arm-none-eabi-gcc",
                "arm-gnu/bin/arm-none-eabi-readelf",
            ):
                path = substitute / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
                path.chmod(0o700)
            with mock.patch.dict(
                os.environ, {"EOS_RUST_SDK_ROOT": str(substitute)}, clear=False
            ):
                with self.assertRaisesRegex(AssertionError, "reviewed Task 16 SDK"):
                    sdk_root()

    def test_arm_gcc_rejects_mutation_outside_the_compiler_binary(self) -> None:
        sdk = Path(os.environ["EOS_RUST_SDK_ROOT"]).resolve(strict=True)
        original_cc = Path(os.environ["EOS_ARM_GNU_CC"]).resolve(strict=True)
        original_root = original_cc.parent.parent
        with tempfile.TemporaryDirectory(
            prefix=".eos-arm-gnu-mutated-", dir=original_root.parent
        ) as temporary:
            mutated_root = Path(temporary) / "arm-gnu"
            shutil.copytree(
                original_root,
                mutated_root,
                symlinks=True,
                copy_function=os.link,
            )
            victim = mutated_root / "bin" / "arm-none-eabi-strings"
            mode = victim.stat().st_mode
            contents = victim.read_bytes()
            victim.unlink()
            victim.write_bytes(contents + b"\0")
            victim.chmod(mode)
            with mock.patch.dict(
                os.environ,
                {
                    "EOS_ARM_GNU_CC": str(
                        mutated_root / "bin" / "arm-none-eabi-gcc"
                    ),
                    "EOS_ARM_GNU_OBJDUMP": str(
                        mutated_root / "bin" / "arm-none-eabi-objdump"
                    ),
                },
                clear=False,
            ):
                with self.assertRaisesRegex(AssertionError, "ARM GNU tree identity"):
                    arm_gcc(sdk)


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

    def test_ci_entrypoint_rejects_mutated_sdk_before_running_tools(self) -> None:
        reviewed = Path(os.environ["EOS_RUST_SDK_ROOT"]).resolve(strict=True)
        with tempfile.TemporaryDirectory(
            prefix=".eos-sdk-substitute-", dir=reviewed.parent
        ) as temporary:
            substitute = Path(temporary) / "sdk"
            shutil.copytree(
                reviewed,
                substitute,
                symlinks=True,
                copy_function=os.link,
            )
            victim = substitute / "share" / "licenses" / "rust" / "LICENSE-MIT"
            mode = victim.stat().st_mode
            contents = victim.read_bytes()
            victim.unlink()
            victim.write_bytes(contents + b"\nsubstituted\n")
            victim.chmod(mode)

            marker = Path(temporary) / "sdk-python-ran"
            python_shim = substitute / "bin" / "python3"
            python_shim.write_text(
                "#!/bin/sh\n: > \"$EOS_PYTHON_SHIM_MARKER\"\nexit 0\n",
                encoding="utf-8",
            )
            python_shim.chmod(0o700)

            control_bin = Path(temporary) / "control-bin"
            control_bin.mkdir()
            for command in ("bash", "dirname", "python3", "readlink"):
                os.symlink(shutil.which(command), control_bin / command)
            environment = os.environ.copy()
            environment["EOS_RUST_SDK_ROOT"] = str(substitute)
            environment["EOS_PYTHON_SHIM_MARKER"] = str(marker)
            environment["PATH"] = str(control_bin)
            result = run(
                [ROOT / "tests" / "eos" / "run-ci.sh"],
                cwd=ROOT,
                env=environment,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists(), "SDK python3 ran before identity verification")
            self.assertIn("reviewed Task 16 SDK tree identity mismatch", result.stderr)

    def test_ci_entrypoint_rejects_mutation_before_cmake_python_can_run(self) -> None:
        reviewed = Path(os.environ["EOS_RUST_SDK_ROOT"]).resolve(strict=True)
        with tempfile.TemporaryDirectory(
            prefix=".eos-sdk-cmake-python-", dir=reviewed.parent
        ) as temporary:
            temporary_root = Path(temporary)
            substitute = temporary_root / "sdk"
            shutil.copytree(
                reviewed,
                substitute,
                symlinks=True,
                copy_function=os.link,
            )
            victim = substitute / "share" / "licenses" / "rust" / "LICENSE-MIT"
            mode = victim.stat().st_mode
            contents = victim.read_bytes()
            victim.unlink()
            victim.write_bytes(contents + b"\nsubstituted\n")
            victim.chmod(mode)

            marker = temporary_root / "cmake-python-ran"
            cmake_bin = temporary_root / "cmake-bin"
            cmake_bin.mkdir()
            python_shim = cmake_bin / "python3"
            python_shim.write_text(
                "#!/bin/sh\n: > \"$EOS_PYTHON_SHIM_MARKER\"\nexit 0\n",
                encoding="utf-8",
            )
            python_shim.chmod(0o700)

            control_bin = temporary_root / "control-bin"
            control_bin.mkdir()
            for command in ("bash", "dirname", "python3", "readlink"):
                os.symlink(shutil.which(command), control_bin / command)
            environment = os.environ.copy()
            environment["EOS_RUST_SDK_ROOT"] = str(substitute)
            environment["EOS_CMAKE_BIN_DIR"] = str(cmake_bin)
            environment["EOS_PYTHON_SHIM_MARKER"] = str(marker)
            environment["PATH"] = str(control_bin)
            result = run(
                [ROOT / "tests" / "eos" / "run-ci.sh"],
                cwd=ROOT,
                env=environment,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(
                marker.exists(), "CMake python3 ran before identity verification"
            )
            self.assertIn("reviewed Task 16 SDK tree identity mismatch", result.stderr)

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
    if sys.argv[1:] == ["--verify-release-identity"]:
        try:
            verified_sdk = sdk_root()
            verified_arm = verify_release_identity(
                verified_sdk,
                configured_tool("EOS_ARM_GNU_CC", "ARM GNU 14.3.1 compiler"),
                configured_tool("EOS_ARM_GNU_OBJDUMP", "binutils 2.44 objdump"),
            )
        except AssertionError as error:
            print(f"EOS release identity verification failed: {error}", file=sys.stderr)
            raise SystemExit(2) from error
        print(
            f"verified Task 16 SDK {REVIEWED_SDK_TREE_SHA256} and "
            f"ARM GNU {REVIEWED_ARM_GNU_TREE_SHA256} at {verified_arm}"
        )
    else:
        unittest.main(verbosity=2)
