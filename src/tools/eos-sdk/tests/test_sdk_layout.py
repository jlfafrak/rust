import importlib.machinery
import importlib.util
import tomllib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


SDK_SOURCE_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[4]
TARGET = "armv7a-unknown-eos-eabi"
INSTALLER = SDK_SOURCE_ROOT / "bin" / "install-eos-sdk"
BUILD_TOOL = SDK_SOURCE_ROOT / "bin" / "build-eos-sdk"
EXPECTED_HOST_TOOLS = ("rustc", "cargo", "rustdoc")
EXPECTED_TARGET_LIBRARIES = (
    "core",
    "alloc",
    "std",
    "panic_unwind",
    "proc_macro",
    "test",
)
EXPECTED_REQUIRED_PATHS = (
    "bin/eos-rust-link",
    "bin/eos-elf-validate",
    "bin/eos-auth-package",
    "lib/libeos_rust_abi.a",
    "include/eos_rust_abi.h",
    "include/eos_rust_abi_version.h",
    "eos/martos/lib/libmartos_app.so",
    "eos/martos/lib/libmartos_app.so.1.0",
    "eos/martos/lib/libmartos_c++.a",
    "eos/martos/lib/libmartos_c++abi.a",
    "eos/martos/lib/libmartos_c.a",
    "linker/app_linker_script.ld",
    "manifests/toolchain.lock.toml",
    "manifests/allowed-dynamic-symbols.txt",
    "manifests/release-manifest.toml",
    "share/templates/config.toml",
    "share/templates/cargo-config.toml",
    "share/licenses/rust/COPYRIGHT",
    "share/licenses/rust/LICENSE-APACHE",
    "share/licenses/rust/LICENSE-MIT",
    "share/source-revisions.toml",
    "share/examples/hello-std/Cargo.toml",
    "share/examples/hello-std/src/main.rs",
)


def link_or_copy(source: str, destination: str) -> str:
    try:
        os.link(source, destination)
        return destination
    except OSError:
        return shutil.copy2(source, destination)


def write_executable(path: Path, source: str = "#!/bin/sh\nexit 0\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def create_installed_sdk(root: Path) -> None:
    layout_source = SDK_SOURCE_ROOT / "manifests" / "sdk-layout.toml"
    layout = tomllib.loads(layout_source.read_text(encoding="utf-8"))
    layout_path = root / "manifests" / "sdk-layout.toml"
    layout_path.parent.mkdir(parents=True, exist_ok=True)
    layout_path.write_bytes(layout_source.read_bytes())
    for tool in layout["required"]["host_tools"]:
        write_executable(root / "bin" / tool)
    for relative in layout["required"]["paths"]:
        path = root / relative
        if path.exists():
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture\n", encoding="utf-8")
        if relative.startswith("bin/"):
            path.chmod(0o755)
    target_libdir = root / "lib" / "rustlib" / TARGET / "lib"
    target_libdir.mkdir(parents=True, exist_ok=True)
    for library in layout["required"]["target_libraries"]:
        (target_libdir / f"lib{library}-0123456789abcdef.rlib").write_bytes(b"archive")
    (target_libdir / "libstd-0123456789abcdef.so").write_bytes(b"shared")
    for directory in ("arm-gnu", "eos"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    (root / "manifests" / "release-manifest.toml").write_text(
        textwrap.dedent(
            f"""\
            [release]
            version = "1.97.1"
            toolchain = "eos-1.97.1"
            target = "{TARGET}"
            layout_version = 1

            [input_hashes]
            algorithm = "sha256-tree-v1"
            arm_gnu_installed = "{"a" * 64}"
            eos_sdk = "{"b" * 64}"

            [source_revisions]
            rust_fork = "{"c" * 40}"
            rust_upstream = "8bab26f4f68e0e26f0bb7960be334d5b520ea452"
            libc_upstream = "71d5bfcc1bda05da1783666fc2cd7d9669c9c4c8"
            backtrace = "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd"

            [distribution_hashes]
            "rustc-cargo-1.97.1-{host_triple()}.tar.xz" = "{"d" * 64}"
            "rust-std-1.97.1-{TARGET}.tar.xz" = "{"e" * 64}"
            """
        ),
        encoding="utf-8",
    )


def run_installer(root: Path, *, path: str) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PATH"] = path
    return subprocess.run(
        [sys.executable, str(INSTALLER), str(root)],
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=20,
        check=False,
    )


def load_installer_module():
    name = f"_task16_install_eos_sdk_{id(object())}"
    loader = importlib.machinery.SourceFileLoader(name, str(INSTALLER))
    spec = importlib.util.spec_from_loader(name, loader)
    if spec is None:
        raise AssertionError("unable to create installer module spec")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def host_triple() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x86_64-unknown-linux-gnu"
    if machine in {"aarch64", "arm64"}:
        return "aarch64-unknown-linux-gnu"
    raise unittest.SkipTest(f"test fixture has no host mapping for {machine}")


def create_fake_source(root: Path) -> None:
    shutil.copytree(SDK_SOURCE_ROOT, root / "src" / "tools" / "eos-sdk")
    shutil.copytree(REPO_ROOT / "src" / "tools" / "eos-abi", root / "src" / "tools" / "eos-abi")
    shutil.copytree(
        REPO_ROOT / "src" / "tools" / "eos-libc",
        root / "src" / "tools" / "eos-libc",
        copy_function=link_or_copy,
    )
    libc_fixture = (
        root / "tests" / "eos" / "fixtures" / "libc-0.2.185-71d5bfcc.sha256"
    )
    libc_fixture.parent.mkdir(parents=True)
    shutil.copy2(
        REPO_ROOT / "tests" / "eos" / "fixtures" / libc_fixture.name,
        libc_fixture,
    )
    shutil.copytree(
        REPO_ROOT / "tests" / "eos" / "apps" / "hello-std",
        root / "tests" / "eos" / "apps" / "hello-std",
    )
    for license_name in ("COPYRIGHT", "LICENSE-APACHE", "LICENSE-MIT"):
        shutil.copy2(REPO_ROOT / license_name, root / license_name)
    (root / "library" / "backtrace").mkdir(parents=True)
    write_executable(
        root / "x",
        textwrap.dedent(
            f"""\
            #!/usr/bin/env python3
            import json
            import multiprocessing
            import os
            from pathlib import Path
            import sys

            log = Path(os.environ["EOS_TEST_COMMAND_LOG"])
            with log.open("a", encoding="utf-8") as output:
                output.write(json.dumps(["x-start-method", multiprocessing.get_start_method()]) + "\\n")
                output.write(json.dumps(["x", *sys.argv[1:]]) + "\\n")
            arguments = sys.argv[1:]
            build_dir = Path(arguments[arguments.index("--build-dir") + 1])
            source = Path.cwd().resolve(strict=True)
            resolved_build_dir = build_dir.resolve(strict=False)
            with log.open("a", encoding="utf-8") as output:
                output.write(json.dumps(["x-layout", str(source), str(resolved_build_dir)]) + "\\n")
            try:
                resolved_build_dir.relative_to(source)
            except ValueError:
                print("test x: build output escapes the resolved source cwd", file=sys.stderr)
                raise SystemExit(94)
            if os.environ.get("EOS_TEST_X_FAIL") == "1":
                raise SystemExit(95)
            host = {host_triple()!r}
            if arguments[0] == "build":
                stage = build_dir / host / "stage2"
                for name in ("rustc", "rustdoc"):
                    path = stage / "bin" / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("#!/bin/sh\\nexit 0\\n", encoding="utf-8")
                    path.chmod(0o755)
                cargo = build_dir / host / "stage2-tools-bin" / "cargo"
                cargo.parent.mkdir(parents=True, exist_ok=True)
                cargo.write_text("#!/bin/sh\\nexit 0\\n", encoding="utf-8")
                cargo.chmod(0o755)
                libdir = stage / "lib" / "rustlib" / {TARGET!r} / "lib"
                libdir.mkdir(parents=True, exist_ok=True)
                for library in ("core", "alloc", "std", "panic_unwind", "proc_macro", "test"):
                    (libdir / f"lib{{library}}-0123456789abcdef.rlib").write_bytes(b"archive")
                (libdir / "libstd-0123456789abcdef.so").write_bytes(b"shared")
                if os.environ.get("EOS_TEST_X_STAGE2_RUST_SRC_LINK") == "1":
                    rust_src = stage / "lib" / "rustlib" / "src"
                    rust_src.mkdir(parents=True, exist_ok=True)
                    (rust_src / "rust").symlink_to(source, target_is_directory=True)
                if os.environ.get("EOS_TEST_X_STAGE2_RUSTC_SRC_LINK") == "1":
                    rustc_src = stage / "lib" / "rustlib" / "rustc-src"
                    rustc_src.mkdir(parents=True, exist_ok=True)
                    (rustc_src / "rust").symlink_to(source, target_is_directory=True)
                tmp_mode = os.environ.get("EOS_TEST_X_BUILD_TMP")
                if tmp_mode == "nonempty":
                    tmp = build_dir / "tmp"
                    tmp.mkdir()
                    (tmp / "keep-me").write_text("bootstrap state\\n", encoding="utf-8")
                elif tmp_mode == "symlink":
                    (build_dir / "tmp").symlink_to(
                        Path(os.environ["EOS_TEST_EXTERNAL_TMP"]),
                        target_is_directory=True,
                    )
            elif arguments[0] == "dist":
                late_output = os.environ.get("EOS_TEST_LATE_OUTPUT")
                late_kind = os.environ.get("EOS_TEST_LATE_OUTPUT_KIND")
                if late_output and late_kind and not os.path.lexists(late_output):
                    collision = Path(late_output)
                    if late_kind == "file":
                        collision.write_text("preserve late file\\n", encoding="utf-8")
                    elif late_kind == "symlink":
                        collision.symlink_to(Path(os.environ["EOS_TEST_LATE_TARGET"]))
                    elif late_kind == "directory":
                        collision.mkdir()
                    else:
                        raise SystemExit(97)
                mutation = os.environ.get("EOS_TEST_MUTATE_INPUT")
                if mutation:
                    Path(mutation).write_text(
                        "mutated while builder was running\\n", encoding="utf-8"
                    )
                # Real bootstrap dist recreates the stage2 sysroot and may remove tools
                # that were requested only by the preceding exact x build.
                (build_dir / host / "stage2" / "bin" / "rustdoc").unlink(
                    missing_ok=True
                )
                tmp = build_dir / "tmp"
                resolved_tmp = tmp.resolve(strict=True)
                with log.open("a", encoding="utf-8") as output:
                    output.write(
                        json.dumps(
                            ["x-dist-tmp", str(tmp), str(resolved_tmp), tmp.is_symlink()]
                        )
                        + "\\n"
                    )
                if not tmp.is_symlink() or resolved_tmp.parent != Path("/tmp"):
                    print("test x: dist temp is not a private real directory under /tmp", file=sys.stderr)
                    raise SystemExit(96)
                (resolved_tmp / "dist-used-temp").write_text("used\\n", encoding="utf-8")
                dist = build_dir / "dist"
                dist.mkdir(parents=True, exist_ok=True)
                if "rust-std" in arguments:
                    name = "rust-std-1.97.1-armv7a-unknown-eos-eabi.tar.xz"
                else:
                    name = "rustc-cargo-1.97.1-{host_triple()}.tar.xz"
                (dist / name).write_bytes((name + "\\n").encode("utf-8"))
            else:
                raise SystemExit(91)
            """
        ),
    )


def create_fake_arm_gnu(root: Path, version: str = "14.3.1") -> None:
    compiler = root / "bin" / "arm-none-eabi-gcc"
    libgcc = root / "lib" / "gcc" / "arm-none-eabi" / "14.3.1" / "libgcc.a"
    libgcc.parent.mkdir(parents=True, exist_ok=True)
    libgcc.write_bytes(b"libgcc")
    write_executable(
        compiler,
        textwrap.dedent(
            f"""\
            #!/usr/bin/env python3
            import sys
            if "-dumpfullversion" in sys.argv:
                print({version!r})
            elif "-print-libgcc-file-name" in sys.argv:
                print({str(libgcc)!r})
            else:
                raise SystemExit(0)
            """
        ),
    )
    expected = [
        line.strip()
        for line in (REPO_ROOT / "src" / "tools" / "eos-abi" / "tests" / "expected-exports-v1.txt")
        .read_text(encoding="utf-8")
        .splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    write_executable(
        root / "bin" / "arm-none-eabi-nm",
        "#!/usr/bin/env python3\n" + "\n".join(
            f"print('00000000 T {symbol}')" for symbol in expected
        ) + "\n",
    )
    for name in ("arm-none-eabi-ar", "arm-none-eabi-ranlib", "arm-none-eabi-readelf"):
        write_executable(root / "bin" / name)
    for relative in (
        "arm-none-eabi/bin/as",
        "arm-none-eabi/bin/ld",
        "libexec/gcc/arm-none-eabi/14.3.1/collect2",
        "libexec/gcc/arm-none-eabi/14.3.1/liblto_plugin.so",
    ):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"tool")


def create_fake_eos_sdk(root: Path) -> None:
    required = (
        "martos/inc/martos_smp.h",
        "libc/include/errno.h",
        "martos/lib/libmartos_app.so",
        "martos/lib/libmartos_app.so.1.0",
        "martos/lib/libmartos_c++.a",
        "martos/lib/libmartos_c++abi.a",
        "martos/lib/libmartos_c.a",
    )
    for relative in required:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"fixture {relative}\n", encoding="utf-8")
    proprietary = root / "martos" / "src" / "proprietary-runtime.c"
    proprietary.parent.mkdir(parents=True, exist_ok=True)
    proprietary.write_text("proprietary source must not be packaged\n", encoding="utf-8")
    (root / "Release-Notes.md").write_text(
        "# EOS SDK\n\n## Build 14.0.39\n", encoding="utf-8"
    )


def create_fake_build_commands(root: Path) -> None:
    write_executable(
        root / "cmake",
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import json
            import os
            from pathlib import Path
            import sys
            log = Path(os.environ["EOS_TEST_COMMAND_LOG"])
            with log.open("a", encoding="utf-8") as output:
                output.write(json.dumps(["cmake", *sys.argv[1:]]) + "\\n")
            arguments = sys.argv[1:]
            if "--build" in arguments:
                build = Path(arguments[arguments.index("--build") + 1])
                if build.name == "eos-abi-martos":
                    (build / "CMakeFiles" / "eos_rust_abi.dir").mkdir(parents=True, exist_ok=True)
                    (build / "CMakeFiles" / "eos_rust_abi.dir" / "flags.make").write_text(
                        "C_FLAGS = -march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp -fPIC\\n",
                        encoding="utf-8",
                    )
                    (build / "libeos_rust_abi.a").write_bytes(b"abi")
            """
        ),
    )
    write_executable(
        root / "ctest",
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import json
            import os
            from pathlib import Path
            import sys
            with Path(os.environ["EOS_TEST_COMMAND_LOG"]).open("a", encoding="utf-8") as output:
                output.write(json.dumps(["ctest", *sys.argv[1:]]) + "\\n")
            """
        ),
    )
    write_executable(
        root / "git",
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import os
            from pathlib import Path
            import sys
            arguments = sys.argv[1:]
            root = Path(arguments[arguments.index("-C") + 1])
            command = arguments[arguments.index("-C") + 2:]
            is_backtrace = root.name == "backtrace"
            if command[:2] == ["merge-base", "--is-ancestor"]:
                raise SystemExit(0)
            if command[:2] == ["status", "--porcelain=v1"]:
                if is_backtrace and os.environ.get("EOS_TEST_BACKTRACE_DIRTY") == "1":
                    print("?? dirty-backtrace")
                if not is_backtrace and os.environ.get("EOS_TEST_SOURCE_DIRTY") == "1":
                    print("?? dirty-source")
                raise SystemExit(0)
            if command == ["ls-tree", "HEAD", "library/backtrace"]:
                revision = os.environ.get(
                    "EOS_TEST_GITLINK_REV",
                    "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd",
                )
                print(f"160000 commit {revision}\tlibrary/backtrace")
                raise SystemExit(0)
            if command == ["rev-parse", "HEAD"]:
                revision = (
                    os.environ.get(
                        "EOS_TEST_BACKTRACE_REV",
                        "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd",
                    )
                    if is_backtrace
                    else "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                )
                print(revision)
                raise SystemExit(0)
            raise SystemExit(93)
            """
        ),
    )


def run_builder(
    source: Path,
    arm_gnu: Path,
    eos_sdk: Path,
    output: Path,
    fake_bin: Path,
    log: Path,
    extra_environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PATH"] = f"{fake_bin}{os.pathsep}{os.defpath}"
    environment["EOS_TEST_COMMAND_LOG"] = str(log)
    if extra_environment:
        environment.update(extra_environment)
    return subprocess.run(
        [
            sys.executable,
            str(BUILD_TOOL),
            "--source-root",
            str(source),
            "--arm-gnu-root",
            str(arm_gnu),
            "--eos-sdk-root",
            str(eos_sdk),
            "--output",
            str(output),
        ],
        cwd=output.parent,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=240,
        check=False,
    )


class SdkSourceContractTests(unittest.TestCase):
    def test_bootstrap_template_builds_only_the_eos_target_with_unwinding(self):
        template = (SDK_SOURCE_ROOT / "templates" / "config.toml").read_text(
            encoding="utf-8"
        )

        rendered = (
            template.replace("@HOST_TRIPLE@", "x86_64-unknown-linux-gnu")
            .replace("@ARM_GNU_ROOT@", "/opt/arm-gnu")
            .replace("@BUILD_DIR@", "/work/rust-build")
        )
        config = tomllib.loads(rendered)

        self.assertEqual(config["build"]["host"], ["x86_64-unknown-linux-gnu"])
        self.assertEqual(config["build"]["target"], [TARGET])
        self.assertEqual(config["build"]["tools"], ["cargo"])
        self.assertTrue(config["build"]["extended"])
        self.assertEqual(
            config["rust"]["std-features"],
            ["panic-unwind", "backtrace", "backtrace-trace-only"],
        )
        self.assertTrue(config["rust"]["backtrace"])
        self.assertEqual(set(config.get("target", {})), {TARGET})
        target = config["target"][TARGET]
        self.assertEqual(target["cc"], "/opt/arm-gnu/bin/arm-none-eabi-gcc")
        self.assertEqual(target["ar"], "/opt/arm-gnu/bin/arm-none-eabi-ar")
        self.assertEqual(target["ranlib"], "/opt/arm-gnu/bin/arm-none-eabi-ranlib")
        self.assertEqual(target["linker"], "eos-rust-link")
        self.assertFalse(target["crt-static"])

    def test_cargo_template_is_stable_only_and_selects_the_builtin_target(self):
        template = SDK_SOURCE_ROOT / "templates" / "cargo-config.toml"
        config = tomllib.loads(template.read_text(encoding="utf-8"))

        self.assertEqual(config, {
            "build": {"target": TARGET},
            "target": {TARGET: {"linker": "eos-rust-link"}},
        })
        text = template.read_text(encoding="utf-8")
        self.assertNotIn("build-std", text)
        self.assertNotIn(".json", text)
        self.assertNotIn("unstable", text)

    def test_layout_manifest_and_hello_example_cover_release_artifacts(self):
        layout = tomllib.loads(
            (SDK_SOURCE_ROOT / "manifests" / "sdk-layout.toml").read_text(
                encoding="utf-8"
            )
        )

        self.assertEqual(layout["sdk"]["toolchain"], "eos-1.97.1")
        self.assertEqual(layout["sdk"]["target"], TARGET)
        self.assertEqual(tuple(layout["required"]["host_tools"]), EXPECTED_HOST_TOOLS)
        self.assertEqual(
            tuple(layout["required"]["target_libraries"]),
            EXPECTED_TARGET_LIBRARIES,
        )
        self.assertEqual(tuple(layout["required"]["paths"]), EXPECTED_REQUIRED_PATHS)

        manifest = tomllib.loads(
            (REPO_ROOT / "tests" / "eos" / "apps" / "hello-std" / "Cargo.toml")
            .read_text(encoding="utf-8")
        )
        source = (
            REPO_ROOT / "tests" / "eos" / "apps" / "hello-std" / "src" / "main.rs"
        ).read_text(encoding="utf-8")
        self.assertEqual(manifest["package"]["name"], "hello-std")
        self.assertEqual(manifest["workspace"], {})
        self.assertTrue(source.startswith("#![feature(restricted_std)]\n"))
        self.assertIn("std::", source)
        self.assertIn("println!", source)


class InstallerContractTests(unittest.TestCase):
    def test_installer_validates_then_links_the_named_rustup_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            fake_bin = temporary / "fake-bin"
            log = temporary / "rustup.json"
            write_executable(
                fake_bin / "rustup",
                """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys
Path(os.environ["RUSTUP_LOG"]).write_text(json.dumps(sys.argv[1:]), encoding="utf-8")
""",
            )
            environment_path = f"{fake_bin}{os.pathsep}{os.defpath}"
            environment = os.environ.copy()
            environment["PATH"] = environment_path
            environment["RUSTUP_LOG"] = str(log)
            result = subprocess.run(
                [sys.executable, str(INSTALLER), str(sdk)],
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=20,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(log.read_text(encoding="utf-8")),
                ["toolchain", "link", "eos-1.97.1", str(sdk.resolve())],
            )
            self.assertIn("eos-1.97.1", result.stdout)

    def test_installer_prints_explicit_commands_when_rustup_is_unavailable(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            empty_path = temporary / "empty-path"
            empty_path.mkdir()

            result = run_installer(sdk, path=str(empty_path))

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"RUSTC='{sdk.resolve() / 'bin' / 'rustc'}'", result.stdout)
            self.assertIn(f"CARGO='{sdk.resolve() / 'bin' / 'cargo'}'", result.stdout)
            self.assertIn(f"PATH='{sdk.resolve() / 'bin'}':$PATH", result.stdout)

    def test_installer_rejects_an_incomplete_target_sysroot_before_rustup(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            next((sdk / "lib" / "rustlib" / TARGET / "lib").glob("libtest-*.rlib")).unlink()
            fake_bin = temporary / "fake-bin"
            write_executable(fake_bin / "rustup", "#!/bin/sh\nexit 99\n")

            result = run_installer(sdk, path=f"{fake_bin}{os.pathsep}{os.defpath}")

            self.assertEqual(result.returncode, 2)
            self.assertIn("target library test", result.stderr)

    def test_installer_rejects_a_required_path_that_escapes_by_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            library = sdk / "lib" / "libeos_rust_abi.a"
            library.unlink()
            outside = temporary / "outside.a"
            outside.write_bytes(b"archive")
            library.symlink_to(outside)

            result = run_installer(sdk, path=os.defpath)

            self.assertEqual(result.returncode, 2)
            self.assertIn("outside the SDK root", result.stderr)

    def test_installer_rejects_weakened_or_expanded_layout_contracts(self):
        mutations = {
            "weakened": lambda text: text.replace(
                'host_tools = ["rustc", "cargo", "rustdoc"]',
                'host_tools = ["rustc", "cargo"]',
            ),
            "expanded": lambda text: text.replace(
                '    "bin/eos-rust-link",',
                '    "bin/unapproved-tool",\n    "bin/eos-rust-link",',
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                temporary = Path(directory)
                sdk = temporary / "sdk"
                create_installed_sdk(sdk)
                if name == "expanded":
                    write_executable(sdk / "bin" / "unapproved-tool")
                layout = sdk / "manifests" / "sdk-layout.toml"
                layout.write_text(
                    mutate(layout.read_text(encoding="utf-8")), encoding="utf-8"
                )

                result = run_installer(sdk, path=str(temporary / "no-rustup"))

                self.assertEqual(result.returncode, 2)
                self.assertIn("exact Task 16 contract", result.stderr)

    def test_installer_recursively_rejects_unsafe_arm_and_eos_entries(self):
        cases = ("broken-arm-link", "escaping-eos-link", "arm-fifo", "extra-eos-header")
        for case in cases:
            native_parent = "/tmp" if case == "arm-fifo" else None
            with self.subTest(case=case), tempfile.TemporaryDirectory(
                dir=native_parent
            ) as directory:
                temporary = Path(directory)
                sdk = temporary / "sdk"
                create_installed_sdk(sdk)
                if case == "broken-arm-link":
                    (sdk / "arm-gnu" / "broken").symlink_to(temporary / "missing")
                elif case == "escaping-eos-link":
                    outside = temporary / "outside"
                    outside.write_text("outside\n", encoding="utf-8")
                    (sdk / "eos" / "escape").symlink_to(outside)
                elif case == "arm-fifo":
                    os.mkfifo(sdk / "arm-gnu" / "unsupported")
                else:
                    header = sdk / "eos" / "martos" / "inc" / "proprietary.h"
                    header.parent.mkdir(parents=True)
                    header.write_text("proprietary\n", encoding="utf-8")

                result = run_installer(sdk, path=str(temporary / "no-rustup"))

                self.assertEqual(result.returncode, 2)
                self.assertTrue(
                    any(
                        phrase in result.stderr
                        for phrase in ("broken", "outside", "unsupported", "exact runtime closure")
                    ),
                    result.stderr,
                )

    def test_installer_rejects_corrupt_release_manifest_contracts(self):
        mutations = {
            "extra-schema": lambda text: text + "\n[unexpected]\nvalue = 1\n",
            "bad-digest": lambda text: text.replace("a" * 64, "not-a-digest", 1),
            "numeric-digest": lambda text: text.replace(
                f'"{"a" * 64}"', "1" * 64, 1
            ),
            "unsafe-archive-name": lambda text: text.replace(
                f"rustc-cargo-1.97.1-{host_triple()}.tar.xz",
                "../rustc-cargo.tar.xz",
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                temporary = Path(directory)
                sdk = temporary / "sdk"
                create_installed_sdk(sdk)
                manifest = sdk / "manifests" / "release-manifest.toml"
                manifest.write_text(
                    mutate(manifest.read_text(encoding="utf-8")), encoding="utf-8"
                )

                result = run_installer(sdk, path=str(temporary / "no-rustup"))

                self.assertEqual(result.returncode, 2)
                self.assertIn("release manifest", result.stderr)

    def test_installer_rechecks_package_fingerprint_before_no_rustup_output(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            installer = load_installer_module()

            def mutate_package(_name):
                (sdk / "bin" / "rustc").write_text(
                    "#!/bin/sh\nexit 7\n", encoding="utf-8"
                )
                return None

            with (
                mock.patch.object(installer.shutil, "which", side_effect=mutate_package),
                mock.patch.object(installer.subprocess, "run") as external,
                self.assertRaisesRegex(
                    installer.InstallError, "changed after validation"
                ),
            ):
                installer.main([str(sdk)])
            external.assert_not_called()

    def test_installer_rechecks_root_identity_before_fake_rustup(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            sdk = temporary / "sdk"
            create_installed_sdk(sdk)
            rustup = temporary / "rustup"
            write_executable(rustup)
            installer = load_installer_module()

            def replace_root(_name):
                sdk.rename(temporary / "validated-sdk")
                create_installed_sdk(sdk)
                return str(rustup)

            with (
                mock.patch.object(installer.shutil, "which", side_effect=replace_root),
                mock.patch.object(installer.subprocess, "run") as external,
                self.assertRaisesRegex(
                    installer.InstallError, "root identity changed after validation"
                ),
            ):
                installer.main([str(sdk)])
            external.assert_not_called()


class BuilderContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory(dir="/tmp")
        self.output_directory = tempfile.TemporaryDirectory(dir="/tmp")
        self.temporary = Path(self.temporary_directory.name)
        self.source = self.temporary / "source"
        self.arm_gnu = self.temporary / "arm-gnu-input"
        self.eos_sdk = self.temporary / "eos-input"
        self.output = Path(self.output_directory.name) / "eos-sdk"
        self.fake_bin = self.temporary / "fake-bin"
        self.log = self.temporary / "commands.jsonl"
        create_fake_source(self.source)
        create_fake_arm_gnu(self.arm_gnu)
        create_fake_eos_sdk(self.eos_sdk)
        create_fake_build_commands(self.fake_bin)

    def tearDown(self):
        self.output_directory.cleanup()
        self.temporary_directory.cleanup()

    def test_builder_runs_all_gates_and_stages_a_relocatable_complete_sdk(self):
        source_example = self.source / "tests" / "eos" / "apps" / "hello-std"
        (source_example / "Cargo.lock").write_text("generated lock\n", encoding="utf-8")
        generated_target = source_example / "target"
        generated_target.mkdir(exist_ok=True)
        (generated_target / "do-not-package").write_text(
            "generated artifact\n", encoding="utf-8"
        )

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / "bin" / "rustc").is_file())
        self.assertTrue((self.output / "bin" / "cargo").is_file())
        self.assertTrue((self.output / "bin" / "rustdoc").is_file())
        self.assertEqual((self.output / "lib" / "libeos_rust_abi.a").read_bytes(), b"abi")
        packaged_eos = self.output / "eos"
        self.assertEqual(
            {
                path.relative_to(packaged_eos).as_posix()
                for path in packaged_eos.rglob("*")
                if path.is_file() or path.is_symlink()
            },
            {
                "martos/lib/libmartos_app.so",
                "martos/lib/libmartos_app.so.1.0",
                "martos/lib/libmartos_c++.a",
                "martos/lib/libmartos_c++abi.a",
                "martos/lib/libmartos_c.a",
            },
        )
        self.assertFalse((packaged_eos / "martos" / "inc").exists())
        self.assertFalse((packaged_eos / "martos" / "src").exists())
        self.assertFalse((packaged_eos / "libc").exists())
        self.assertTrue(
            (self.output / "arm-gnu" / "bin" / "arm-none-eabi-gcc").is_file()
        )
        packaged_example = self.output / "share" / "examples" / "hello-std"
        self.assertEqual(
            {
                path.relative_to(packaged_example)
                for path in packaged_example.rglob("*")
                if path.is_file() or path.is_symlink()
            },
            {Path("Cargo.toml"), Path("src/main.rs")},
        )
        self.assertFalse((packaged_example / "Cargo.lock").exists())
        self.assertFalse((packaged_example / "target").exists())

        release = tomllib.loads(
            (self.output / "manifests" / "release-manifest.toml").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(release["release"]["target"], TARGET)
        self.assertEqual(release["source_revisions"]["rust_fork"], "a" * 40)
        self.assertEqual(
            release["source_revisions"]["backtrace"],
            "02ef1b533157e8ddbd0f9295c867e79b59e9bbbd",
        )
        self.assertRegex(release["input_hashes"]["arm_gnu_installed"], r"^[0-9a-f]{64}$")
        self.assertRegex(release["input_hashes"]["eos_sdk"], r"^[0-9a-f]{64}$")
        self.assertGreaterEqual(len(release["distribution_hashes"]), 2)

        revisions = tomllib.loads(
            (self.output / "share" / "source-revisions.toml").read_text(encoding="utf-8")
        )
        self.assertEqual(revisions["rust"]["version"], "1.97.1")
        self.assertEqual(
            revisions["rust"]["upstream_commit"],
            "8bab26f4f68e0e26f0bb7960be334d5b520ea452",
        )
        self.assertEqual(revisions["libc"]["version"], "0.2.185")

        commands = [
            json.loads(line)
            for line in self.log.read_text(encoding="utf-8").splitlines()
        ]
        self.assertTrue(any(command[0] == "ctest" for command in commands))
        configure_commands = [
            command for command in commands if command[:2] == ["cmake", "-S"]
        ]
        self.assertEqual(len(configure_commands), 2)
        martos_configure = next(
            command
            for command in configure_commands
            if "-DEOS_RUST_PORT=martos_14_0_39" in command
        )
        self.assertIn("-DBUILD_TESTING=OFF", martos_configure)
        self.assertIn(
            f"-DCMAKE_C_COMPILER={self.arm_gnu.resolve() / 'bin' / 'arm-none-eabi-gcc'}",
            martos_configure,
        )
        x_commands = [command for command in commands if command[0] == "x"]
        x_layouts = [command for command in commands if command[0] == "x-layout"]
        self.assertEqual([command[1] for command in x_commands], ["build", "dist", "dist"])
        self.assertEqual(len(x_layouts), 3)
        for _, cwd, build_dir in x_layouts:
            self.assertEqual(Path(cwd), self.source.resolve())
            Path(build_dir).relative_to(self.source.resolve())
        self.assertFalse(list(self.source.glob(".eos-sdk-rust-build-*")))
        dist_tmps = [command for command in commands if command[0] == "x-dist-tmp"]
        self.assertEqual(len(dist_tmps), 2)
        self.assertEqual({command[2] for command in dist_tmps}, {dist_tmps[0][2]})
        self.assertTrue(all(command[3] for command in dist_tmps))
        native_dist_tmp = Path(dist_tmps[0][2])
        self.assertEqual(native_dist_tmp.parent, Path("/tmp"))
        self.assertFalse(native_dist_tmp.exists())
        self.assertEqual(
            [command[1] for command in commands if command[0] == "x-start-method"],
            ["fork", "fork", "fork"],
        )
        self.assertIn("compiler/rustc", x_commands[0])
        self.assertIn("src/tools/cargo", x_commands[0])
        self.assertIn("src/tools/rustdoc", x_commands[0])
        self.assertIn("library/std", x_commands[0])
        self.assertIn("library/test", x_commands[0])
        self.assertIn("rustc", x_commands[1])
        self.assertIn("cargo", x_commands[1])
        self.assertIn("rust-std", x_commands[2])

        install_check = run_installer(
            self.output, path=str(self.temporary / "no-rustup")
        )
        self.assertEqual(install_check.returncode, 0, install_check.stderr)

    def test_builder_hashes_the_complete_supplied_input_roots(self):
        arm_omitted = self.arm_gnu / "share" / "not-staged.txt"
        arm_omitted.parent.mkdir(parents=True)
        arm_omitted.write_text("arm input version one\n", encoding="utf-8")
        eos_omitted = self.eos_sdk / "martos" / "src" / "proprietary-runtime.c"

        first_output = self.output.parent / "first-sdk"
        first = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            first_output,
            self.fake_bin,
            self.log,
        )
        self.assertEqual(first.returncode, 0, first.stderr)
        first_hashes = tomllib.loads(
            (first_output / "manifests" / "release-manifest.toml").read_text(
                encoding="utf-8"
            )
        )["input_hashes"]

        arm_omitted.write_text("arm input version two\n", encoding="utf-8")
        eos_omitted.write_text("proprietary source version two\n", encoding="utf-8")
        second_output = self.output.parent / "second-sdk"
        second = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            second_output,
            self.fake_bin,
            self.temporary / "second-commands.jsonl",
        )
        self.assertEqual(second.returncode, 0, second.stderr)
        second_hashes = tomllib.loads(
            (second_output / "manifests" / "release-manifest.toml").read_text(
                encoding="utf-8"
            )
        )["input_hashes"]

        self.assertNotEqual(
            first_hashes["arm_gnu_installed"],
            second_hashes["arm_gnu_installed"],
        )
        self.assertNotEqual(first_hashes["eos_sdk"], second_hashes["eos_sdk"])

    def test_builder_rejects_an_input_mutated_after_the_initial_hash(self):
        mutation = self.eos_sdk / "martos" / "src" / "proprietary-runtime.c"

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_MUTATE_INPUT": str(mutation)},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("input tree changed during the build", result.stderr)
        self.assertFalse(self.output.exists())

    def test_builder_atomically_preserves_late_output_collisions(self):
        for kind in ("file", "symlink", "directory"):
            with self.subTest(kind=kind):
                output = self.output.parent / f"late-{kind}-sdk"
                target = self.temporary / f"late-{kind}-target"
                target.write_text("preserve symlink target\n", encoding="utf-8")
                result = run_builder(
                    self.source,
                    self.arm_gnu,
                    self.eos_sdk,
                    output,
                    self.fake_bin,
                    self.temporary / f"late-{kind}-commands.jsonl",
                    {
                        "EOS_TEST_LATE_OUTPUT": str(output),
                        "EOS_TEST_LATE_OUTPUT_KIND": kind,
                        "EOS_TEST_LATE_TARGET": str(target),
                    },
                )

                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("output collision", result.stderr)
                if kind == "file":
                    self.assertEqual(
                        output.read_text(encoding="utf-8"), "preserve late file\n"
                    )
                elif kind == "symlink":
                    self.assertTrue(output.is_symlink())
                    self.assertEqual(output.resolve(strict=True), target.resolve(strict=True))
                    self.assertEqual(
                        target.read_text(encoding="utf-8"),
                        "preserve symlink target\n",
                    )
                else:
                    self.assertTrue(output.is_dir())
                    self.assertEqual(list(output.iterdir()), [])

    def test_builder_omits_stage2_rust_source_symlink_that_escapes_the_sdk(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_X_STAGE2_RUST_SRC_LINK": "1"},
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.output / "lib" / "rustlib" / "src").exists())
        self.assertTrue((self.output / "bin" / "rustc").is_file())
        self.assertTrue((self.output / "bin" / "cargo").is_file())
        self.assertTrue((self.output / "bin" / "rustdoc").is_file())
        target_lib = self.output / "lib" / "rustlib" / TARGET / "lib"
        self.assertTrue(list(target_lib.glob("libstd-*.rlib")))
        self.assertTrue(list(target_lib.glob("libtest-*.rlib")))
        sdk_root = self.output.resolve(strict=True)
        for path in self.output.rglob("*"):
            if path.is_symlink():
                path.resolve(strict=True).relative_to(sdk_root)

    def test_builder_omits_dist_rustc_source_symlink_that_escapes_the_sdk(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_X_STAGE2_RUSTC_SRC_LINK": "1"},
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.output / "lib" / "rustlib" / "rustc-src").exists())
        self.assertTrue((self.output / "bin" / "rustc").is_file())
        self.assertTrue((self.output / "bin" / "cargo").is_file())
        self.assertTrue((self.output / "bin" / "rustdoc").is_file())
        target_lib = self.output / "lib" / "rustlib" / TARGET / "lib"
        self.assertTrue(list(target_lib.glob("libstd-*.rlib")))
        self.assertTrue(list(target_lib.glob("libtest-*.rlib")))
        sdk_root = self.output.resolve(strict=True)
        for path in self.output.rglob("*"):
            if path.is_symlink():
                path.resolve(strict=True).relative_to(sdk_root)

    def test_builder_cleans_private_rust_build_directory_after_x_failure(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_X_FAIL": "1"},
        )

        self.assertEqual(result.returncode, 2)
        commands = [
            json.loads(line)
            for line in self.log.read_text(encoding="utf-8").splitlines()
        ]
        _, cwd, build_dir = next(
            command for command in commands if command[0] == "x-layout"
        )
        self.assertEqual(Path(cwd), self.source.resolve())
        Path(build_dir).relative_to(self.source.resolve())
        self.assertFalse(Path(build_dir).exists())
        self.assertFalse(list(self.source.glob(".eos-sdk-rust-build-*")))
        self.assertFalse(self.output.exists())

    def test_builder_does_not_follow_a_colliding_private_build_symlink(self):
        outside = self.temporary / "collision-target"
        outside.mkdir()
        collision = self.source / ".eos-sdk-rust-build-collision"
        collision.symlink_to(outside, target_is_directory=True)

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            json.loads(line)
            for line in self.log.read_text(encoding="utf-8").splitlines()
        ]
        build_dirs = {
            Path(command[2])
            for command in commands
            if command[0] == "x-layout"
        }
        self.assertEqual(len(build_dirs), 1)
        build_dir = next(iter(build_dirs))
        build_dir.relative_to(self.source.resolve())
        self.assertNotEqual(build_dir, collision)
        self.assertFalse(build_dir.exists())
        self.assertTrue(collision.is_symlink())
        self.assertEqual(list(outside.iterdir()), [])

    def test_builder_refuses_to_discard_existing_bootstrap_temp_state(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_X_BUILD_TMP": "nonempty"},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("bootstrap temp directory is not empty", result.stderr)
        commands = [
            json.loads(line)
            for line in self.log.read_text(encoding="utf-8").splitlines()
        ]
        self.assertFalse(any(command[0] == "x-dist-tmp" for command in commands))
        self.assertFalse(list(self.source.glob(".eos-sdk-rust-build-*")))
        self.assertFalse(self.output.exists())

    def test_builder_refuses_a_bootstrap_temp_symlink_without_touching_its_target(self):
        outside = self.temporary / "external-bootstrap-temp"
        outside.mkdir()
        sentinel = outside / "sentinel"
        sentinel.write_text("preserve\n", encoding="utf-8")

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {
                "EOS_TEST_X_BUILD_TMP": "symlink",
                "EOS_TEST_EXTERNAL_TMP": str(outside),
            },
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("bootstrap temp path must not be a symlink", result.stderr)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve\n")
        self.assertFalse(list(self.source.glob(".eos-sdk-rust-build-*")))
        self.assertFalse(self.output.exists())

    def test_builder_rejects_lock_drift_before_running_external_builds(self):
        lock = self.source / "src" / "tools" / "eos-sdk" / "manifests" / "toolchain.lock.toml"
        lock.write_text(
            lock.read_text(encoding="utf-8").replace('version = "1.97.1"', 'version = "1.98.0"', 1),
            encoding="utf-8",
        )

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("toolchain lock", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.log.exists())

    def test_builder_rejects_a_dirty_outer_source_tree(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_SOURCE_DIRTY": "1"},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("Rust source tree must be clean", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.log.exists())

    def test_builder_rejects_a_dirty_backtrace_submodule(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_BACKTRACE_DIRTY": "1"},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("backtrace source tree must be clean", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.log.exists())

    def test_builder_rejects_a_wrong_outer_backtrace_gitlink(self):
        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
            {"EOS_TEST_GITLINK_REV": "f" * 40},
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("backtrace gitlink", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.log.exists())

    def test_builder_rejects_vendored_libc_content_drift(self):
        libc_readme = self.source / "src" / "tools" / "eos-libc" / "README.md"
        libc_readme.unlink()
        libc_readme.write_text("corrupted vendored libc\n", encoding="utf-8")

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("vendored libc content", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.log.exists())

    def test_builder_rejects_the_wrong_arm_gnu_version(self):
        create_fake_arm_gnu(self.arm_gnu, version="15.1.0")

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("GCC 14.3.1", result.stderr)
        self.assertFalse(self.output.exists())

    def test_builder_rejects_missing_eos_release_notes(self):
        (self.eos_sdk / "Release-Notes.md").unlink()

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("Release-Notes.md", result.stderr)
        self.assertFalse(self.output.exists())

    def test_builder_rejects_the_wrong_eos_release_notes_heading(self):
        (self.eos_sdk / "Release-Notes.md").write_text(
            "# EOS SDK\n\n## Build 14.0.40\n", encoding="utf-8"
        )

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("## Build 14.0.39", result.stderr)
        self.assertFalse(self.output.exists())

    def test_builder_rejects_an_input_symlink_escape(self):
        compiler = self.arm_gnu / "bin" / "arm-none-eabi-gcc"
        compiler.unlink()
        outside = self.temporary / "outside-gcc"
        write_executable(outside)
        compiler.symlink_to(outside)

        result = run_builder(
            self.source,
            self.arm_gnu,
            self.eos_sdk,
            self.output,
            self.fake_bin,
            self.log,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("outside", result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
