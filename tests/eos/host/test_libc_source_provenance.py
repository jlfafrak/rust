"""Offline provenance checks for the vendored rust-lang/libc source tree."""

import hashlib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
VENDOR = ROOT / "src" / "tools" / "eos-libc"
MANIFEST = ROOT / "tests" / "eos" / "fixtures" / "libc-0.2.185-71d5bfcc.sha256"

EXPECTED_METADATA = {
    "source": "https://github.com/rust-lang/libc.git",
    "commit": "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8",
    "tree": "68d565ea31258a8056ada681c1e0ec90dcb23988",
    "files": "508",
}

# These are the only tracked upstream files Task 11 intentionally changes.
EXPECTED_EOS_HASHES = {
    "build.rs": "f47a9b616c619130b6be76652b6f398b0b15ee99458eee9e2905a58210d9ddad",
    "src/lib.rs": "9e00f607791a159309dba5f3772c452bcff6068beb965aa77a32b730f0d95b44",
    "src/new/mod.rs": "d4018f14c7b99604e0c31c69db8808de6d41d83d373c0a49be8d0b4f62736f85",
    "src/eos/mod.rs": "3af9d6c8d819d96701099f34b919e5269de29c28d16b1ce64631ac8f71f2f550",
}
MODIFIED_UPSTREAM_PATHS = {"build.rs", "src/lib.rs", "src/new/mod.rs"}
ADDED_EOS_PATHS = {"src/eos/mod.rs"}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_manifest():
    metadata = {}
    files = {}
    for line_number, raw in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        if raw.startswith("# "):
            key, value = raw[2:].split("=", 1)
            metadata[key] = value
            continue
        if not raw:
            continue
        digest, relative = raw.split("  ", 1)
        if relative in files:
            raise AssertionError(f"duplicate manifest path on line {line_number}: {relative}")
        files[relative] = digest
    return metadata, files


def summarize(paths):
    ordered = sorted(paths)
    suffix = "" if len(ordered) <= 12 else f" ... ({len(ordered)} total)"
    return ", ".join(ordered[:12]) + suffix


class LibcSourceProvenanceTests(unittest.TestCase):
    def test_vendor_is_exact_tracked_tree_plus_eos_allowlist(self):
        metadata, upstream = read_manifest()
        self.assertEqual(EXPECTED_METADATA, metadata)
        self.assertEqual(508, len(upstream))
        self.assertEqual(MODIFIED_UPSTREAM_PATHS, MODIFIED_UPSTREAM_PATHS & upstream.keys())
        self.assertFalse((VENDOR / ".git").exists(), "vendored source must not contain .git")

        symlinks = {
            path.relative_to(VENDOR).as_posix()
            for path in VENDOR.rglob("*")
            if path.is_symlink()
        }
        self.assertFalse(symlinks, f"vendored source contains symlinks: {summarize(symlinks)}")
        actual = {
            path.relative_to(VENDOR).as_posix()
            for path in VENDOR.rglob("*")
            if path.is_file()
        }
        expected = set(upstream) | ADDED_EOS_PATHS
        missing = expected - actual
        unexpected = actual - expected
        self.assertFalse(missing, f"tracked libc paths missing: {summarize(missing)}")
        self.assertFalse(unexpected, f"non-allowlisted vendor paths: {summarize(unexpected)}")

        for relative, expected_digest in upstream.items():
            if relative in MODIFIED_UPSTREAM_PATHS:
                expected_digest = EXPECTED_EOS_HASHES[relative]
            self.assertEqual(
                expected_digest,
                sha256(VENDOR / relative),
                f"unexpected vendored content: {relative}",
            )
        for relative in ADDED_EOS_PATHS:
            self.assertEqual(
                EXPECTED_EOS_HASHES[relative],
                sha256(VENDOR / relative),
                f"unexpected EOS addition content: {relative}",
            )


if __name__ == "__main__":
    unittest.main()
