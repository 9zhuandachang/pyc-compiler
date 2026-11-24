cmake_minimum_required(VERSION 3.16)
project(pyc_compiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Core compiler library so both CLI and test suite can link against it
add_library(pyc_core STATIC
    src/common.c
    src/scanner.c
    src/ast.c
    src/parser.c
    src/symbol_table.c
    src/analyzer.c
    src/bytecode.c
    src/vm.c
)

target_include_directories(pyc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_definitions(pyc_core PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pyc_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(pyc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Main CLI executable
add_executable(pyc src/main.c)
target_link_libraries(pyc PRIVATE pyc_core)
if (MSVC)
    target_compile_options(pyc PRIVATE /W4 /utf-8)
else()
    target_compile_options(pyc PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Test suite
enable_testing()
add_executable(pyc_tests tests/test_runner.c)
target_link_libraries(pyc_tests PRIVATE pyc_core)
if (MSVC)
    target_compile_options(pyc_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(pyc_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME pyc_unit_and_integration_tests COMMAND pyc_tests)
