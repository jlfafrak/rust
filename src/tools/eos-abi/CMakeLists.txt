cmake_minimum_required(VERSION 3.20)

project(eos_rust_abi VERSION 1.0.0 LANGUAGES C)

set(EOS_RUST_PORT "host" CACHE STRING "EOS Rust ABI native port")
set_property(CACHE EOS_RUST_PORT PROPERTY STRINGS host martos_14_0_39)
option(EOS_RUST_DEBUG_ERRORS
    "Record unknown native statuses in a private debug diagnostic" OFF)

if(NOT DEFINED BUILD_TESTING)
    if(EOS_RUST_PORT STREQUAL "host")
        set(BUILD_TESTING ON CACHE BOOL "Build the EOS Rust ABI tests")
    else()
        set(BUILD_TESTING OFF CACHE BOOL "Build the EOS Rust ABI tests")
    endif()
endif()

include(CTest)

if(EOS_RUST_PORT STREQUAL "host")
    set(EOS_RUST_PORT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/ports/host/eos_port_host.c")
elseif(EOS_RUST_PORT STREQUAL "martos_14_0_39")
    if(BUILD_TESTING)
        message(FATAL_ERROR
            "BUILD_TESTING=ON is not supported for EOS_RUST_PORT=martos_14_0_39; "
            "configure the host port to run native tests")
    endif()
    set(EOS_RUST_PORT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/ports/martos_14_0_39/eos_port_martos.c")
else()
    message(FATAL_ERROR
        "Unsupported EOS_RUST_PORT '${EOS_RUST_PORT}'; expected host or martos_14_0_39")
endif()

add_library(eos_rust_abi STATIC
    src/eos_abi.c
)
add_library(EOS::RustABI ALIAS eos_rust_abi)

set_target_properties(eos_rust_abi PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE YES
    OUTPUT_NAME eos_rust_abi
)
target_include_directories(eos_rust_abi
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(eos_rust_abi PRIVATE
    EOS_RUST_PORT_SOURCE="${EOS_RUST_PORT_SOURCE}"
)
if(EOS_RUST_DEBUG_ERRORS)
    target_compile_definitions(eos_rust_abi PRIVATE EOS_RUST_DEBUG_ERRORS=1)
endif()

if(EOS_RUST_PORT STREQUAL "martos_14_0_39")
    set(EOS_SDK_ROOT "" CACHE PATH "MARTOS-SMP 14.0.39 EOS SDK root")
    set(EOS_MARTOS_INCLUDE_DIR "${EOS_SDK_ROOT}/martos/inc")
    set(EOS_LIBC_INCLUDE_DIR "${EOS_SDK_ROOT}/libc/include")
    if(NOT EOS_SDK_ROOT)
        message(FATAL_ERROR
            "EOS_SDK_ROOT is required for EOS_RUST_PORT=martos_14_0_39")
    elseif(NOT EXISTS "${EOS_MARTOS_INCLUDE_DIR}/martos_smp.h")
        message(FATAL_ERROR
            "EOS_SDK_ROOT does not contain martos/inc/martos_smp.h")
    elseif(NOT EXISTS "${EOS_LIBC_INCLUDE_DIR}/errno.h")
        message(FATAL_ERROR
            "EOS_SDK_ROOT does not contain libc/include/errno.h")
    endif()
    target_include_directories(eos_rust_abi PRIVATE
        "${EOS_MARTOS_INCLUDE_DIR}"
    )
    target_compile_definitions(eos_rust_abi PRIVATE
        EOS_LIBC_ERRNO_HEADER="${EOS_LIBC_INCLUDE_DIR}/errno.h"
    )
endif()

if(BUILD_TESTING)
    enable_language(CXX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Threads REQUIRED)

    add_executable(abi_contract_test tests/abi_contract_test.cpp)
    set_target_properties(abi_contract_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_link_libraries(abi_contract_test PRIVATE EOS::RustABI Threads::Threads)
    add_test(NAME abi_contract COMMAND abi_contract_test)

    add_executable(error_map_test
        tests/error_map_test.cpp
        src/eos_error.c
    )
    set_target_properties(error_map_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_compile_definitions(error_map_test PRIVATE
        EOS_RUST_TESTING=1
        EOS_RUST_DEBUG_ERRORS=1
    )
    target_include_directories(error_map_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    add_test(NAME error_map COMMAND error_map_test)

    add_library(eos_rust_abi_host_test STATIC src/eos_abi.c)
    set_target_properties(eos_rust_abi_host_test PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
        C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE YES)
    target_include_directories(eos_rust_abi_host_test PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(eos_rust_abi_host_test PRIVATE
        EOS_RUST_HOST_TEST=1 EOS_RUST_PORT_SOURCE="${EOS_RUST_PORT_SOURCE}")
    target_link_libraries(eos_rust_abi_host_test PRIVATE Threads::Threads)

    add_executable(alloc_test tests/alloc_test.cpp)
    set_target_properties(alloc_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_link_libraries(alloc_test PRIVATE eos_rust_abi_host_test)
    add_test(NAME alloc COMMAND alloc_test)

    add_executable(runtime_env_test tests/runtime_env_test.cpp)
    set_target_properties(runtime_env_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_link_libraries(runtime_env_test PRIVATE eos_rust_abi_host_test Threads::Threads)
    add_test(NAME runtime_env COMMAND runtime_env_test)

    add_executable(hash_seed_test tests/hash_seed_test.cpp)
    set_target_properties(hash_seed_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_compile_definitions(hash_seed_test PRIVATE EOS_RUST_HOST_TEST=1)
    target_link_libraries(hash_seed_test PRIVATE
        eos_rust_abi_host_test
        Threads::Threads
    )
    add_test(NAME hash_seed COMMAND hash_seed_test)

    add_test(
        NAME port_config
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/port_config_test.py"
            --cmake "${CMAKE_COMMAND}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME exports
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports.py"
            --nm "${CMAKE_NM}"
            --archive "$<TARGET_FILE:eos_rust_abi>"
            --expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/expected-exports-v1.txt"
    )

    add_test(
        NAME exports_checker_contract
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports_test.py"
            --python "${Python3_EXECUTABLE}"
            --checker "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports.py"
    )
endif()
