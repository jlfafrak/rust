#!/usr/bin/env python3

import os
import re
import signal
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
STD = ROOT / "library" / "std" / "src"
UNWIND = ROOT / "library" / "unwind" / "src" / "lib.rs"
PERSONALITY = STD / "sys" / "personality" / "mod.rs"
LIFECYCLE = STD / "thread" / "lifecycle.rs"
THREAD = STD / "sys" / "thread" / "unix.rs"
THREAD_LOCAL = STD / "sys" / "thread_local" / "mod.rs"
NATIVE_THREAD = ROOT / "src" / "tools" / "eos-abi" / "src" / "eos_thread.c"
NATIVE_TLS = ROOT / "src" / "tools" / "eos-abi" / "src" / "eos_tls.c"
TARGET = (
    ROOT
    / "compiler"
    / "rustc_target"
    / "src"
    / "spec"
    / "targets"
    / "armv7a_unknown_eos_eabi.rs"
)
EOS_LIBC = ROOT / "src" / "tools" / "eos-libc" / "src" / "eos" / "mod.rs"
UNWIND_APP = ROOT / "tests" / "eos" / "apps" / "unwind" / "src" / "main.rs"
FFI_LIB = (
    ROOT
    / "tests"
    / "eos"
    / "apps"
    / "ffi-containment"
    / "src"
    / "lib.rs"
)
FFI_CALLER = ROOT / "tests" / "eos" / "abi" / "ffi_caller.c"

# Release one does not approve any EOS-owned production C-unwind declaration.
C_UNWIND_ALLOWLIST = frozenset()
C_UNWIND_DECLARATION = re.compile(r'\bextern\s+"C-unwind"|\babi\s*=\s*"C-unwind"')


def run(command, *, env=None):
    return subprocess.run(
        [str(part) for part in command],
        cwd=ROOT,
        env=env,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def rustc():
    compiler = shutil.which("rustc")
    if compiler is None:
        raise unittest.SkipTest("no host Rust compiler is available")
    return Path(compiler)


def eos_owned_production_rust_sources():
    paths = {TARGET, EOS_LIBC, UNWIND}
    for path in STD.rglob("*.rs"):
        source = path.read_text(encoding="utf-8")
        if path.name == "eos.rs" or 'target_os = "eos"' in source:
            paths.add(path)
    return sorted(paths)


class FfiUnwindPolicyTests(unittest.TestCase):
    def test_eos_owned_production_has_no_c_unwind_declaration(self):
        offenders = set()
        for path in eos_owned_production_rust_sources():
            source = path.read_text(encoding="utf-8")
            if C_UNWIND_DECLARATION.search(source):
                offenders.add(path.relative_to(ROOT).as_posix())
        self.assertEqual(C_UNWIND_ALLOWLIST, offenders)

    def test_eos_links_the_arm_gnu_unwinder_contract(self):
        source = UNWIND.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            re.compile(
                r'#\[cfg\(target_os\s*=\s*"eos"\)\]\s*'
                r'#\[link\(name\s*=\s*"gcc"\)\]\s*'
                r'unsafe\s+extern\s+"C"\s*\{\s*\}',
                re.DOTALL,
            ),
        )

    def test_eos_keeps_the_unix_gcc_personality(self):
        source = PERSONALITY.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            re.compile(
                r'all\(\s*target_family\s*=\s*"unix"[^)]*\)\s*,?\s*'
                r'.*?\)\s*=>\s*\{\s*mod\s+gcc\s*;',
                re.DOTALL,
            ),
        )

    def test_thread_roots_and_cleanup_have_containment_guards(self):
        lifecycle = LIFECYCLE.read_text(encoding="utf-8")
        thread = THREAD.read_text(encoding="utf-8")
        thread_local = THREAD_LOCAL.read_text(encoding="utf-8")
        native_thread = NATIVE_THREAD.read_text(encoding="utf-8")
        native_tls = NATIVE_TLS.read_text(encoding="utf-8")

        self.assertIn("panic::catch_unwind", lifecycle)
        self.assertRegex(thread, r'extern\s+"C"\s+fn\s+thread_start')
        self.assertIn("record->start_routine(record->argument)", native_thread)
        self.assertIn("eos_tls_cleanup_current();", native_thread)
        self.assertIn("destructor(argument);", native_tls)
        self.assertIn("fn abort_on_dtor_unwind", thread_local)

    def test_unwind_probe_catches_panic_and_cleanup_panic_aborts(self):
        with tempfile.TemporaryDirectory(prefix="eos-unwind-host-") as temp:
            output = Path(temp) / "unwind-probe"
            env = os.environ.copy()
            env["RUSTC_BOOTSTRAP"] = "1"
            run(
                [
                    rustc(),
                    UNWIND_APP,
                    "--edition=2024",
                    "-Dffi-unwind-calls",
                    "-Cpanic=unwind",
                    "-o",
                    output,
                ],
                env=env,
            )
            run([output])
            cleanup = subprocess.run(
                [str(output), "--cleanup-panic"],
                cwd=ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertEqual(
                cleanup.returncode,
                -signal.SIGABRT,
                "a panic from Rust TLS cleanup must abort the process",
            )

    def test_c_and_cpp_callers_observe_contained_panic(self):
        cc = shutil.which("cc")
        cxx = shutil.which("c++") or shutil.which("g++")
        nm = shutil.which("nm")
        if cc is None or cxx is None or nm is None:
            self.skipTest("host C, C++, and nm tools are required")

        with tempfile.TemporaryDirectory(prefix="eos-ffi-host-") as temp:
            temp = Path(temp)
            library = temp / "libffi_containment.so"
            env = os.environ.copy()
            env["RUSTC_BOOTSTRAP"] = "1"
            run(
                [
                    rustc(),
                    FFI_LIB,
                    "--crate-name=ffi_containment",
                    "--crate-type=cdylib",
                    "--edition=2024",
                    "-Dffi-unwind-calls",
                    "-Cpanic=unwind",
                    "-o",
                    library,
                ],
                env=env,
            )
            symbols = run([nm, "-D", "--defined-only", library]).stdout
            self.assertRegex(symbols, r"\beos_ffi_containment_probe\b")

            for compiler, language in ((cc, "c"), (cxx, "c++")):
                with self.subTest(language=language):
                    executable = temp / f"ffi-caller-{language.replace('+', 'p')}"
                    run(
                        [
                            compiler,
                            "-x",
                            language,
                            FFI_CALLER,
                            "-L",
                            temp,
                            "-lffi_containment",
                            f"-Wl,-rpath,{temp}",
                            "-o",
                            executable,
                        ]
                    )
                    run([executable])


if __name__ == "__main__":
    unittest.main()
