import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
LIBC = ROOT / "src" / "tools" / "eos-libc"
EOS_MODULE = LIBC / "src" / "eos" / "mod.rs"
ABI_HEADER = ROOT / "src" / "tools" / "eos-abi" / "include" / "eos_rust_abi.h"
EXPECTED_EXPORTS = (
    ROOT / "src" / "tools" / "eos-abi" / "tests" / "expected-exports-v1.txt"
)


def run(command, *, cwd=ROOT):
    return subprocess.run(
        [str(part) for part in command],
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def rustc():
    configured = os.environ.get("EOS_RUST_RUSTC")
    if configured:
        return Path(configured)
    found = shutil.which("rustc")
    if found:
        return Path(found)
    candidates = sorted((ROOT / "build").glob("*/stage1/bin/rustc"))
    if candidates:
        return candidates[0]
    raise unittest.SkipTest("no Rust compiler is available")


def compile_host_libc(output_dir):
    output_dir = Path(output_dir)
    wrapper = output_dir / "host_libc.rs"
    macros = (LIBC / "src" / "macros.rs").as_posix()
    eos = EOS_MODULE.as_posix()
    wrapper.write_text(
        f"""#![no_std]
#[macro_use]
#[path = \"{macros}\"]
mod macros;

pub use core::ffi::{{
    c_char, c_double, c_float, c_int, c_long, c_longlong, c_schar, c_short,
    c_uchar, c_uint, c_ulong, c_ulonglong, c_ushort, c_void,
}};

pub mod prelude {{
    pub use crate::{{
        c_char, c_double, c_float, c_int, c_long, c_longlong, c_schar, c_short,
        c_uchar, c_uint, c_ulong, c_ulonglong, c_ushort, c_void,
    }};
}}

#[path = \"{eos}\"]
mod eos;
pub use eos::*;
""",
        encoding="utf-8",
    )
    run(
        [
            rustc(),
            wrapper,
            "--crate-name",
            "libc",
            "--crate-type",
            "rlib",
            "--edition=2021",
            "--out-dir",
            output_dir,
        ]
    )
    return output_dir / "liblibc.rlib"


class LibcLinkTests(unittest.TestCase):
    def test_all_public_functions_use_the_stable_eos_exports(self):
        source = EOS_MODULE.read_text(encoding="utf-8")
        declarations = re.findall(
            r'#\[link_name\s*=\s*"([^"]+)"\]\s*pub\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(',
            source,
            flags=re.MULTILINE,
        )
        declared_links = [link for link, _rust_name in declarations]
        public_functions = re.findall(
            r"\bpub\s+(?:unsafe\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", source
        )
        self.assertEqual(
            len(declarations),
            len(public_functions),
            "every public function must be an extern declaration with an explicit link_name",
        )
        self.assertTrue(
            all(name.startswith("eos_rust_") for name in declared_links),
            "all extern link names must use the stable eos_rust_ namespace",
        )
        self.assertEqual(len(declared_links), len(set(declared_links)), "duplicate link name")

        header_exports = set(
            re.findall(r"\b(eos_rust_[A-Za-z0-9_]+)\s*\(", ABI_HEADER.read_text())
        )
        expected_exports = {
            line.strip()
            for line in EXPECTED_EXPORTS.read_text(encoding="utf-8").splitlines()
            if line.strip()
        }
        self.assertEqual(118, len(header_exports))
        self.assertEqual(expected_exports, header_exports)
        self.assertEqual(header_exports, set(declared_links))

    def test_eos_libc_compiles_as_a_standalone_crate(self):
        with tempfile.TemporaryDirectory(prefix="eos-libc-compile-") as temp:
            archive = compile_host_libc(temp)
            self.assertGreater(archive.stat().st_size, 0)

    def test_open_compiles_to_the_stable_shim_symbol(self):
        with tempfile.TemporaryDirectory(prefix="eos-libc-symbol-") as temp:
            temp = Path(temp)
            archive = compile_host_libc(temp)
            probe = temp / "probe.rs"
            probe.write_text(
                """#![no_std]
extern crate libc;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn eos_libc_open_probe(path: *const libc::c_char) -> libc::c_int {
    unsafe { libc::open(path, libc::O_RDONLY, 0) }
}
""",
                encoding="utf-8",
            )
            obj = temp / "probe.o"
            run(
                [
                    rustc(),
                    probe,
                    "--crate-name",
                    "eos_libc_open_probe",
                    "--crate-type",
                    "lib",
                    "--edition=2021",
                    "--extern",
                    f"libc={archive}",
                    "--emit=obj",
                    "-o",
                    obj,
                ]
            )
            symbols = run(["readelf", "-Ws", obj]).stdout
            self.assertRegex(symbols, r"\bUND\s+eos_rust_open\b")
            self.assertNotRegex(symbols, r"\bUND\s+open\b")

    def test_open_links_against_only_the_stable_shim(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("no host C compiler is available")
        with tempfile.TemporaryDirectory(prefix="eos-libc-link-") as temp:
            temp = Path(temp)
            archive = compile_host_libc(temp)
            probe = temp / "probe.rs"
            probe.write_text(
                """#![no_std]
extern crate libc;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn eos_libc_open_probe(path: *const libc::c_char) -> libc::c_int {
    unsafe { libc::open(path, libc::O_RDONLY, 0) }
}
""",
                encoding="utf-8",
            )
            probe_obj = temp / "probe.o"
            run(
                [
                    rustc(),
                    probe,
                    "--crate-name",
                    "eos_libc_open_probe",
                    "--crate-type",
                    "lib",
                    "--edition=2021",
                    "--extern",
                    f"libc={archive}",
                    "--emit=obj",
                    "-o",
                    probe_obj,
                ]
            )
            stub = temp / "stub.c"
            stub.write_text(
                "int eos_rust_open(const char *p, unsigned f, unsigned m) { return p != 0; }\n",
                encoding="utf-8",
            )
            stub_obj = temp / "stub.o"
            run([cc, "-fPIC", "-c", stub, "-o", stub_obj])
            run(
                [
                    cc,
                    "-shared",
                    "-nostdlib",
                    "-Wl,--no-undefined",
                    probe_obj,
                    stub_obj,
                    "-o",
                    temp / "probe.so",
                ]
            )

    def test_open_links_and_runs_against_only_the_stable_shim(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("no host C compiler is available")
        with tempfile.TemporaryDirectory(prefix="eos-libc-runtime-") as temp:
            temp = Path(temp)
            archive = compile_host_libc(temp)
            stub = temp / "stub.c"
            stub.write_text(
                """int eos_rust_open(const char *path, unsigned flags, unsigned mode) {
    return path != 0 && flags == 0 && mode == 0 ? 73 : -1;
}
""",
                encoding="utf-8",
            )
            stub_obj = temp / "stub.o"
            run([cc, "-c", stub, "-o", stub_obj])
            probe = temp / "runtime.rs"
            probe.write_text(
                """extern crate libc;

fn main() {
    let result = unsafe { libc::open(c"probe".as_ptr(), libc::O_RDONLY, 0) };
    assert_eq!(result, 73);
}
""",
                encoding="utf-8",
            )
            executable = temp / "runtime"
            run(
                [
                    rustc(),
                    probe,
                    "--edition=2021",
                    "--extern",
                    f"libc={archive}",
                    "-C",
                    f"link-arg={stub_obj}",
                    "-o",
                    executable,
                ]
            )
            run([executable])


if __name__ == "__main__":
    unittest.main()
