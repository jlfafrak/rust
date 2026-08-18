#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
TARGET = "armv7a-unknown-eos-eabi"


def run(command, *, cwd=ROOT):
    result = subprocess.run(
        [str(part) for part in command],
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(map(str, command))}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def find_rustc_and_sysroot():
    configured = os.environ.get("EOS_RUST_RUSTC")
    rustcs = [Path(configured)] if configured else sorted((ROOT / "build").glob("*/stage1/bin/rustc"))
    for compiler in rustcs:
        sysroot = compiler.parents[1]
        core = list((sysroot / "lib" / "rustlib" / TARGET / "lib").glob("libcore-*.rlib"))
        if core:
            return compiler, sysroot
    raise RuntimeError(
        "EOS core is not built; run ./x check library/std --target armv7a-unknown-eos-eabi first"
    )


def layout_symbols(readelf_output):
    symbols = {}
    pattern = re.compile(
        r"^\s*\d+:\s+\S+\s+(\d+)\s+\S+\s+\S+\s+\S+\s+\S+\s+"
        r"(eos_layout_[A-Za-z0-9_]+)\s*$"
    )
    for line in readelf_output.splitlines():
        match = pattern.match(line)
        if match:
            symbols[match.group(2)] = int(match.group(1)) - 1
    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    clang = shutil.which("clang")
    readelf = shutil.which("readelf")
    objdump = shutil.which("objdump")
    if not clang or not readelf or not objdump:
        raise RuntimeError("clang, readelf, and objdump are required")
    rustc, sysroot = find_rustc_and_sysroot()

    c_object = output / "c_layout.o"
    run(
        [
            clang,
            "--target=armv7a-none-eabi",
            "-march=armv7-a",
            "-mtune=cortex-a9",
            "-mfpu=neon-vfpv3",
            "-mfloat-abi=softfp",
            "-fPIC",
            "-std=c11",
            "-I",
            ROOT / "src" / "tools" / "eos-abi" / "include",
            "-c",
            ROOT / "tests" / "eos" / "abi" / "c_layout.c",
            "-o",
            c_object,
        ]
    )

    libc_dir = output / "libc"
    libc_dir.mkdir(exist_ok=True)
    run(
        [
            rustc,
            ROOT / "src" / "tools" / "eos-libc" / "src" / "lib.rs",
            "--crate-name",
            "libc",
            "--crate-type",
            "rlib",
            "--edition=2021",
            "--target",
            TARGET,
            "--sysroot",
            sysroot,
            "--out-dir",
            libc_dir,
        ]
    )
    rust_object = output / "rust_layout.o"
    run(
        [
            rustc,
            ROOT / "tests" / "eos" / "abi" / "rust_layout.rs",
            "--crate-name",
            "eos_rust_layout",
            "--crate-type",
            "lib",
            "--edition=2021",
            "--target",
            TARGET,
            "--sysroot",
            sysroot,
            "--extern",
            f"libc={libc_dir / 'liblibc.rlib'}",
            "--emit=obj",
            "-o",
            rust_object,
        ]
    )

    outputs = {}
    for name, obj in (("c", c_object), ("rust", rust_object)):
        readelf_text = run([readelf, "-Ws", obj])
        objdump_text = run([objdump, "-s", obj])
        if "Contents of section" not in objdump_text:
            raise RuntimeError(f"objdump found no initialized layout sections in {obj}")
        (output / f"{name}_layout.readelf.txt").write_text(readelf_text, encoding="utf-8")
        (output / f"{name}_layout.objdump.txt").write_text(objdump_text, encoding="utf-8")
        outputs[name] = layout_symbols(readelf_text)

    if not outputs["c"] or not outputs["rust"]:
        raise RuntimeError("layout symbols were not emitted")
    missing_c = sorted(outputs["rust"].keys() - outputs["c"].keys())
    missing_rust = sorted(outputs["c"].keys() - outputs["rust"].keys())
    mismatches = [
        (name, outputs["c"][name], outputs["rust"][name])
        for name in sorted(outputs["c"].keys() & outputs["rust"].keys())
        if outputs["c"][name] != outputs["rust"][name]
    ]
    if missing_c or missing_rust or mismatches:
        lines = ["EOS C/Rust layout mismatch"]
        if missing_c:
            lines.append(f"missing from C: {', '.join(missing_c)}")
        if missing_rust:
            lines.append(f"missing from Rust: {', '.join(missing_rust)}")
        lines.extend(f"{name}: C={c_value}, Rust={rust_value}" for name, c_value, rust_value in mismatches)
        raise RuntimeError("\n".join(lines))
    print(f"matched {len(outputs['c'])} ARM layout facts")


if __name__ == "__main__":
    main()
