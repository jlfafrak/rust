#!/usr/bin/env python3

import argparse
import subprocess
import tempfile
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Test duplicate export detection")
    parser.add_argument("--python", required=True)
    parser.add_argument("--checker", required=True, type=Path)
    return parser.parse_args()


def run_checker(
    python: str, checker: Path, expected_text: str, nm_output: str
) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="eos-exports-check-") as directory:
        root = Path(directory)
        expected = root / "expected.txt"
        expected.write_text(expected_text, encoding="utf-8")
        archive = root / "libtest.a"
        archive.touch()
        fake_nm = root / "fake-nm"
        fake_nm.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            f"sys.stdout.write({nm_output!r})\n",
            encoding="utf-8",
        )
        fake_nm.chmod(fake_nm.stat().st_mode | 0o111)
        return subprocess.run(
            [
                python,
                str(checker),
                "--nm",
                str(fake_nm),
                "--archive",
                str(archive),
                "--expected",
                str(expected),
            ],
            capture_output=True,
            text=True,
        )


def main() -> int:
    arguments = parse_arguments()
    duplicate_expected = run_checker(
        arguments.python,
        arguments.checker,
        "eos_rust_abi_version\neos_rust_abi_version\n",
        "00000000 T eos_rust_abi_version\n",
    )
    if duplicate_expected.returncode == 0:
        print("duplicate expected exports were accepted")
        return 1
    if "duplicate expected exports" not in duplicate_expected.stdout:
        print("duplicate expected export failure was not diagnosed")
        print(duplicate_expected.stdout, duplicate_expected.stderr)
        return 1

    duplicate_actual = run_checker(
        arguments.python,
        arguments.checker,
        "eos_rust_abi_version\n",
        "00000000 T eos_rust_abi_version\n00000004 T eos_rust_abi_version\n",
    )
    if duplicate_actual.returncode == 0:
        print("duplicate actual exports were accepted")
        return 1
    if "duplicate actual exports" not in duplicate_actual.stdout:
        print("duplicate actual export failure was not diagnosed")
        print(duplicate_actual.stdout, duplicate_actual.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
