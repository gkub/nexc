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

add_test(
    NAME dump_tokens_smoke
    COMMAND nexc --dump-tokens ${CMAKE_CURRENT_SOURCE_DIR}/examples/minimal.nexs
)

add_test(
    NAME dump_ast_smoke
    COMMAND nexc --dump-ast ${CMAKE_CURRENT_SOURCE_DIR}/examples/add.nexs
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
