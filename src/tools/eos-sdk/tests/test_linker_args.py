import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from support import LINK_WRAPPER, SDK_SOURCE_ROOT, run_python, write_executable


class LinkerWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "sdk"
        self.work = self.base / "work"
        self.work.mkdir()
        self.capture = self.base / "compiler-argv.json"
        self.environment_capture = self.base / "compiler-environment.json"
        self.version_control = self.base / "compiler-version"
        self.libgcc_control = self.base / "compiler-libgcc"
        self.status_control = self.base / "compiler-status"
        self.version_control.write_text("14.3.1", encoding="utf-8")
        self.status_control.write_text("0", encoding="utf-8")
        compiler = self.root / "arm-gnu" / "bin" / "arm-none-eabi-gcc"
        write_executable(
            compiler,
            f"""#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

if sys.argv[1:] == ["-dumpfullversion"]:
    print(Path({str(self.version_control)!r}).read_text(encoding="utf-8"))
    raise SystemExit(0)
if "-print-libgcc-file-name" in sys.argv:
    print(Path({str(self.libgcc_control)!r}).read_text(encoding="utf-8"))
    raise SystemExit(0)
Path({str(self.capture)!r}).write_text(json.dumps(sys.argv[1:]))
Path({str(self.environment_capture)!r}).write_text(json.dumps(dict(os.environ)))
raise SystemExit(int(Path({str(self.status_control)!r}).read_text(encoding="utf-8")))
""",
        )
        self.libgcc = (
            self.root
            / "arm-gnu"
            / "lib"
            / "gcc"
            / "arm-none-eabi"
            / "14.3.1"
            / "libgcc.a"
        )
        self.libgcc.parent.mkdir(parents=True)
        self.libgcc.touch()
        self.libgcc_control.write_text(str(self.libgcc), encoding="utf-8")
        linker_script = self.root / "linker" / "app_linker_script.ld"
        linker_script.parent.mkdir(parents=True)
        linker_script.write_text("ENTRY(main)\n", encoding="utf-8")
        abi = self.root / "lib" / "libeos_rust_abi.a"
        abi.parent.mkdir(parents=True)
        abi.touch()
        martos = self.root / "eos" / "martos" / "lib"
        martos.mkdir(parents=True)
        for name in (
            "libmartos_app.so",
            "libmartos_app.so.1.0",
            "libmartos_c++.a",
            "libmartos_c++abi.a",
            "libmartos_c.a",
        ):
            (martos / name).touch()
        self.env = {
            "EOS_RUST_SDK_ROOT": str(self.root),
        }

    def invoke(self, *args: str, env: dict[str, str] | None = None):
        invocation_env = dict(self.env)
        if env:
            invocation_env.update(env)
        return run_python(LINK_WRAPPER, list(args), cwd=self.work, env=invocation_env)

    def rustc_std_shared_args(self) -> list[str]:
        output = self.work / "stage1-std" / "deps" / "libstd-0e630290a4d2acf4.so"
        output.parent.mkdir(parents=True, exist_ok=True)
        version_script = output.parent / "rustckDNTll" / "list"
        version_script.parent.mkdir(exist_ok=True)
        version_script.write_text("RUST_1.0 { global: *; };\n", encoding="utf-8")
        return [
            f"-Wl,--version-script={version_script}",
            "-Wl,--no-undefined-version",
            str(output.parent / "symbols.o"),
            str(output.parent / "std.rcgu.o"),
            str(output.parent / "rmeta.o"),
            "-Wl,--as-needed",
            "-Wl,-Bstatic",
            str(output.parent / "libpanic_unwind.rlib"),
            str(output.parent / "libunwind.rlib"),
            "-Wl,-Bdynamic",
            "-lgcc",
            "-L",
            str(output.parent / "raw-dylibs"),
            "-Wl,--eh-frame-hdr",
            "-Wl,-z,noexecstack",
            "-o",
            str(output),
            "-shared",
            f"-Wl,-soname={output.name}",
            "-Wl,-O1",
            "-nodefaultlibs",
        ]

    def test_preserves_rustc_inputs_and_orders_main_before_runtime_libraries(self):
        output = self.work / "application.elf"
        rust_search = self.work / "deps"
        rust_search.mkdir()
        result = self.invoke(
            "-o",
            str(output),
            "-L",
            str(rust_search),
            "-Wl,--export-dynamic-symbol=main",
            "rust-main.o",
            "libstd.rlib",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        argv = json.loads(self.capture.read_text(encoding="utf-8"))
        required_prefix = [
            "-march=armv7-a",
            "-mtune=cortex-a9",
            "-mfpu=neon-vfpv3",
            "-mfloat-abi=softfp",
            "-fPIC",
            "-pie",
            "-nostdlib",
            "-Wl,--gc-sections",
            "-Wl,-z,notext",
            "-Wl,-z,defs",
            "-Wl,--dynamic-linker=/usr/lib/ld.so.1",
            f"-Wl,-T,{self.root / 'linker' / 'app_linker_script.ld'}",
        ]
        self.assertEqual(argv[: len(required_prefix)], required_prefix)
        for preserved in (
            "-o",
            str(output),
            "-L",
            str(rust_search),
            "-Wl,--export-dynamic-symbol=main",
            "rust-main.o",
            "libstd.rlib",
        ):
            self.assertIn(preserved, argv)
        abi = str(self.root / "lib" / "libeos_rust_abi.a")
        martos = self.root / "eos" / "martos" / "lib"
        self.assertLess(argv.index("rust-main.o"), argv.index(abi))
        self.assertLess(argv.index("libstd.rlib"), argv.index(abi))
        self.assertEqual(
            argv[argv.index(abi) :],
            [
                abi,
                "-Wl,-Bdynamic",
                str(martos / "libmartos_app.so"),
                str(martos / "libmartos_c++.a"),
                "-Wl,-u,__cxa_begin_cleanup",
                "-Wl,-u,__cxa_call_unexpected",
                str(martos / "libmartos_c++abi.a"),
                str(martos / "libmartos_c.a"),
                str(self.libgcc),
            ],
        )
        self.assertFalse(any(argument.startswith("-lmartos") for argument in argv))
        self.assertFalse(any(Path(arg).name.startswith("crt") for arg in argv))

    def test_forces_only_ehabi_symbols_that_the_pinned_martos_cxxabi_defines(self):
        result = self.invoke(
            "-o", str(self.work / "application.elf"), "rust-main.o"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        argv = json.loads(self.capture.read_text(encoding="utf-8"))
        cxxabi = argv.index(
            str(self.root / "eos" / "martos" / "lib" / "libmartos_c++abi.a")
        )
        self.assertEqual(
            argv[cxxabi - 2 : cxxabi],
            ["-Wl,-u,__cxa_begin_cleanup", "-Wl,-u,__cxa_call_unexpected"],
        )
        self.assertNotIn("-Wl,-u,__cxa_type_match", argv)
        self.assertNotIn("-Wl,-u,__gnu_Unwind_Find_exidx", argv)

    def test_preserves_exact_rustc_std_shared_shape_without_application_policy(self):
        rustc_arguments = self.rustc_std_shared_args()
        (self.root / "linker" / "app_linker_script.ld").unlink()
        (self.root / "lib" / "libeos_rust_abi.a").unlink()
        shutil.rmtree(self.root / "eos")
        result = self.invoke(*rustc_arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        argv = json.loads(self.capture.read_text(encoding="utf-8"))
        self.assertEqual(
            argv,
            [
                "-march=armv7-a",
                "-mtune=cortex-a9",
                "-mfpu=neon-vfpv3",
                "-mfloat-abi=softfp",
                "-fPIC",
                "-nostdlib",
                *rustc_arguments,
                str(self.libgcc),
            ],
        )
        forbidden_application_items = {
            "-pie",
            "-Wl,--dynamic-linker=/usr/lib/ld.so.1",
            str(self.root / "lib" / "libeos_rust_abi.a"),
            "-lmartos_app",
            "-lmartos_c++",
            "-lmartos_c++abi",
            "-lmartos_c",
        }
        self.assertTrue(forbidden_application_items.isdisjoint(argv))
        self.assertFalse(any(argument.startswith("-Wl,-T,") for argument in argv))

    def test_rejects_arbitrary_or_ambiguous_shared_outputs(self):
        cases = {
            "application DSO": [
                "-o",
                str(self.work / "application.so"),
                "std.rcgu.o",
                "-shared",
                "-Wl,-soname=application.so",
                f"-Wl,--version-script={self.work / 'list'}",
                "-Wl,--no-undefined-version",
            ],
            "mismatched soname": [
                *self.rustc_std_shared_args(),
                "-Wl,-soname=libstd-deadbeefdeadbeef.so",
            ],
            "alternate second soname": [
                *self.rustc_std_shared_args(),
                "-Wl,-soname,libstd-deadbeefdeadbeef.so",
            ],
            "alternate second version script": [
                *self.rustc_std_shared_args(),
                f"-Wl,--version-script,{self.base / 'outside-version-script'}",
            ],
            "shared PIE mix": [*self.rustc_std_shared_args(), "-pie"],
            "shared linker PIE mix": [*self.rustc_std_shared_args(), "-Wl,-pie"],
            "shared static mix": [*self.rustc_std_shared_args(), "-static"],
            "shared hard-float mix": [
                *self.rustc_std_shared_args(),
                "-mfloat-abi=hard",
            ],
            "shared application script": [
                *self.rustc_std_shared_args(),
                "-Wl,-T,/tmp/alternate.ld",
            ],
        }
        for label, arguments in cases.items():
            with self.subTest(label=label):
                self.capture.unlink(missing_ok=True)
                result = self.invoke(*arguments)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("shared sysroot", result.stderr)
                self.assertFalse(self.capture.exists())

    def test_rejects_shared_version_script_symlink_escape(self):
        arguments = self.rustc_std_shared_args()
        outside = self.base / "outside-version-script"
        outside.write_text("RUST_1.0 { global: *; };\n", encoding="utf-8")
        version_option = next(
            argument for argument in arguments if argument.startswith("-Wl,--version-script=")
        )
        version_script = Path(version_option.split("=", 1)[1])
        version_script.unlink()
        version_script.symlink_to(outside)
        result = self.invoke(*arguments)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shared sysroot version script", result.stderr)
        self.assertIn("current working directory", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_compiler_status_is_returned(self):
        self.status_control.write_text("23", encoding="utf-8")
        result = self.invoke(
            "-o", str(self.work / "application.elf"), "rust-main.o",
        )
        self.assertEqual(result.returncode, 23)

    def test_sanitizes_gcc_process_environment(self):
        result = self.invoke(
            "-o",
            str(self.work / "application.elf"),
            "rust-main.o",
            env={
                "COMPILER_PATH": "/tmp/attacker-tools",
                "GCC_EXEC_PREFIX": "/tmp/attacker-prefix/",
                "LIBRARY_PATH": "/tmp/attacker-libraries",
                "COLLECT_GCC_OPTIONS": "-B/tmp/attacker-tools",
                "LD_PRELOAD": "/tmp/attacker.so",
                "EOS_ATTACKER_SENTINEL": "must-not-reach-gcc",
            },
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        compiler_environment = json.loads(
            self.environment_capture.read_text(encoding="utf-8")
        )
        self.assertEqual(compiler_environment.get("PATH"), os.defpath)
        self.assertEqual(compiler_environment.get("LC_ALL"), "C")
        self.assertEqual(compiler_environment.get("LANG"), "C")
        for variable in (
            "COMPILER_PATH",
            "GCC_EXEC_PREFIX",
            "LIBRARY_PATH",
            "COLLECT_GCC_OPTIONS",
            "LD_PRELOAD",
            "EOS_ATTACKER_SENTINEL",
        ):
            self.assertNotIn(variable, compiler_environment)

    def test_packaged_script_relative_root_is_the_default(self):
        packaged = self.root / "bin" / "eos-rust-link"
        packaged.parent.mkdir(parents=True)
        shutil.copy2(LINK_WRAPPER, packaged)
        env = dict(self.env)
        env.pop("EOS_RUST_SDK_ROOT")
        result = run_python(
            packaged,
            ["-o", str(self.work / "application.elf"), "rust-main.o"],
            cwd=self.work,
            env=env,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_policy_weakening_and_ambiguous_arguments(self):
        cases = {
            "static": ["-static"],
            "hard float": ["-mfloat-abi=hard"],
            "alternate script": ["-Wl,-T,/tmp/alternate.ld"],
            "alternate interpreter": ["-Wl,--dynamic-linker=/tmp/ld.so"],
            "shared output": ["-shared"],
            "PIE disabled": ["-no-pie"],
            "PIC disabled": ["-fno-PIC"],
            "relocatable output": ["-Wl,-r"],
            "response file": ["@hidden-linker-arguments"],
            "GCC crt object": ["crt1.o"],
            "forwarded output": ["-Wl,-o,/tmp/outside.elf"],
            "forwarded response file": ["-Wl,@hidden-linker-arguments"],
            "Xlinker response file": ["-Xlinker", "@hidden-linker-arguments"],
            "alternate tool prefix": ["-B/tmp/attacker-tools"],
            "alternate GCC specs": ["-specs=/tmp/attacker.specs"],
            "alternate sysroot": ["--sysroot=/tmp/attacker-root"],
            "direct alternate output": ["--output=/tmp/outside.elf"],
            "forwarded no PIE": ["-Wl,--no-pie"],
            "linker plugin": ["-Wl,--plugin=/tmp/attacker.so"],
            "compiler plugin": ["-fplugin=/tmp/attacker.so"],
        }
        for label, bad_args in cases.items():
            with self.subTest(label=label):
                self.capture.unlink(missing_ok=True)
                result = self.invoke(
                    "-o", str(self.work / "application.elf"), "rust-main.o", *bad_args
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refusing", result.stderr)
                self.assertFalse(self.capture.exists())

    def test_shared_sysroot_rejects_forwarding_and_tool_selection_escapes(self):
        cases = {
            "forwarded output": ["-Wl,--output=/tmp/outside.so"],
            "forwarded response": ["-Wl,@hidden-linker-arguments"],
            "Xlinker output": ["-Xlinker", "--output=/tmp/outside.so"],
            "tool prefix": ["-B/tmp/attacker-tools"],
            "specs": ["--specs=/tmp/attacker.specs"],
            "sysroot": ["--sysroot", "/tmp/attacker-root"],
            "direct output": ["--output=/tmp/outside.so"],
            "no PIE alias": ["-Wl,-no-pie"],
            "linker plugin": ["-Wl,-plugin,/tmp/attacker.so"],
        }
        for label, extra in cases.items():
            with self.subTest(label=label):
                self.capture.unlink(missing_ok=True)
                result = self.invoke(*self.rustc_std_shared_args(), *extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refusing", result.stderr)
                self.assertFalse(self.capture.exists())

    def test_rejects_output_outside_resolved_current_directory(self):
        result = self.invoke("-o", str(self.base / "outside.elf"), "rust-main.o")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("current working directory", result.stderr)

    def test_rejects_missing_or_multiple_outputs(self):
        missing = self.invoke("rust-main.o")
        multiple = self.invoke(
            "-o",
            str(self.work / "one.elf"),
            "-o",
            str(self.work / "two.elf"),
            "rust-main.o",
        )
        self.assertNotEqual(missing.returncode, 0)
        self.assertNotEqual(multiple.returncode, 0)

    def test_rejects_unpinned_compiler_version(self):
        self.version_control.write_text("14.2.0", encoding="utf-8")
        result = self.invoke(
            "-o", str(self.work / "application.elf"), "rust-main.o",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("14.3.1", result.stderr)

    def test_rejects_multilib_or_tool_symlink_escape(self):
        outside = self.base / "outside-libgcc.a"
        outside.touch()
        self.libgcc_control.write_text(str(outside), encoding="utf-8")
        result = self.invoke(
            "-o", str(self.work / "application.elf"), "rust-main.o",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside EOS_RUST_SDK_ROOT", result.stderr)

        compiler = self.root / "arm-gnu" / "bin" / "arm-none-eabi-gcc"
        compiler.unlink()
        external_compiler = self.base / "arm-none-eabi-gcc"
        write_executable(external_compiler, "#!/bin/sh\nexit 0\n")
        compiler.symlink_to(external_compiler)
        result = self.invoke("-o", str(self.work / "application.elf"), "rust-main.o")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside EOS_RUST_SDK_ROOT", result.stderr)


class LinkerScriptBehaviorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        explicit = os.environ.get("EOS_TASK15_REAL_ARM_GNU")
        known = Path("/home/dev/code/arm-toolchain-build/custom-arm-libs/bin")
        cls.tool_bin = Path(explicit) if explicit else known
        cls.gcc = cls.tool_bin / "arm-none-eabi-gcc"
        cls.readelf = cls.tool_bin / "arm-none-eabi-readelf"

    def setUp(self):
        if not (self.gcc.is_file() and self.readelf.is_file()):
            self.skipTest("ARM GNU 14.3.Rel1 tools are unavailable")
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def compile_assembly(self, name: str, source: str):
        assembly = self.work / f"{name}.s"
        obj = self.work / f"{name}.o"
        assembly.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [
                str(self.gcc),
                "-c",
                "-march=armv7-a",
                "-mtune=cortex-a9",
                "-mfpu=neon-vfpv3",
                "-mfloat-abi=softfp",
                "-fPIC",
                str(assembly),
                "-o",
                str(obj),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return obj

    def compile_c(self, name: str, source: str):
        c_source = self.work / f"{name}.c"
        obj = self.work / f"{name}.o"
        c_source.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [
                str(self.gcc),
                "-c",
                "-std=c11",
                "-march=armv7-a",
                "-mtune=cortex-a9",
                "-mfpu=neon-vfpv3",
                "-mfloat-abi=softfp",
                "-fPIC",
                str(c_source),
                "-o",
                str(obj),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return obj

    def link(self, obj: Path):
        output = self.work / "application.elf"
        return subprocess.run(
            [
                str(self.gcc),
                "-nostdlib",
                "-pie",
                "-Wl,--gc-sections",
                f"-Wl,-T,{SDK_SOURCE_ROOT / 'linker' / 'app_linker_script.ld'}",
                str(obj),
                "-o",
                str(output),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        ), output

    def test_gc_keeps_arm_exidx_extab_and_required_program_headers(self):
        obj = self.compile_assembly(
            "ehabi",
            """.syntax unified
.arm
.section .text.main,"ax",%progbits
.global main
.type main,%function
main:
  ldr r0, bss_address
  bx lr
  .align 2
bss_address:
  .word retained_bss
.size main, .-main
.section .bss.retained,"aw",%nobits
.global retained_bss
retained_bss:
  .space 32
.section .ARM.extab.unused,"a",%progbits
.word 0x01020304
.section .ARM.exidx.unused,"a",%arm_exidx
.word main
.word 1
""",
        )
        result, output = self.link(obj)
        self.assertEqual(result.returncode, 0, result.stderr)
        sections = subprocess.run(
            [str(self.readelf), "-SW", str(output)],
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        programs = subprocess.run(
            [str(self.readelf), "-lW", str(output)],
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        symbols = subprocess.run(
            [str(self.readelf), "-Ws", str(output)],
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        self.assertRegex(sections, r"\.ARM\.extab\s+PROGBITS\s+\S+\s+\S+\s+000004")
        self.assertRegex(sections, r"\.ARM\.exidx\s+ARM_EXIDX\s+\S+\s+\S+\s+000008")
        for program_type in ("INTERP", "LOAD", "DYNAMIC", "ARM_EXIDX"):
            self.assertIn(program_type, programs)
        symbol_values = {
            fields[-1]: int(fields[1], 16)
            for line in symbols.splitlines()
            if len(fields := line.split()) >= 8
            and fields[-1] in {"_end", "__bss_end__"}
        }
        self.assertEqual(symbol_values["_end"], symbol_values["__bss_end__"])

    def test_nonempty_native_tls_is_rejected_by_linker_assertion(self):
        obj = self.compile_assembly(
            "tls",
            """.syntax unified
.arm
.section .text.main,"ax",%progbits
.global main
.type main,%function
main:
  ldr r0, tls_address
  bx lr
  .align 2
tls_address:
  .word tls_value
.size main, .-main
.section .tdata,"awT",%progbits
.global tls_value
tls_value:
.word 42
""",
        )
        result, _ = self.link(obj)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native TLS is unsupported", result.stderr)

    def test_absent_optional_ehabi_hooks_resolve_locally_without_dynamic_relocations(self):
        obj = self.compile_c(
            "weak_hooks",
            """extern void __gnu_Unwind_Find_exidx(void) __attribute__((weak));
extern void __cxa_type_match(void) __attribute__((weak));
int main(void) {
    return __gnu_Unwind_Find_exidx != 0 || __cxa_type_match != 0;
}
""",
        )
        result, output = self.link(obj)
        self.assertEqual(result.returncode, 0, result.stderr)
        relocations = subprocess.run(
            [str(self.readelf), "-rW", str(output)],
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        symbols = subprocess.run(
            [str(self.tool_bin / "arm-none-eabi-nm"), "-u", str(output)],
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        self.assertNotIn("R_ARM_GLOB_DAT", relocations)
        self.assertNotIn("__gnu_Unwind_Find_exidx", symbols)
        self.assertNotIn("__cxa_type_match", symbols)


if __name__ == "__main__":
    unittest.main()
