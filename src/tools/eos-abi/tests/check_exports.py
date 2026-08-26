#!/usr/bin/env python3

import argparse
import subprocess
from collections import Counter
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check the public global definitions in libeos_rust_abi.a"
    )
    parser.add_argument("--nm", required=True, help="nm-compatible executable")
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=Path)
    return parser.parse_args()


def read_expected(path: Path) -> list[str]:
    return [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]


def read_actual(nm: str, archive: Path) -> list[str]:
    result = subprocess.run(
        [nm, "-g", "--defined-only", str(archive)],
        check=True,
        capture_output=True,
        text=True,
    )
    symbols: list[str] = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and len(fields[-2]) == 1:
            symbols.append(fields[-1])
    return symbols


def duplicates(symbols: list[str]) -> list[str]:
    return sorted(symbol for symbol, count in Counter(symbols).items() if count > 1)


def main() -> int:
    arguments = parse_arguments()
    expected = read_expected(arguments.expected)
    actual = read_actual(arguments.nm, arguments.archive)
    duplicate_expected = duplicates(expected)
    duplicate_actual = duplicates(actual)
    if duplicate_expected:
        print("duplicate expected exports:", ", ".join(duplicate_expected))
    if duplicate_actual:
        print("duplicate actual exports:", ", ".join(duplicate_actual))
    if duplicate_expected or duplicate_actual:
        return 1

    expected_set = set(expected)
    actual_set = set(actual)
    if actual_set == expected_set:
        return 0

    missing = sorted(expected_set - actual_set)
    unexpected = sorted(actual_set - expected_set)
    if missing:
        print("missing exports:", ", ".join(missing))
    if unexpected:
        print("unexpected exports:", ", ".join(unexpected))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
