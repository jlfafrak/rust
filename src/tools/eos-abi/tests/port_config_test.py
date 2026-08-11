#!/usr/bin/env python3

import argparse
import subprocess
import tempfile
from pathlib import Path
from typing import Optional


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check EOS ABI CMake port policy")
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source", required=True, type=Path)
    return parser.parse_args()


def run_configure(
    cmake: str, source: Path, build: Path, sdk: Optional[Path] = None,
    testing: Optional[str] = None,
    port: str = "martos_14_0_39",
) -> subprocess.CompletedProcess[str]:
    command = [
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        f"-DEOS_RUST_PORT={port}",
    ]
    if sdk is not None:
        command.append(f"-DEOS_SDK_ROOT={sdk}")
    if testing is not None:
        command.append(f"-DBUILD_TESTING={testing}")
    return subprocess.run(command, capture_output=True, text=True)


def cache_value(cache: Path, name: str) -> Optional[str]:
    prefix = f"{name}:BOOL="
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix)
    return None


def main() -> int:
    arguments = parse_arguments()
    source = arguments.source.resolve()
    with tempfile.TemporaryDirectory(prefix="eos-abi-port-config-") as directory:
        root = Path(directory)
        sdk = root / "sdk"
        (sdk / "martos" / "inc").mkdir(parents=True)
        (sdk / "libc" / "include").mkdir(parents=True)
        (sdk / "martos" / "inc" / "martos_smp.h").touch()
        (sdk / "libc" / "include" / "errno.h").touch()

        host_result = run_configure(
            arguments.cmake, source, root / "host", port="host"
        )
        if host_result.returncode != 0:
            print("host default configuration failed")
            print(host_result.stdout)
            print(host_result.stderr)
            return 1
        host_cache = root / "host" / "CMakeCache.txt"
        if cache_value(host_cache, "BUILD_TESTING") != "ON":
            print("host configuration must default BUILD_TESTING to ON")
            return 1

        default_result = run_configure(
            arguments.cmake, source, root / "default", sdk
        )
        if default_result.returncode != 0:
            print("MARTOS default configuration failed")
            print(default_result.stdout)
            print(default_result.stderr)
            return 1
        default_cache = root / "default" / "CMakeCache.txt"
        if cache_value(default_cache, "BUILD_TESTING") != "OFF":
            print("MARTOS configuration must default BUILD_TESTING to OFF")
            return 1

        invalid_result = run_configure(
            arguments.cmake, source, root / "invalid", sdk, testing="ON"
        )
        invalid_output = invalid_result.stdout + invalid_result.stderr
        if invalid_result.returncode == 0:
            print("MARTOS configuration unexpectedly accepted BUILD_TESTING=ON")
            return 1
        if "BUILD_TESTING=ON is not supported" not in invalid_output:
            print("MARTOS test rejection did not explain the invalid configuration")
            print(invalid_output)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
