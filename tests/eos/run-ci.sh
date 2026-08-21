#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
TARGET=armv7a-unknown-eos-eabi

: "${EOS_RUST_SDK_ROOT:?EOS_RUST_SDK_ROOT must name the reviewed Task 16 SDK}"
: "${EOS_ARM_GNU_CC:?EOS_ARM_GNU_CC must name the reviewed ARM GNU 14.3.1 compiler}"
: "${EOS_ARM_GNU_OBJDUMP:?EOS_ARM_GNU_OBJDUMP must name the reviewed binutils 2.44 objdump}"

INITIAL_PYTHON=$(command -v python3) || {
    printf 'required runner Python is unavailable in the initial PATH\n' >&2
    exit 2
}
TRUSTED_PYTHON=$(readlink -f "$INITIAL_PYTHON")
if [[ ! -f "$TRUSTED_PYTHON" || ! -x "$TRUSTED_PYTHON" ]]; then
    printf 'required runner Python is not an executable file: %s\n' "$TRUSTED_PYTHON" >&2
    exit 2
fi
if ! "$TRUSTED_PYTHON" -c 'import sys, tomllib; raise SystemExit(sys.version_info < (3, 11))'; then
    printf 'required runner Python must be Python 3.11 or newer with tomllib: %s\n' \
        "$TRUSTED_PYTHON" >&2
    exit 2
fi

SDK_ROOT=$(cd "$EOS_RUST_SDK_ROOT" && pwd -P)
ARM_CC=$(readlink -f "$EOS_ARM_GNU_CC")
ARM_OBJDUMP=$(readlink -f "$EOS_ARM_GNU_OBJDUMP")
ARM_ROOT=$(cd "$(dirname "$ARM_CC")/.." && pwd -P)
ARTIFACT_ROOT=${EOS_CI_ARTIFACT_DIR:-"$ROOT/artifacts/eos"}
CMAKE_BIN_DIR=${EOS_CMAKE_BIN_DIR:-}

if [[ -n "$CMAKE_BIN_DIR" ]]; then
    CMAKE_BIN_DIR=$(cd "$CMAKE_BIN_DIR" && pwd -P)
fi
export EOS_RUST_SDK_ROOT="$SDK_ROOT"
export EOS_ARM_GNU_CC="$ARM_CC"
export EOS_ARM_GNU_OBJDUMP="$ARM_OBJDUMP"
export LANG=C
export LC_ALL=C
export PYTHONPYCACHEPREFIX=${PYTHONPYCACHEPREFIX:-/tmp/eos-ci-pycache}

"$TRUSTED_PYTHON" "$ROOT/tests/eos/host/test_static_elves.py" --verify-release-identity

if [[ -n "$CMAKE_BIN_DIR" ]]; then
    PATH="$CMAKE_BIN_DIR:$PATH"
fi
PATH="$SDK_ROOT/bin:$PATH"
export PATH

for executable in \
    "$SDK_ROOT/bin/rustc" \
    "$SDK_ROOT/bin/cargo" \
    "$SDK_ROOT/bin/eos-rust-link" \
    "$SDK_ROOT/bin/eos-elf-validate" \
    "$SDK_ROOT/bin/eos-auth-package" \
    "$ARM_CC" \
    "$ARM_OBJDUMP"
do
    if [[ ! -x "$executable" ]]; then
        printf 'required EOS CI tool is not executable: %s\n' "$executable" >&2
        exit 2
    fi
done
for command_name in cmake ctest readelf objdump clang
do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'required EOS CI command is unavailable: %s\n' "$command_name" >&2
        exit 2
    fi
done
if ! "$ARM_CC" --version | head -n 1 | grep -Eq '(^|[^0-9])14\.3\.1([^0-9]|$)'; then
    printf 'EOS CI requires ARM GCC 14.3.1: %s\n' "$ARM_CC" >&2
    exit 2
fi
if ! "$ARM_OBJDUMP" --version | head -n 1 | grep -Eq '(^|[^0-9])2\.44(\.[0-9]+)?([^0-9]|$)'; then
    printf 'EOS CI requires binutils objdump 2.44: %s\n' "$ARM_OBJDUMP" >&2
    exit 2
fi

mkdir -p "$ARTIFACT_ROOT/sdk" "$ARTIFACT_ROOT/static-tests"
cp "$SDK_ROOT/manifests/sdk-layout.toml" "$ARTIFACT_ROOT/sdk/"
cp "$SDK_ROOT/manifests/release-manifest.toml" "$ARTIFACT_ROOT/sdk/"
cp "$SDK_ROOT/share/source-revisions.toml" "$ARTIFACT_ROOT/sdk/"
"$SDK_ROOT/bin/rustc" --version --verbose >"$ARTIFACT_ROOT/sdk/rustc-version.txt"
"$SDK_ROOT/bin/cargo" --version >"$ARTIFACT_ROOT/sdk/cargo-version.txt"
"$ARM_CC" --version >"$ARTIFACT_ROOT/sdk/arm-gcc-version.txt"
"$ARM_OBJDUMP" --version >"$ARTIFACT_ROOT/sdk/arm-objdump-version.txt"

CI_TEMP=$(mktemp -d /tmp/eos-ci.XXXXXX)
trap 'rm -rf -- "$CI_TEMP"' EXIT
HOST_TRIPLE=$("$SDK_ROOT/bin/rustc" -vV | sed -n 's/^host: //p')
BOOTSTRAP_CONFIG="$CI_TEMP/bootstrap.toml"
cp "$ROOT/src/tools/eos-sdk/templates/config.toml" "$BOOTSTRAP_CONFIG"
sed -i \
    -e "s|@HOST_TRIPLE@|$HOST_TRIPLE|g" \
    -e "s|@BUILD_DIR@|$ROOT/build|g" \
    -e "s|@ARM_GNU_ROOT@|$ARM_ROOT|g" \
    "$BOOTSTRAP_CONFIG"

cd "$ROOT"

"$TRUSTED_PYTHON" -m unittest -v tests.eos.host.test_toolchain_lock

./x test \
    --stage 1 \
    --config "$BOOTSTRAP_CONFIG" \
    compiler/rustc_target \
    --test-args eos
./x test \
    --stage 1 \
    library/test \
    --test-args eos_

cmake \
    -S "$ROOT/src/tools/eos-abi" \
    -B "$CI_TEMP/eos-abi-host" \
    -DEOS_RUST_PORT=host \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$CI_TEMP/eos-abi-host" --parallel "${EOS_CI_JOBS:-2}"
ctest --test-dir "$CI_TEMP/eos-abi-host" --output-on-failure

EOS_RUST_RUSTC="$ROOT/build/$HOST_TRIPLE/stage1/bin/rustc" \
PATH="$ROOT/build/$HOST_TRIPLE/stage1/bin:$PATH" \
"$TRUSTED_PYTHON" -m unittest -v \
    tests.eos.host.test_libc_links \
    tests.eos.host.test_libc_source_provenance
PATH="$ROOT/build/$HOST_TRIPLE/stage1/bin:$PATH" \
"$TRUSTED_PYTHON" -m unittest -v \
    tests.eos.host.test_bootstrap_target \
    tests.eos.host.test_pal_cfg_scope \
    tests.eos.host.test_ffi_unwind_policy
env -u EOS_RUST_SDK_ROOT "$TRUSTED_PYTHON" -m unittest discover \
    -s "$ROOT/src/tools/eos-sdk/tests" \
    -p 'test_*.py' \
    -v

./x build \
    --stage 1 \
    --config "$BOOTSTRAP_CONFIG" \
    --target "$TARGET" \
    library/sysroot

EOS_RUST_RUSTC="$ROOT/build/$HOST_TRIPLE/stage1/bin/rustc" \
    "$TRUSTED_PYTHON" "$ROOT/tests/eos/abi/compare_layouts.py" \
    "$ARTIFACT_ROOT/static-tests/layouts"

"$TRUSTED_PYTHON" -m unittest -v tests.eos.host.test_static_elves

printf 'EOS CI gates passed; SDK evidence: %s; static artifacts: %s\n' \
    "$ARTIFACT_ROOT/sdk" "$ARTIFACT_ROOT/static-tests"
