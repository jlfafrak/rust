#!/usr/bin/env python3

import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="eos-host-consumer-") as directory:
        root = Path(directory)
        (root / "main.c").write_text(
            '''#include <eos_rust_abi.h>
#include <stddef.h>

_Static_assert(sizeof(eos_rust_sockaddr_in6) == 28, "IPv6 ABI size");
_Static_assert(sizeof(eos_rust_addrinfo) >= 32, "addrinfo ABI size");

int main(void) {
    eos_rust_sockaddr_in address = {0};
    eos_rust_pollfd descriptor = {-1, EOS_RUST_POLLIN, 0};
    eos_rust_addrinfo *results = NULL;
    char text[16];
    int32_t status;
    address.sin_family = EOS_RUST_AF_INET;
    status = eos_rust_inet_pton(EOS_RUST_AF_INET, "127.0.0.1",
                                &address.sin_addr);
    if (status != 1 || eos_rust_inet_ntop(EOS_RUST_AF_INET,
                                          &address.sin_addr, text,
                                          sizeof(text)) == NULL) {
        return 1;
    }
    status = eos_rust_getaddrinfo("127.0.0.1", "80", NULL, &results);
    eos_rust_freeaddrinfo(results);
    return status != 0 || eos_rust_poll(&descriptor, 1, 0) != 0 ||
           eos_rust_abi_version() == 0;
}
''',
            encoding="utf-8",
        )
        (root / "CMakeLists.txt").write_text(
            f'''cmake_minimum_required(VERSION 3.20)
project(eos_host_consumer C)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("{args.source.resolve()}" eos-abi)
get_target_property(eos_links eos_rust_abi INTERFACE_LINK_LIBRARIES)
if(NOT "${{eos_links}}" MATCHES "Threads::Threads")
  message(FATAL_ERROR "eos_rust_abi does not transitively publish Threads::Threads")
endif()
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE EOS::RustABI)
''',
            encoding="utf-8",
        )
        result = subprocess.run(
            [args.cmake, "-S", str(root), "-B", str(root / "build")],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr)
            return result.returncode
        return subprocess.run(
            [args.cmake, "--build", str(root / "build")]
        ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
