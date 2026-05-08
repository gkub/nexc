cmake_minimum_required(VERSION 3.20)

project(nexc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(nexc_frontend
    src/frontend/ast.cpp
    src/frontend/diagnostic.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/source.cpp
    src/frontend/token.cpp
)

target_include_directories(nexc_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(nexc_frontend
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(nexc src/tools/nexc/main.cpp)
target_link_libraries(nexc PRIVATE nexc_frontend)
target_compile_options(nexc PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

function(add_nexc_golden_test name mode input expected)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DMODE=${mode}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input}
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${expected}
            -DEXPECTED_EXIT_CODE=0
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
