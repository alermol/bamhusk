cmake_minimum_required(VERSION 3.28)

project(
    bamhusk
    VERSION 0.1.0
    DESCRIPTION "Reduce a BAM to the fields downstream analysis needs"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(HUSK_STATIC "Link htslib and its dependencies statically" OFF)
option(HUSK_FULLY_STATIC "Pass -static to the husk executable where supported" OFF)
option(HUSK_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(HUSK_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(HUSK_GIT_SHA "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _husk_git_result
        OUTPUT_VARIABLE _husk_git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_husk_git_result EQUAL 0 AND _husk_git_sha)
        set(HUSK_GIT_SHA "${_husk_git_sha}")
    endif()
endif()

# Source tarballs created with `git archive` have no .git directory. The
# exported GIT_SHA file carries the commit through export-subst.
if(HUSK_GIT_SHA STREQUAL "unknown"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/GIT_SHA")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/GIT_SHA" _husk_source_sha)
    string(STRIP "${_husk_source_sha}" _husk_source_sha)
    string(LENGTH "${_husk_source_sha}" _husk_source_sha_length)
    if(_husk_source_sha_length GREATER 6
       AND _husk_source_sha_length LESS 41
       AND _husk_source_sha MATCHES "^[0-9a-fA-F]+$")
        set(HUSK_GIT_SHA "${_husk_source_sha}")
    endif()
endif()

configure_file(
    src/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp"
    @ONLY
)

if(HUSK_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(HTSlib 1.21 REQUIRED)

add_library(
    husk_lib
    STATIC
    src/cli.cpp
    src/header.cpp
    src/bam_slim.cpp
    src/platform.cpp
)
target_include_directories(
    husk_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(husk_lib PUBLIC HTSlib::htslib)

function(husk_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

husk_set_warnings(husk_lib)

add_executable(husk src/main.cpp)
target_link_libraries(husk PRIVATE husk_lib)
husk_set_warnings(husk)

if(HUSK_FULLY_STATIC AND NOT APPLE)
    target_link_options(husk PRIVATE -static)
endif()

install(TARGETS husk RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(
        husk_tests
        tests/unit/test_main.cpp
        tests/unit/test_cli.cpp
        tests/unit/test_header.cpp
        tests/unit/test_record.cpp
        tests/unit/test_bam_slim.cpp
    )
    target_link_libraries(husk_tests PRIVATE husk_lib)
    target_include_directories(
        husk_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit"
    )
    husk_set_warnings(husk_tests)

    add_test(NAME unit COMMAND husk_tests)
    add_test(NAME help COMMAND husk --help)
    set_tests_properties(help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: husk")

    add_test(
        NAME integration
        COMMAND
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run.sh"
            "$<TARGET_FILE:husk>"
    )
    set_tests_properties(integration PROPERTIES SKIP_RETURN_CODE 77)

    add_test(
        NAME manifest
        COMMAND
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/manifest.sh"
    )
    set_tests_properties(manifest PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(HUSK_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HUSK_BUILD_FUZZERS requires Clang")
    endif()
    foreach(fuzzer fuzz_cli fuzz_header fuzz_bam)
        add_executable(${fuzzer} "tests/fuzz/${fuzzer}.cpp")
        target_link_libraries(${fuzzer} PRIVATE husk_lib)
        target_compile_options(
            ${fuzzer}
            PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
        )
        target_link_options(
            ${fuzzer}
            PRIVATE -fsanitize=fuzzer,address,undefined
        )
    endforeach()
endif()
