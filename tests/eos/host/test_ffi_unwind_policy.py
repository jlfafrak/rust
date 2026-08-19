#!/usr/bin/env python3

import os
import re
import signal
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
STD = ROOT / "library" / "std" / "src"
UNWIND = ROOT / "library" / "unwind" / "src" / "lib.rs"
UNWIND_LIBUNWIND = ROOT / "library" / "unwind" / "src" / "libunwind.rs"
UNWIND_WASM = ROOT / "library" / "unwind" / "src" / "wasm.rs"
PANIC_UNWIND = ROOT / "library" / "panic_unwind" / "src"
PERSONALITY = STD / "sys" / "personality" / "mod.rs"
PERSONALITY_GCC = STD / "sys" / "personality" / "gcc.rs"
BACKTRACE_LIBUNWIND = (
    ROOT / "library" / "backtrace" / "src" / "backtrace" / "libunwind.rs"
)
LIFECYCLE = STD / "thread" / "lifecycle.rs"
THREAD = STD / "sys" / "thread" / "unix.rs"
THREAD_LOCAL = STD / "sys" / "thread_local" / "mod.rs"
THREAD_LOCAL_OS = STD / "sys" / "thread_local" / "os.rs"
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
EOS_LIBC_ROOT = ROOT / "src" / "tools" / "eos-libc" / "src"
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
STD_ROOT = STD / "lib.rs"

# EOS entry surfaces use ordinary C ABI only. The unwind crate's two active
# C-unwind declarations are implementation details of the audited ARM EHABI
# backend, not entries that native EOS/C/C++ code may call.
EOS_ENTRY_C_UNWIND_ALLOWLIST = frozenset()
ALL_LIBUNWIND_C_UNWIND = frozenset(
    {
        ("library/unwind/src/libunwind.rs", "_Unwind_Resume"),
        ("library/unwind/src/libunwind.rs", "_Unwind_RaiseException"),
        ("library/unwind/src/libunwind.rs", "_Unwind_SjLj_RaiseException"),
    }
)
EOS_LIBUNWIND_C_UNWIND = frozenset(
    {
        ("library/unwind/src/libunwind.rs", "_Unwind_Resume"),
        ("library/unwind/src/libunwind.rs", "_Unwind_RaiseException"),
    }
)
WASM_C_UNWIND = frozenset({("library/unwind/src/wasm.rs", "wasm_throw")})
C_UNWIND_BLOCK = re.compile(r"\bunsafe\s+extern\s+C_unwind\s*\{")
C_UNWIND_TOKEN = re.compile(r"\bextern\s+C_unwind|\babi\s*=\s*C_unwind")
RAW_STRING_START = re.compile(r'(?:b|c)?r(?P<hashes>#{0,255})"')
CHARACTER_LITERAL = re.compile(
    r"'(?:\\(?:x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f_]+\}|[^\n])|[^'\\\n])'"
)


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


def source_without_non_code(source, *, mask_literals):
    """Mask comments and optionally literals while preserving byte offsets."""
    masked = list(source)

    def mask(start, end):
        for index in range(start, end):
            if masked[index] != "\n":
                masked[index] = " "

    index = 0
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            end = len(source) if end < 0 else end
            mask(index, end)
            index = end
            continue

        if source.startswith("/*", index):
            depth = 1
            end = index + 2
            while end < len(source) and depth:
                if source.startswith("/*", end):
                    depth += 1
                    end += 2
                elif source.startswith("*/", end):
                    depth -= 1
                    end += 2
                else:
                    end += 1
            mask(index, end)
            index = end
            continue

        raw = RAW_STRING_START.match(source, index)
        if raw is not None:
            delimiter = '"' + raw.group("hashes")
            end = source.find(delimiter, raw.end())
            end = len(source) if end < 0 else end + len(delimiter)
            if mask_literals:
                mask(index, end)
            index = end
            continue

        if source[index] == '"':
            end = index + 1
            while end < len(source):
                if source[end] == "\\":
                    end += 2
                elif source[end] == '"':
                    end += 1
                    break
                else:
                    end += 1
            if mask_literals:
                mask(index, min(end, len(source)))
            index = end
            continue

        character = CHARACTER_LITERAL.match(source, index)
        if character is not None:
            end = character.end()
            if mask_literals:
                mask(index, end)
            index = end
            continue

        index += 1

    return "".join(masked)


def source_code_only(source):
    return source_without_non_code(source, mask_literals=True)


def source_without_comments(source):
    return source_without_non_code(source, mask_literals=False)


def braced_body(source, opening):
    code = source_code_only(source)
    if code[opening] != "{":
        raise AssertionError("expected opening brace")
    depth = 0
    for index in range(opening, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError("unterminated braced body")


def body_after_marker(source, marker):
    code = source_code_only(source)
    code_marker = source_code_only(marker)
    marker_index = code.find(code_marker)
    if marker_index < 0:
        raise AssertionError(f"source marker was not found: {marker}")
    opening = code.find("{", marker_index + len(code_marker))
    if opening < 0:
        raise AssertionError(f"source marker has no braced body: {marker}")
    return braced_body(source, opening)


def branch_after_pattern(source, pattern):
    match = re.search(
        pattern + r"\s*=>\s*\{", source_without_comments(source), re.DOTALL
    )
    if match is None:
        raise AssertionError(f"cfg branch was not found: {pattern}")
    return braced_body(source, match.end() - 1)


def c_unwind_declarations(path, source=None):
    if source is None:
        source = path.read_text(encoding="utf-8")
    if "C-unwind" not in source:
        return set()
    # Preserve only the ABI literal as a token before masking all other
    # comments and literals. The replacement is length-preserving.
    source = source_code_only(source.replace('"C-unwind"', " C_unwind "))
    relative = path.relative_to(ROOT).as_posix()
    declarations = set()
    accounted_ranges = []
    for match in C_UNWIND_BLOCK.finditer(source):
        opening = source.index("{", match.start())
        body = braced_body(source, opening)
        closing = opening + len(body) + 1
        accounted_ranges.append((match.start(), closing + 1))
        for function in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)", body):
            declarations.add((relative, function.group(1)))
    for match in C_UNWIND_TOKEN.finditer(source):
        if any(start <= match.start() < end for start, end in accounted_ranges):
            continue
        direct = re.match(
            r"extern\s+C_unwind\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)",
            source[match.start() :],
        )
        name = (
            direct.group(1)
            if direct
            else f"<C-unwind@{source.count(chr(10), 0, match.start()) + 1}>"
        )
        declarations.add((relative, name))
    return declarations


def eos_entry_production_rust_sources():
    paths = {TARGET, EOS_LIBC, UNWIND}
    paths.update(STD.rglob("*.rs"))
    paths.update(PANIC_UNWIND.rglob("*.rs"))
    paths.update(EOS_LIBC_ROOT.rglob("*.rs"))
    return sorted(paths)


def eos_owned_production_rust_sources():
    return sorted(
        set(eos_entry_production_rust_sources()) | {UNWIND, UNWIND_LIBUNWIND}
    )


def dynamic_defined_symbols(nm_output):
    return {
        match.group(1)
        for match in re.finditer(
            r"^[0-9A-Fa-f]+\s+[A-Za-z]\s+(\S+)$", nm_output, re.MULTILINE
        )
    }


def read_text_with_mutation(path, transform):
    original_read_text = Path.read_text

    def mutated_read_text(candidate, *args, **kwargs):
        source = original_read_text(candidate, *args, **kwargs)
        if Path(candidate) == path:
            return transform(source)
        return source

    return mutated_read_text


class FfiUnwindPolicyTests(unittest.TestCase):
    def assert_source_mutation_is_rejected(self, test_name, path, transform):
        case = type(self)(test_name)
        with mock.patch.object(
            Path, "read_text", read_text_with_mutation(path, transform)
        ):
            with self.assertRaises(AssertionError):
                getattr(case, test_name)()

    def assert_markers_in_order(self, source, markers):
        code = source_code_only(source)
        offsets = []
        for marker in markers:
            code_marker = source_code_only(marker)
            self.assertIn(code_marker, code)
            offsets.append(code.index(code_marker))
        self.assertEqual(sorted(offsets), offsets)

    def test_eos_owned_production_has_no_c_unwind_declaration(self):
        offenders = set()
        for path in eos_entry_production_rust_sources():
            offenders.update(c_unwind_declarations(path))
        self.assertEqual(EOS_ENTRY_C_UNWIND_ALLOWLIST, offenders)

    def test_eos_links_the_arm_gnu_unwinder_contract(self):
        source = source_without_comments(UNWIND.read_text(encoding="utf-8"))
        self.assertRegex(
            source,
            re.compile(
                r'#\[cfg\(target_os\s*=\s*"eos"\)\]\s*'
                r'#\[link\(name\s*=\s*"gcc"\)\]\s*'
                r'unsafe\s+extern\s+"C"\s*\{\s*\}',
                re.DOTALL,
            ),
        )

    def test_eos_backtrace_uses_the_arm_gnu_macro_api(self):
        source = source_without_comments(
            BACKTRACE_LIBUNWIND.read_text(encoding="utf-8")
        )
        self.assertRegex(
            source,
            re.compile(
                r'not\(all\(target_os\s*=\s*"eos"\s*,\s*'
                r'target_arch\s*=\s*"arm"\)\)\s*,',
                re.DOTALL,
            ),
        )
        unwind_cfg = source[source.rindex("cfg_if::cfg_if!") :]
        macro_api = body_after_marker(unwind_cfg, "} else")
        self.assert_markers_in_order(
            macro_api,
            (
                "fn _Unwind_VRS_Get(",
                "pub unsafe fn _Unwind_GetIP(",
                "pub unsafe fn _Unwind_FindEnclosingFunction(",
            ),
        )
        self.assertRegex(
            source_without_comments(macro_api),
            r"pub\s+unsafe\s+fn\s+_Unwind_FindEnclosingFunction\s*"
            r"\([^)]*\)\s*->\s*\*mut\s+c_void\s*\{\s*pc\s*\}",
        )

    def test_backtrace_policy_rejects_the_extern_arm_api_for_eos(self):
        self.assert_source_mutation_is_rejected(
            "test_eos_backtrace_uses_the_arm_gnu_macro_api",
            BACKTRACE_LIBUNWIND,
            lambda source: source.replace(
                '            not(all(target_os = "eos", target_arch = "arm")),\n',
                "",
                1,
            ),
        )

    def test_eos_keeps_the_unix_gcc_personality(self):
        target = source_without_comments(TARGET.read_text(encoding="utf-8"))
        self.assertRegex(target, r'families:\s*cvs!\["unix"\]')
        self.assertRegex(target, r'panic_strategy:\s*PanicStrategy::Unwind')
        self.assertRegex(target, r'arch:\s*Arch::Arm')
        self.assertRegex(target, r'llvm_target:\s*"armv7a-unknown-none-eabi"')
        self.assertRegex(target, r'os:\s*Os::Eos')

        personality = source_without_comments(PERSONALITY.read_text(encoding="utf-8"))
        unix_gcc = branch_after_pattern(
            personality,
            r'(?m)^\s*any\(\s*all\(target_family\s*=\s*"windows".*?'
            r'all\(target_family\s*=\s*"unix"\s*,\s*'
            r'not\(target_os\s*=\s*"espidf"\)\s*,\s*'
            r'not\(target_os\s*=\s*"l4re"\)\s*,\s*'
            r'not\(target_os\s*=\s*"nuttx"\)\s*\).*?\)',
        )
        self.assertRegex(source_without_comments(unix_gcc), r"^\s*mod\s+gcc\s*;\s*$")

        gcc = source_without_comments(PERSONALITY_GCC.read_text(encoding="utf-8"))
        arm_ehabi = branch_after_pattern(
            gcc,
            r'(?m)^\s*all\(\s*target_arch\s*=\s*"arm"\s*,\s*'
            r'not\(target_vendor\s*=\s*"apple"\)\s*,\s*'
            r'not\(target_os\s*=\s*"netbsd"\)\s*,\s*\)',
        )
        self.assert_markers_in_order(
            arm_ehabi,
            (
                '#[lang = "eh_personality"]',
                "state: uw::_Unwind_State",
                "fn __gnu_unwind_frame",
            ),
        )

    def test_personality_policy_rejects_disconnected_target_facts(self):
        self.assert_source_mutation_is_rejected(
            "test_eos_keeps_the_unix_gcc_personality",
            TARGET,
            lambda source: source.replace('families: cvs!["unix"]', "families: cvs![]"),
        )

    def test_personality_policy_rejects_non_ehabi_arm_branch(self):
        self.assert_source_mutation_is_rejected(
            "test_eos_keeps_the_unix_gcc_personality",
            PERSONALITY_GCC,
            lambda source: source.replace(
                '    all(\n        target_arch = "arm",\n'
                '        not(target_vendor = "apple"),\n'
                '        not(target_os = "netbsd"),\n',
                '    all(\n        target_arch = "aarch64",\n'
                '        not(target_vendor = "apple"),\n'
                '        not(target_os = "netbsd"),\n',
                1,
            ),
        )

    def test_thread_roots_and_cleanup_have_containment_guards(self):
        lifecycle = LIFECYCLE.read_text(encoding="utf-8")
        thread = THREAD.read_text(encoding="utf-8")
        thread_local = THREAD_LOCAL.read_text(encoding="utf-8")
        thread_local_os = THREAD_LOCAL_OS.read_text(encoding="utf-8")
        native_thread = NATIVE_THREAD.read_text(encoding="utf-8")
        native_tls = NATIVE_TLS.read_text(encoding="utf-8")

        rust_start = body_after_marker(lifecycle, "let rust_start = move ||")
        caught_user_work = body_after_marker(
            rust_start, "panic::catch_unwind(panic::AssertUnwindSafe(||"
        )
        self.assert_markers_in_order(
            caught_user_work, ("hooks.run()", "backtrace(f)")
        )
        self.assert_markers_in_order(
            rust_start,
            ("panic::catch_unwind", "Some(try_result)", "drop(their_packet)"),
        )

        signature = re.search(
            r'extern\s+"(?P<abi>[^"]+)"\s+fn\s+thread_start',
            source_without_comments(thread),
        )
        self.assertIsNotNone(signature)
        self.assertEqual("C", signature.group("abi"))
        thread_start = body_after_marker(thread, signature.group(0))
        thread_markers = (
            "Box::from_raw",
            "init.init()",
            "Handler::new()",
            "rust_start();",
            "ptr::null_mut()",
        )
        self.assert_markers_in_order(thread_start, thread_markers)
        self.assertNotIn("catch_unwind", source_code_only(thread_start))

        self.assertRegex(
            source_without_comments(thread_local),
            re.compile(
                r'target_thread_local\s*=>\s*\{\s*mod\s+native\s*;.*?\}\s*'
                r'_\s*=>\s*\{\s*mod\s+os\s*;\s*pub\s+use\s+os::',
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source_without_comments(thread_local),
            re.compile(
                r'target_family\s*=\s*"unix"\s*,\s*\).*?=>\s*\{\s*'
                r'mod\s+racy\s*;\s*mod\s+unix\s*;',
                re.DOTALL,
            ),
        )
        target = source_without_comments(TARGET.read_text(encoding="utf-8"))
        self.assertRegex(target, r"has_thread_local:\s*false")
        abort_helper = body_after_marker(thread_local, "fn abort_on_dtor_unwind")
        self.assert_markers_in_order(
            abort_helper, ("let guard = DtorUnwindGuard", "f();", "forget(guard)")
        )
        guard_drop = body_after_marker(abort_helper, "fn drop")
        self.assert_markers_in_order(guard_drop, ("rtabort!(",))
        destroy_value = body_after_marker(
            thread_local_os, 'unsafe extern "C" fn destroy_value'
        )
        abort_guard = body_after_marker(destroy_value, "abort_on_dtor_unwind(||")
        tls_markers = (
            "from_raw",
            "let key = ptr.key",
            "without_provenance_mut(1)",
            "drop(ptr)",
            "ptr::null_mut()",
            "guard::enable()",
        )
        self.assert_markers_in_order(abort_guard, tls_markers)

        trampoline = body_after_marker(native_thread, "static void eos_thread_trampoline")
        native_thread_markers = (
            "eos_tls_set_thread_identity(record->identity)",
            "record->start_routine(record->argument)",
            "eos_tls_cleanup_current()",
            "record->result = result",
            "record->completed",
        )
        self.assert_markers_in_order(trampoline, native_thread_markers)

        native_cleanup = body_after_marker(native_tls, "static void eos_tls_cleanup_current")
        native_tls_markers = (
            "eos_port_thread_tls_get",
            "root->cleanup_active",
            "destructor(argument)",
            "eos_tls_release_destructor(ownership)",
            "while (root->values != NULL)",
            "eos_port_thread_tls_set(EOS_RUST_TLS_SLOT, (uintptr_t)0)",
            "eos_port_memory_free(root)",
        )
        self.assert_markers_in_order(native_cleanup, native_tls_markers)

    def test_thread_policy_rejects_a_disconnected_catch_unwind_marker(self):
        self.assert_source_mutation_is_rejected(
            "test_thread_roots_and_cleanup_have_containment_guards",
            LIFECYCLE,
            lambda source: source.replace(
                "panic::catch_unwind", "fake_catch_unwind", 1
            )
            + "\n// panic::catch_unwind is intentionally disconnected.\n",
        )

    def test_tls_policy_rejects_a_disconnected_abort_guard(self):
        self.assert_source_mutation_is_rejected(
            "test_thread_roots_and_cleanup_have_containment_guards",
            THREAD_LOCAL_OS,
            lambda source: source.replace(
                "abort_on_dtor_unwind(|| {", "(|| {", 1
            ).replace("\n    });", "\n    })();", 1)
            + "\n// abort_on_dtor_unwind is intentionally disconnected.\n",
        )

    def test_thread_policy_rejects_commented_out_native_callback(self):
        self.assert_source_mutation_is_rejected(
            "test_thread_roots_and_cleanup_have_containment_guards",
            NATIVE_THREAD,
            lambda source: source.replace(
                "    result = record->start_routine(record->argument);",
                "    /* result = record->start_routine(record->argument); */\n"
                "    result = NULL;",
                1,
            ),
        )

    def test_thread_policy_rejects_commented_out_native_cleanup(self):
        self.assert_source_mutation_is_rejected(
            "test_thread_roots_and_cleanup_have_containment_guards",
            NATIVE_THREAD,
            lambda source: source.replace(
                "    eos_tls_cleanup_current();",
                "    /* eos_tls_cleanup_current(); */",
                1,
            ),
        )

    def test_c_unwind_policy_scans_shared_std_and_libunwind_sources(self):
        sources = set(eos_owned_production_rust_sources())
        self.assertIn(PERSONALITY_GCC, sources)
        self.assertIn(UNWIND_LIBUNWIND, sources)
        self.assertNotIn(UNWIND_WASM, sources)

        target = source_without_comments(TARGET.read_text(encoding="utf-8"))
        self.assertRegex(target, r'families:\s*cvs!\["unix"\]')
        unwind = UNWIND.read_text(encoding="utf-8")
        unix_backend = branch_after_pattern(
            unwind,
            r'(?m)^\s*any\(\s*unix\s*,.*?'
            r'all\(target_os\s*=\s*"wasi"\s*,\s*panic\s*=\s*"unwind"\s*\)\s*,?\s*\)',
        )
        self.assertRegex(
            source_without_comments(unix_backend),
            r"^\s*mod\s+libunwind\s*;\s*pub\s+use\s+libunwind::\*\s*;\s*$",
        )
        wasm_backend = branch_after_pattern(
            unwind, r'(?m)^\s*target_family\s*=\s*"wasm"'
        )
        self.assertRegex(
            source_without_comments(wasm_backend),
            r"^\s*mod\s+wasm\s*;\s*pub\s+use\s+wasm::\*\s*;\s*$",
        )

        libunwind = UNWIND_LIBUNWIND.read_text(encoding="utf-8")
        self.assertEqual(
            ALL_LIBUNWIND_C_UNWIND,
            c_unwind_declarations(UNWIND_LIBUNWIND, libunwind),
        )
        apple_sjlj = branch_after_pattern(
            libunwind,
            r'(?m)^\s*all\(target_vendor\s*=\s*"apple"\s*,\s*'
            r'not\(target_os\s*=\s*"watchos"\)\s*,\s*'
            r'target_arch\s*=\s*"arm"\s*\)',
        )
        excluded = c_unwind_declarations(UNWIND_LIBUNWIND, apple_sjlj)
        self.assertEqual(
            {("library/unwind/src/libunwind.rs", "_Unwind_SjLj_RaiseException")},
            excluded,
        )
        self.assertEqual(EOS_LIBUNWIND_C_UNWIND, ALL_LIBUNWIND_C_UNWIND - excluded)
        self.assertEqual(WASM_C_UNWIND, c_unwind_declarations(UNWIND_WASM))

    def test_c_unwind_policy_rejects_shared_std_declarations(self):
        self.assert_source_mutation_is_rejected(
            "test_eos_owned_production_has_no_c_unwind_declaration",
            PERSONALITY_GCC,
            lambda source: source
            + '\nunsafe extern "C-unwind" { fn unaudited_shared_std(); }\n',
        )

    def test_c_unwind_policy_rejects_unwind_root_declarations(self):
        self.assert_source_mutation_is_rejected(
            "test_eos_owned_production_has_no_c_unwind_declaration",
            UNWIND,
            lambda source: source
            + '\nunsafe extern "C-unwind" { fn injected_unwind_root(); }\n',
        )

    def test_production_and_fixture_crates_deny_ffi_unwind_calls(self):
        expected = {
            STD_ROOT: "#![deny(ffi_unwind_calls)]",
            UNWIND: '#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]',
            UNWIND_APP: '#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]',
            FFI_LIB: '#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]',
        }
        for path, deny in expected.items():
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertIn(
                    deny,
                    source_without_comments(path.read_text(encoding="utf-8")),
                )

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
            self.assertEqual(
                {"eos_ffi_containment_probe"}, dynamic_defined_symbols(symbols)
            )

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

    def test_dynamic_export_policy_rejects_surplus_symbols(self):
        original_run = run

        def run_with_surplus_export(command, *, env=None):
            result = original_run(command, env=env)
            if "--defined-only" in [str(part) for part in command]:
                return subprocess.CompletedProcess(
                    result.args,
                    result.returncode,
                    result.stdout + "0000000000000000 T unaudited_export\n",
                    result.stderr,
                )
            return result

        case = type(self)("test_c_and_cpp_callers_observe_contained_panic")
        with mock.patch(f"{__name__}.run", run_with_surplus_export):
            with self.assertRaises(AssertionError):
                case.test_c_and_cpp_callers_observe_contained_panic()


if __name__ == "__main__":
    unittest.main()
