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

if(EOS_RUST_PORT STREQUAL "host")
    find_package(Threads REQUIRED)
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
if(EOS_RUST_PORT STREQUAL "host")
    target_link_libraries(eos_rust_abi PUBLIC Threads::Threads)
endif()
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
    include(CheckCXXSourceRuns)

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

    add_executable(fd_table_test tests/fd_table_test.cpp)
    set_target_properties(fd_table_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_compile_definitions(fd_table_test PRIVATE EOS_RUST_HOST_TEST=1)
    target_include_directories(fd_table_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(fd_table_test PRIVATE
        eos_rust_abi_host_test
        Threads::Threads
    )
    add_test(NAME fd_table COMMAND fd_table_test)

    add_executable(fd_race_test tests/fd_race_test.cpp)
    set_target_properties(fd_race_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_compile_definitions(fd_race_test PRIVATE EOS_RUST_HOST_TEST=1)
    target_include_directories(fd_race_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(fd_race_test PRIVATE
        eos_rust_abi_host_test
        Threads::Threads
    )
    add_test(NAME fd_race COMMAND fd_race_test)

    foreach(EOS_RUST_IO_TEST fs dir pipe_stdio)
        add_executable(${EOS_RUST_IO_TEST}_test
            tests/${EOS_RUST_IO_TEST}_test.cpp)
        set_target_properties(${EOS_RUST_IO_TEST}_test PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(${EOS_RUST_IO_TEST}_test PRIVATE
            EOS_RUST_HOST_TEST=1)
        target_include_directories(${EOS_RUST_IO_TEST}_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${EOS_RUST_IO_TEST}_test PRIVATE
            eos_rust_abi_host_test Threads::Threads)
        add_test(NAME ${EOS_RUST_IO_TEST} COMMAND ${EOS_RUST_IO_TEST}_test)
    endforeach()

    foreach(EOS_RUST_THREAD_TEST thread tls tls_destructor)
        add_executable(${EOS_RUST_THREAD_TEST}_test
            tests/${EOS_RUST_THREAD_TEST}_test.cpp)
        set_target_properties(${EOS_RUST_THREAD_TEST}_test PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(${EOS_RUST_THREAD_TEST}_test PRIVATE
            EOS_RUST_HOST_TEST=1)
        target_include_directories(${EOS_RUST_THREAD_TEST}_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${EOS_RUST_THREAD_TEST}_test PRIVATE
            eos_rust_abi_host_test Threads::Threads)
        add_test(NAME ${EOS_RUST_THREAD_TEST}
            COMMAND ${EOS_RUST_THREAD_TEST}_test)
    endforeach()

    foreach(EOS_RUST_SYNC_TIME_TEST sync time)
        add_executable(${EOS_RUST_SYNC_TIME_TEST}_test
            tests/${EOS_RUST_SYNC_TIME_TEST}_test.cpp)
        set_target_properties(${EOS_RUST_SYNC_TIME_TEST}_test PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(${EOS_RUST_SYNC_TIME_TEST}_test PRIVATE
            EOS_RUST_HOST_TEST=1)
        target_include_directories(${EOS_RUST_SYNC_TIME_TEST}_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${EOS_RUST_SYNC_TIME_TEST}_test PRIVATE
            eos_rust_abi_host_test Threads::Threads)
        add_test(NAME ${EOS_RUST_SYNC_TIME_TEST}
            COMMAND ${EOS_RUST_SYNC_TIME_TEST}_test)
    endforeach()

    foreach(EOS_RUST_MARTOS_FS_CASE rmdir trunc rename)
        add_executable(martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract_test
            tests/martos_fs_contract_test.c)
        set_target_properties(martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract_test
            PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        target_compile_definitions(
            martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract_test PRIVATE
            EOS_RUST_MARTOS_FS_CASE_${EOS_RUST_MARTOS_FS_CASE}=1)
        target_include_directories(
            martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/ports/martos_14_0_39")
        add_test(NAME martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract
            COMMAND martos_fs_${EOS_RUST_MARTOS_FS_CASE}_contract_test)
    endforeach()

    add_executable(martos_thread_contract_test
        tests/martos_thread_contract_test.c)
    set_target_properties(martos_thread_contract_test PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_include_directories(martos_thread_contract_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ports/martos_14_0_39")
    add_test(NAME martos_thread_contract COMMAND martos_thread_contract_test)

    add_executable(martos_sync_contract_test
        tests/martos_sync_contract_test.c)
    set_target_properties(martos_sync_contract_test PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_include_directories(martos_sync_contract_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ports/martos_14_0_39")
    add_test(NAME martos_sync_contract COMMAND martos_sync_contract_test)

    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
    check_cxx_source_runs(
        "#include <thread>\nint main() { std::thread t([] {}); t.join(); return 0; }"
        EOS_RUST_TSAN_AVAILABLE
    )
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(EOS_RUST_TSAN_AVAILABLE)
        add_library(eos_rust_abi_host_tsan STATIC src/eos_abi.c)
        set_target_properties(eos_rust_abi_host_tsan PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
            C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE YES)
        target_include_directories(eos_rust_abi_host_tsan PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(eos_rust_abi_host_tsan PRIVATE
            EOS_RUST_HOST_TEST=1 EOS_RUST_PORT_SOURCE="${EOS_RUST_PORT_SOURCE}")
        target_compile_options(eos_rust_abi_host_tsan PRIVATE -fsanitize=thread)
        target_link_options(eos_rust_abi_host_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(eos_rust_abi_host_tsan PRIVATE Threads::Threads)

        add_executable(fd_race_tsan tests/fd_race_test.cpp)
        set_target_properties(fd_race_tsan PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(fd_race_tsan PRIVATE EOS_RUST_HOST_TEST=1)
        target_include_directories(fd_race_tsan PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(fd_race_tsan PRIVATE -fsanitize=thread)
        target_link_options(fd_race_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(fd_race_tsan PRIVATE
            eos_rust_abi_host_tsan Threads::Threads)
        add_test(NAME fd_race_tsan COMMAND fd_race_tsan)

        add_executable(pipe_stdio_tsan tests/pipe_stdio_test.cpp)
        set_target_properties(pipe_stdio_tsan PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(pipe_stdio_tsan PRIVATE EOS_RUST_HOST_TEST=1)
        target_include_directories(pipe_stdio_tsan PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(pipe_stdio_tsan PRIVATE -fsanitize=thread)
        target_link_options(pipe_stdio_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(pipe_stdio_tsan PRIVATE
            eos_rust_abi_host_tsan Threads::Threads)
        add_test(NAME pipe_stdio_tsan COMMAND pipe_stdio_tsan)

        add_executable(fs_tsan tests/fs_test.cpp)
        set_target_properties(fs_tsan PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(fs_tsan PRIVATE EOS_RUST_HOST_TEST=1)
        target_include_directories(fs_tsan PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(fs_tsan PRIVATE -fsanitize=thread)
        target_link_options(fs_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(fs_tsan PRIVATE
            eos_rust_abi_host_tsan Threads::Threads)
        add_test(NAME fs_tsan COMMAND fs_tsan)

        add_executable(dir_tsan tests/dir_test.cpp)
        set_target_properties(dir_tsan PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(dir_tsan PRIVATE EOS_RUST_HOST_TEST=1)
        target_include_directories(dir_tsan PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(dir_tsan PRIVATE -fsanitize=thread)
        target_link_options(dir_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(dir_tsan PRIVATE
            eos_rust_abi_host_tsan Threads::Threads)
        add_test(NAME dir_tsan COMMAND dir_tsan)

        foreach(EOS_RUST_THREAD_RACE thread tls tls_destructor)
            add_executable(${EOS_RUST_THREAD_RACE}_tsan
                tests/${EOS_RUST_THREAD_RACE}_test.cpp)
            set_target_properties(${EOS_RUST_THREAD_RACE}_tsan PROPERTIES
                CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
            target_compile_definitions(${EOS_RUST_THREAD_RACE}_tsan PRIVATE
                EOS_RUST_HOST_TEST=1 EOS_RUST_TSAN_TEST=1)
            target_include_directories(${EOS_RUST_THREAD_RACE}_tsan PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src")
            target_compile_options(${EOS_RUST_THREAD_RACE}_tsan PRIVATE
                -fsanitize=thread)
            target_link_options(${EOS_RUST_THREAD_RACE}_tsan PRIVATE
                -fsanitize=thread)
            target_link_libraries(${EOS_RUST_THREAD_RACE}_tsan PRIVATE
                eos_rust_abi_host_tsan Threads::Threads)
            add_test(NAME ${EOS_RUST_THREAD_RACE}_tsan
                COMMAND ${EOS_RUST_THREAD_RACE}_tsan)
        endforeach()

        add_executable(sync_tsan tests/sync_test.cpp)
        set_target_properties(sync_tsan PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        target_compile_definitions(sync_tsan PRIVATE
            EOS_RUST_HOST_TEST=1 EOS_RUST_TSAN_TEST=1)
        target_include_directories(sync_tsan PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(sync_tsan PRIVATE -fsanitize=thread)
        target_link_options(sync_tsan PRIVATE -fsanitize=thread)
        target_link_libraries(sync_tsan PRIVATE
            eos_rust_abi_host_tsan Threads::Threads)
        add_test(NAME sync_tsan COMMAND sync_tsan)
    else()
        message(STATUS
            "ThreadSanitizer compile/runtime probe failed; fd_race remains enabled without TSan")
    endif()

    add_test(
        NAME port_config
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/port_config_test.py"
            --cmake "${CMAKE_COMMAND}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME host_consumer
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/host_consumer_test.py"
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
