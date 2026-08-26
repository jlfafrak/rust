#!/usr/bin/env python3

import re
import unittest
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[3]
STD = ROOT / "library" / "std" / "src"
EOS_LIBC = ROOT / "src" / "tools" / "eos-libc" / "src" / "eos" / "mod.rs"

PAL_FILES = (
    STD / "sys" / "alloc" / "unix.rs",
    STD / "sys" / "pal" / "unix" / "mod.rs",
    STD / "sys" / "io" / "error" / "unix.rs",
    STD / "sys" / "io" / "io_slice" / "iovec.rs",
    STD / "sys" / "args" / "unix.rs",
    STD / "sys" / "env" / "unix.rs",
    STD / "sys" / "env_consts.rs",
    STD / "sys" / "fd" / "unix.rs",
    STD / "sys" / "fs" / "unix.rs",
    STD / "sys" / "fs" / "unix" / "dir.rs",
    STD / "sys" / "fs" / "mod.rs",
    STD / "sys" / "net" / "connection" / "socket" / "unix.rs",
    STD / "sys" / "net" / "hostname" / "unix.rs",
    STD / "sys" / "paths" / "unix.rs",
    STD / "sys" / "pipe" / "unix.rs",
    STD / "sys" / "process" / "unix" / "common.rs",
    STD / "sys" / "process" / "unix" / "eos.rs",
    STD / "sys" / "process" / "unix" / "mod.rs",
    STD / "sys" / "thread" / "unix.rs",
    STD / "sys" / "random" / "mod.rs",
    STD / "sys" / "random" / "eos.rs",
    STD / "os" / "unix" / "mod.rs",
    STD / "os" / "unix" / "process.rs",
)

SPECIAL_SHIM_DECLARATIONS = {
    "eos_rust_errno_location": (
        STD / "sys" / "io" / "error" / "unix.rs",
        'link_name = "eos_rust_errno_location"',
    ),
}

EOS_ONLY_FILES = {
    STD / "sys" / "process" / "unix" / "eos.rs",
    STD / "sys" / "random" / "eos.rs",
}

EOS_ONLY_ADJACENT_BRANCHES = {
    STD / "sys" / "random" / "eos.rs": (STD / "sys" / "random" / "mod.rs", "mod eos;"),
}


def cfg_attributes(source: str) -> list[str]:
    return re.findall(r"#\s*!?\s*\[cfg(?:_attr)?\((.*?)\)\]", source, re.DOTALL)


def eos_libc_stable_links() -> dict[str, str]:
    source = EOS_LIBC.read_text(encoding="utf-8")
    return {
        rust_name: link_name
        for link_name, rust_name in re.findall(
            r'#\[link_name\s*=\s*"(eos_rust_[^"]+)"\]\s*'
            r"pub\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
            source,
            re.MULTILINE,
        )
    }


def balanced_block_end(source: str, opening_brace: int) -> int:
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return index + 1
    raise AssertionError(f"unclosed Rust block at byte {opening_brace}")


def item_region(source: str, start: int) -> Optional[tuple[int, int]]:
    opening_brace = source.find("{", start)
    semicolon = source.find(";", start)
    if semicolon != -1 and (opening_brace == -1 or semicolon < opening_brace):
        statement = source[start : semicolon + 1]
        if not re.search(r"\blet\b", statement):
            return None
        following_semicolon = source.find(";", semicolon + 1)
        if following_semicolon == -1:
            return None
        return start, following_semicolon + 1
    if opening_brace == -1:
        return None
    return start, balanced_block_end(source, opening_brace)


def eos_libc_alias_links(source: str, stable_links: dict[str, str]) -> dict[str, str]:
    aliases = {}
    imports = [
        match.group(1)
        for match in re.finditer(r"use\s+libc::\{(.*?)\};", source, re.DOTALL)
    ]
    imports.extend(
        f"{match.group(1)} as {match.group(2)}"
        for match in re.finditer(
            r"use\s+libc::([A-Za-z_][A-Za-z0-9_]*)\s+as\s+"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*;",
            source,
        )
    )
    for imported in imports:
        for rust_name, alias in re.findall(
            r"\b([A-Za-z_][A-Za-z0-9_]*)\s+as\s+([A-Za-z_][A-Za-z0-9_]*)",
            imported,
        ):
            if rust_name in stable_links:
                aliases[alias] = stable_links[rust_name]
    return aliases


def adjacent_comment_start(source: str, start: int) -> int:
    line_start = source.rfind("\n", 0, start) + 1
    cursor = line_start
    while cursor > 0:
        previous_end = cursor - 1
        previous_start = source.rfind("\n", 0, previous_end) + 1
        previous = source[previous_start:previous_end].strip()
        if previous.startswith("//") or previous.startswith("#["):
            cursor = previous_start
            continue
        break
    return cursor


def eos_branch_regions(source: str, *, eos_only: bool) -> list[tuple[int, int]]:
    regions = []
    if eos_only:
        starts = (
            match.start()
            for match in re.finditer(
                r"(?m)^[ \t]*(?:pub(?:\([^)]*\))?[ \t]+)?"
                r"(?:unsafe[ \t]+)?fn[ \t]+[A-Za-z_][A-Za-z0-9_]*[^;{]*\{",
                source,
            )
        )
    else:
        cfg_starts = (
            match.start()
            for match in re.finditer(
                r'#\s*\[cfg\(\s*target_os\s*=\s*"eos"\s*\)\s*\]',
                source,
            )
        )
        arm_starts = (
            match.start()
            for match in re.finditer(
                r'target_os\s*=\s*"eos"\s*=>\s*\{',
                source,
            )
        )
        starts = (*cfg_starts, *arm_starts)

    for start in starts:
        region = item_region(source, start)
        if region is not None and region not in regions:
            regions.append(region)
    return regions


def eos_branch_comment_violations(
    source: str,
    path: Path,
    stable_links: dict[str, str],
    *,
    eos_only: bool = False,
    adjacent_context: str = "",
) -> list[str]:
    violations = []
    aliases = eos_libc_alias_links(source, stable_links)
    for start, end in eos_branch_regions(source, eos_only=eos_only):
        branch = source[adjacent_comment_start(source, start):end]
        comment_scope = f"{adjacent_context}\n{branch}"
        calls = {
            name: stable_links[name]
            for name in re.findall(r"\blibc::([A-Za-z_][A-Za-z0-9_]*)\s*\(", branch)
            if name in stable_links
        }
        calls.update(
            {
                alias: link_name
                for alias, link_name in aliases.items()
                if re.search(rf"\b{re.escape(alias)}\s*\(", branch)
            }
        )
        function = re.search(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)", branch)
        label = function.group(1) if function else "cfg block"
        line = source.count("\n", 0, start) + 1
        for rust_name, link_name in sorted(calls.items()):
            if not re.search(rf"//[^\n]*\b{re.escape(link_name)}\b", comment_scope):
                violations.append(
                    f"{path}:{line}: EOS {label} calls libc::{rust_name} "
                    f"without a nearby comment naming {link_name}"
                )
    return violations


def adjacent_eos_route(path: Path) -> str:
    route = EOS_ONLY_ADJACENT_BRANCHES.get(path)
    if route is None:
        return ""
    route_path, marker = route
    source = route_path.read_text(encoding="utf-8")
    matches = [
        source[start:end]
        for start, end in eos_branch_regions(source, eos_only=False)
        if marker in source[start:end]
    ]
    if len(matches) != 1:
        raise AssertionError(
            f"expected one adjacent EOS route containing {marker!r} in {route_path}"
        )
    return matches[0]


class PalCfgScopeTest(unittest.TestCase):
    def test_eos_is_not_folded_into_linux_cfg_attributes(self) -> None:
        offenders = []
        for path in PAL_FILES:
            source = path.read_text(encoding="utf-8")
            for attribute in cfg_attributes(source):
                if 'target_os = "eos"' in attribute and 'target_os = "linux"' in attribute:
                    offenders.append(f"{path.relative_to(ROOT)}: {attribute.strip()}")
        self.assertEqual(
            offenders,
            [],
            "EOS must use its own PAL branch, not a Linux cfg attribute:\n"
            + "\n".join(offenders),
        )

    def test_linux_only_modules_never_select_eos(self) -> None:
        offenders = []
        for directory in (STD / "sys" / "pal" / "unix" / "linux", STD / "os" / "linux"):
            for path in directory.rglob("*.rs"):
                if 'target_os = "eos"' in path.read_text(encoding="utf-8"):
                    offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [], "Linux-only source must not mention EOS")

    def test_special_eos_declaration_names_its_adjacent_stable_service(self) -> None:
        missing = []
        for service, (path, marker) in SPECIAL_SHIM_DECLARATIONS.items():
            if not path.exists():
                missing.append(f"{path.relative_to(ROOT)} is missing")
                continue
            source = path.read_text(encoding="utf-8")
            marker_line = source.find(marker)
            if marker_line == -1:
                missing.append(f"{path.relative_to(ROOT)} lacks {marker}")
                continue
            context_start = source.rfind("\n", 0, source.rfind("\n", 0, marker_line)) + 1
            context_end = source.find("\n", source.find("\n", marker_line) + 1)
            context = source[context_start:context_end]
            if not re.search(rf"//[^\n]*\b{re.escape(service)}\b", context):
                missing.append(
                    f"{path.relative_to(ROOT)} lacks an adjacent EOS comment naming {service}"
                )
        self.assertEqual(missing, [], "\n".join(missing))

    def test_branch_comment_audit_rejects_uncommented_eos_fcntl_routes(self) -> None:
        fixture = '''
#[cfg(target_os = "eos")]
pub fn set_cloexec(fd: i32) {
    libc::fcntl(fd, libc::F_GETFD, 0);
    libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC);
}

#[cfg(target_os = "eos")]
pub fn set_nonblocking(fd: i32) {
    libc::fcntl(fd, libc::F_GETFL, 0);
    libc::fcntl(fd, libc::F_SETFL, libc::O_NONBLOCK);
}
'''
        violations = eos_branch_comment_violations(
            fixture,
            Path("library/std/src/sys/fd/unix.rs"),
            {"fcntl": "eos_rust_fcntl"},
        )
        self.assertEqual(2, len(violations), violations)
        self.assertTrue(all("eos_rust_fcntl" in item for item in violations))

    def test_branch_comment_audit_rejects_misnamed_statement_route(self) -> None:
        fixture = '''
#[cfg(target_os = "eos")]
use libc::open as open64;

fn open_c(path: *const u8, flags: i32, opts: &Options) {
    #[cfg(not(target_os = "eos"))]
    let mode = opts.mode as i32;
    // EOS routes open through the fixed-signature eos_rust_read service.
    #[cfg(target_os = "eos")]
    let mode = opts.mode as u32;
    let fd = cvt_r(|| unsafe { open64(path, flags, mode) });
}
'''
        violations = eos_branch_comment_violations(
            fixture,
            Path("library/std/src/sys/fs/unix.rs"),
            {"open": "eos_rust_open"},
        )
        self.assertEqual(1, len(violations), violations)
        self.assertIn("eos_rust_open", violations[0])

    def test_every_eos_shim_branch_names_its_exact_stable_service(self) -> None:
        stable_links = eos_libc_stable_links()
        violations = []
        for path in PAL_FILES:
            violations.extend(
                eos_branch_comment_violations(
                    path.read_text(encoding="utf-8"),
                    path.relative_to(ROOT),
                    stable_links,
                    eos_only=path in EOS_ONLY_FILES,
                    adjacent_context=adjacent_eos_route(path),
                )
            )
        self.assertEqual([], violations, "\n".join(violations))

    def test_eos_random_policy_is_literal_and_separate(self) -> None:
        path = STD / "sys" / "random" / "eos.rs"
        self.assertTrue(path.exists(), "EOS must have a separate random policy module")
        source = path.read_text(encoding="utf-8")
        self.assertIn(
            'panic!("EOS v1 does not provide cryptographically secure random bytes")',
            source,
        )
        self.assertIn("libc::eos_hash_seed(&mut key0, &mut key1)", source)

    def test_eos_fixed_open_signature_does_not_change_unix_varargs_promotion(self) -> None:
        source = (STD / "sys" / "fs" / "unix.rs").read_text(encoding="utf-8")
        self.assertIn(
            '#[cfg(not(target_os = "eos"))]\n'
            '        let mode = opts.mode as c_int;',
            source,
        )
        self.assertIn(
            '#[cfg(target_os = "eos")]\n'
            '        let mode = opts.mode as mode_t;',
            source,
        )
        self.assertIn("open64(path.as_ptr(), flags, mode)", source)

    def test_eos_does_not_expose_lossy_unix_raw_process_status(self) -> None:
        source = (STD / "os" / "unix" / "process.rs").read_text(encoding="utf-8")
        for implementor in ("process::ExitStatus", "process::ExitStatusError"):
            self.assertRegex(
                source,
                rf'#\[cfg\(not\(target_os = "eos"\)\)\]\s*'
                rf'(?:#\[[^\n]+\]\s*)*'
                rf'impl ExitStatusExt for {re.escape(implementor)}',
                f"{implementor} must retain its non-EOS Unix raw-status implementation "
                "without pretending the 32-byte EOS status is a c_int wait status",
            )


if __name__ == "__main__":
    unittest.main()
