cmake_minimum_required(VERSION 3.20)

project(nexc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NEXC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(nexc_target_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )

        if(NEXC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)

        if(NEXC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

add_library(nexc_frontend
    src/frontend/ast.cpp
    src/frontend/diagnostic.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/semantic.cpp
    src/frontend/source.cpp
    src/frontend/token.cpp
)

target_include_directories(nexc_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

nexc_target_warnings(nexc_frontend)

add_library(nexc_ir
    src/ir/builder.cpp
    src/ir/dump.cpp
    src/ir/ir.cpp
)

target_include_directories(nexc_ir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexc_ir PUBLIC nexc_frontend)
nexc_target_warnings(nexc_ir)

add_executable(nexc src/tools/nexc/main.cpp)
target_link_libraries(nexc PRIVATE nexc_ir)
nexc_target_warnings(nexc)

enable_testing()

function(add_nexc_golden_test name mode input expected)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DMODE=${mode}
            -DINPUT=${input}
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${expected}
            -DEXPECTED_EXIT_CODE=0
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunGolden.cmake
    )
endfunction()

function(add_nexc_golden_diagnostic_test name mode input expected_stderr)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DMODE=${mode}
            -DINPUT=${input}
            -DEXPECTED_STDERR=${CMAKE_CURRENT_SOURCE_DIR}/${expected_stderr}
            -DEXPECTED_EXIT_CODE=1
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunGolden.cmake
    )
endfunction()

add_test(
    NAME dump_tokens_smoke
    COMMAND nexc --dump-tokens ${CMAKE_CURRENT_SOURCE_DIR}/examples/minimal.nexs
)

add_nexc_golden_test(
    dump_tokens_minimal_golden
    --dump-tokens
    examples/minimal.nexs
    tests/golden/dump_tokens/minimal.tokens.txt
)

add_test(
    NAME dump_ast_smoke
    COMMAND nexc --dump-ast ${CMAKE_CURRENT_SOURCE_DIR}/examples/add.nexs
)

add_nexc_golden_test(
    dump_ast_add_golden
    --dump-ast
    examples/add.nexs
    tests/golden/dump_ast/add.ast.txt
)

add_nexc_golden_test(
    dump_ast_control_flow_golden
    --dump-ast
    examples/control_flow.nexs
    tests/golden/dump_ast/control_flow.ast.txt
)

add_nexc_golden_test(
    dump_ast_dot_add_golden
    --dump-ast-dot
    examples/add.nexs
    tests/golden/dump_ast_dot/add.dot
)

add_nexc_golden_test(
    dump_ir_add_golden
    --dump-ir
    examples/add.nexs
    tests/golden/dump_ir/add.ir.txt
)

add_nexc_golden_test(
    dump_ir_control_flow_golden
    --dump-ir
    examples/control_flow.nexs
    tests/golden/dump_ir/control_flow.ir.txt
)

add_nexc_golden_test(
    dump_ir_hello_golden
    --dump-ir
    examples/hello.nexs
    tests/golden/dump_ir/hello.ir.txt
)

add_nexc_golden_test(
    dump_ir_if_else_returns_golden
    --dump-ir
    tests/semantic_if_else_returns.nexs
    tests/golden/dump_ir/if_else_returns.ir.txt
)

add_test(
    NAME semantic_check_add
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/add.nexs
)

add_test(
    NAME semantic_check_control_flow
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/control_flow.nexs
)

add_test(
    NAME semantic_check_hello
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.nexs
)

add_test(
    NAME semantic_check_if_else_returns
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_if_else_returns.nexs
)

add_test(
    NAME invalid_trailing_comma
    COMMAND nexc --dump-ast ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_trailing_comma.nexs
)
set_tests_properties(invalid_trailing_comma PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME invalid_inner_const
    COMMAND nexc --dump-ast ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_inner_const.nexs
)
set_tests_properties(invalid_inner_const PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME invalid_expression_statement
    COMMAND nexc --dump-ast ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_expression_statement.nexs
)
set_tests_properties(invalid_expression_statement PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_return_type_mismatch
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_return_type_mismatch.nexs
)
set_tests_properties(semantic_return_type_mismatch PROPERTIES WILL_FAIL TRUE)

add_nexc_golden_diagnostic_test(
    semantic_return_type_mismatch_golden
    --check
    tests/semantic_return_type_mismatch.nexs
    tests/golden/diagnostics/semantic_return_type_mismatch.stderr.txt
)

add_test(
    NAME semantic_undefined_name
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_undefined_name.nexs
)
set_tests_properties(semantic_undefined_name PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_immutable_assignment
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_immutable_assignment.nexs
)
set_tests_properties(semantic_immutable_assignment PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_discard_non_void_call
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_discard_non_void_call.nexs
)
set_tests_properties(semantic_discard_non_void_call PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_bad_main
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_bad_main.nexs
)
set_tests_properties(semantic_bad_main PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_literal_out_of_range
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_literal_out_of_range.nexs
)
set_tests_properties(semantic_literal_out_of_range PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME semantic_missing_return_path
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_missing_return_path.nexs
)
set_tests_properties(semantic_missing_return_path PROPERTIES WILL_FAIL TRUE)

add_nexc_golden_diagnostic_test(
    semantic_missing_return_path_golden
    --check
    tests/semantic_missing_return_path.nexs
    tests/golden/diagnostics/semantic_missing_return_path.stderr.txt
)

add_test(
    NAME semantic_const_overflow
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_const_overflow.nexs
)
set_tests_properties(semantic_const_overflow PROPERTIES WILL_FAIL TRUE)

add_nexc_golden_diagnostic_test(
    semantic_const_overflow_golden
    --check
    tests/semantic_const_overflow.nexs
    tests/golden/diagnostics/semantic_const_overflow.stderr.txt
)

add_test(
    NAME semantic_const_division_by_zero
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_const_division_by_zero.nexs
)
set_tests_properties(semantic_const_division_by_zero PROPERTIES WILL_FAIL TRUE)

add_nexc_golden_diagnostic_test(
    semantic_const_division_by_zero_golden
    --check
    tests/semantic_const_division_by_zero.nexs
    tests/golden/diagnostics/semantic_const_division_by_zero.stderr.txt
)

add_test(
    NAME semantic_print_wrong_type
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_print_wrong_type.nexs
)
set_tests_properties(semantic_print_wrong_type PROPERTIES WILL_FAIL TRUE)
