#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check the public global definitions in libeos_rust_abi.a"
    )
    parser.add_argument("--nm", required=True, help="nm-compatible executable")
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=Path)
    return parser.parse_args()


def read_expected(path: Path) -> set[str]:
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def read_actual(nm: str, archive: Path) -> set[str]:
    result = subprocess.run(
        [nm, "-g", "--defined-only", str(archive)],
        check=True,
        capture_output=True,
        text=True,
    )
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and len(fields[-2]) == 1:
            symbols.add(fields[-1])
    return symbols


def main() -> int:
    arguments = parse_arguments()
    expected = read_expected(arguments.expected)
    actual = read_actual(arguments.nm, arguments.archive)
    if actual == expected:
        return 0

    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        print("missing exports:", ", ".join(missing))
    if unexpected:
        print("unexpected exports:", ", ".join(unexpected))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
