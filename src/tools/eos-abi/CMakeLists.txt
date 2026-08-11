cmake_minimum_required(VERSION 3.20)

project(eos_rust_abi VERSION 1.0.0 LANGUAGES C)

include(CTest)

set(EOS_RUST_PORT "host" CACHE STRING "EOS Rust ABI native port")
set_property(CACHE EOS_RUST_PORT PROPERTY STRINGS host martos_14_0_39)

if(EOS_RUST_PORT STREQUAL "host")
    set(EOS_RUST_PORT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/ports/host/eos_port_host.c")
elseif(EOS_RUST_PORT STREQUAL "martos_14_0_39")
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

if(EOS_RUST_PORT STREQUAL "martos_14_0_39")
    set(MARTOS_INCLUDE_DIR "" CACHE PATH
        "Directory containing the MARTOS-SMP 14.0.39 martos_smp.h header")
    if(NOT MARTOS_INCLUDE_DIR)
        message(FATAL_ERROR
            "MARTOS_INCLUDE_DIR is required for EOS_RUST_PORT=martos_14_0_39")
    endif()
    target_include_directories(eos_rust_abi PRIVATE "${MARTOS_INCLUDE_DIR}")
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
    target_compile_definitions(error_map_test PRIVATE EOS_RUST_TESTING=1)
    target_include_directories(error_map_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    add_test(NAME error_map COMMAND error_map_test)

    add_test(
        NAME exports
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports.py"
            --nm "${CMAKE_NM}"
            --archive "$<TARGET_FILE:eos_rust_abi>"
            --expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/expected-exports-v1.txt"
    )
endif()
