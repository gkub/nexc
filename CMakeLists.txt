cmake_minimum_required(VERSION 3.20)

project(nexc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NEXC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

find_package(MLIR CONFIG
    HINTS
        ENV MLIR_DIR
        /usr/lib/llvm-18/lib/cmake/mlir
)

if(MLIR_FOUND)
    message(STATUS "Found MLIR ${MLIR_VERSION}: ${MLIR_DIR}")

    find_program(NEXC_MLIR_OPT_EXECUTABLE
        NAMES mlir-opt
        HINTS
            ${LLVM_TOOLS_BINARY_DIR}
            /usr/lib/llvm-18/bin
    )

    if(NEXC_MLIR_OPT_EXECUTABLE)
        message(STATUS "Found mlir-opt: ${NEXC_MLIR_OPT_EXECUTABLE}")
    else()
        message(STATUS "mlir-opt not found; MLIR validation tests will be skipped")
    endif()

    find_program(NEXC_LLVM_AS_EXECUTABLE
        NAMES llvm-as llvm-as-18
        HINTS
            ${LLVM_TOOLS_BINARY_DIR}
            /usr/lib/llvm-18/bin
    )

    if(NEXC_LLVM_AS_EXECUTABLE)
        message(STATUS "Found llvm-as: ${NEXC_LLVM_AS_EXECUTABLE}")
    else()
        message(STATUS "llvm-as not found; LLVM IR validation tests will be skipped")
    endif()

endif()

# Native executable pipeline uses llc + ld.lld (no clang as linker driver).
set(NEXC_LLVM_TOOL_HINTS /usr/lib/llvm-18/bin)
if(DEFINED LLVM_TOOLS_BINARY_DIR)
    list(APPEND NEXC_LLVM_TOOL_HINTS "${LLVM_TOOLS_BINARY_DIR}")
endif()

set(NEXC_NATIVE_LINK_READY FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(NEXC_LLC_EXECUTABLE
        NAMES llc llc-18
        HINTS ${NEXC_LLVM_TOOL_HINTS}
    )
    find_program(NEXC_LD_LLD_EXECUTABLE
        NAMES ld.lld ld.lld-18
        HINTS ${NEXC_LLVM_TOOL_HINTS}
    )

    if(NEXC_LLC_EXECUTABLE AND NEXC_LD_LLD_EXECUTABLE)
        message(STATUS "Found llc: ${NEXC_LLC_EXECUTABLE}")
        message(STATUS "Found ld.lld: ${NEXC_LD_LLD_EXECUTABLE}")

        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=Scrt1.o
            OUTPUT_VARIABLE NEXC_SCRT1_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=crti.o
            OUTPUT_VARIABLE NEXC_CRTI_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtn.o
            OUTPUT_VARIABLE NEXC_CRTN_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=libc.so.6
            OUTPUT_VARIABLE NEXC_LIBC_SO_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        get_filename_component(NEXC_LIBDIR_FOR_LC "${NEXC_LIBC_SO_PATH}" DIRECTORY)

        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(NEXC_LINUX_DYNAMIC_LINKER "/lib64/ld-linux-x86-64.so.2")
            set(NEXC_NATIVE_LINK_READY TRUE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AArch64|ARM64)$")
            set(NEXC_LINUX_DYNAMIC_LINKER "/lib/ld-linux-aarch64.so.1")
            set(NEXC_NATIVE_LINK_READY TRUE)
        else()
            message(STATUS "Unsupported CMAKE_SYSTEM_PROCESSOR for lld link; native link disabled")
        endif()
    else()
        message(STATUS "llc and/or ld.lld not found; native executable tests will be skipped")
    endif()
endif()

if(NEXC_NATIVE_LINK_READY)
    set(NEXC_CONFIGURED_LINUX_LINK 1)
    set(NEXC_LLC_PATH "${NEXC_LLC_EXECUTABLE}")
    set(NEXC_LD_LLD_PATH "${NEXC_LD_LLD_EXECUTABLE}")
else()
    set(NEXC_CONFIGURED_LINUX_LINK 0)
    set(NEXC_LLC_PATH "")
    set(NEXC_LD_LLD_PATH "")
    set(NEXC_LINUX_DYNAMIC_LINKER "")
    set(NEXC_SCRT1_PATH "")
    set(NEXC_CRTI_PATH "")
    set(NEXC_CRTN_PATH "")
    set(NEXC_LIBDIR_FOR_LC "")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/link_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/nexc_link_config.h
    @ONLY
)

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
    src/frontend/string_literal_decode.cpp
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

add_library(nexc_mlir
    src/mlir/textual.cpp
)

target_include_directories(nexc_mlir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexc_mlir PUBLIC nexc_ir)
if(MLIR_FOUND)
    target_compile_definitions(nexc_mlir PRIVATE NEXC_HAS_REAL_MLIR=1)
    target_include_directories(nexc_mlir SYSTEM PRIVATE ${MLIR_INCLUDE_DIRS})
    target_link_libraries(nexc_mlir
        PRIVATE
            MLIRIR
            MLIRFuncDialect
            MLIRArithDialect
            MLIRSCFDialect
            MLIRMemRefDialect
            MLIRLLVMDialect
    )
endif()
nexc_target_warnings(nexc_mlir)

add_library(nexc_llvm
    src/llvm/textual.cpp
)

target_include_directories(nexc_llvm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexc_llvm PUBLIC nexc_mlir)
if(MLIR_FOUND)
    target_compile_definitions(nexc_llvm PRIVATE NEXC_HAS_REAL_MLIR=1)
    target_include_directories(nexc_llvm SYSTEM PRIVATE ${MLIR_INCLUDE_DIRS})
    target_link_libraries(nexc_llvm
        PRIVATE
            MLIRArithToLLVM
            MLIRControlFlowDialect
            MLIRControlFlowToLLVM
            MLIRFuncToLLVM
            MLIRLLVMDialect
            MLIRMemRefToLLVM
            MLIRReconcileUnrealizedCasts
            MLIRSCFToControlFlow
            MLIRTargetLLVMIRExport
            MLIRBuiltinToLLVMIRTranslation
            MLIRLLVMToLLVMIRTranslation
            LLVMCore
            LLVMSupport
    )
endif()
nexc_target_warnings(nexc_llvm)

add_library(nexc_runtime STATIC
    runtime/nex_runtime.c
)

set_target_properties(nexc_runtime
    PROPERTIES
        OUTPUT_NAME nexrt
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/runtime
)

nexc_target_warnings(nexc_runtime)

add_executable(nexc src/tools/nexc/main.cpp)
target_include_directories(nexc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(nexc PRIVATE nexc_llvm)
add_dependencies(nexc nexc_runtime)
target_compile_definitions(nexc
    PRIVATE
        NEXC_RUNTIME_LIBRARY_RELATIVE_PATH="runtime/$<TARGET_FILE_NAME:nexc_runtime>"
)
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

function(add_nexc_mlir_validation_test name input)
    if(NOT NEXC_MLIR_OPT_EXECUTABLE)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DMLIR_OPT_EXECUTABLE=${NEXC_MLIR_OPT_EXECUTABLE}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}.mlir
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunMlirVerify.cmake
    )
endfunction()

function(add_nexc_llvm_validation_test name input)
    if(NOT NEXC_LLVM_AS_EXECUTABLE)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DLLVM_AS_EXECUTABLE=${NEXC_LLVM_AS_EXECUTABLE}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}.ll
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunLlvmVerify.cmake
    )
endfunction()

function(add_nexc_executable_test name input expected_exit_code)
    if(NOT NEXC_NATIVE_LINK_READY)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}
            -DEXPECTED_RUN_EXIT_CODE=${expected_exit_code}
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutable.cmake
    )
endfunction()

function(add_nexc_executable_stdout_test name input expected_exit_code expected_stdout)
    if(NOT NEXC_NATIVE_LINK_READY)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}
            -DEXPECTED_RUN_EXIT_CODE=${expected_exit_code}
            -DEXPECTED_STDOUT=${expected_stdout}
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutable.cmake
    )
endfunction()

function(add_nexc_executable_stdin_stdout_test name input stdin expected_exit_code expected_stdout)
    if(NOT NEXC_NATIVE_LINK_READY)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}
            -DSTDIN=${stdin}
            -DEXPECTED_RUN_EXIT_CODE=${expected_exit_code}
            -DEXPECTED_STDOUT=${expected_stdout}
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutable.cmake
    )
endfunction()


function(add_nexc_compile_failure_test name input expected_stderr_contains)
    if(NOT NEXC_NATIVE_LINK_READY)
        return()
    endif()

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
            -DINPUT=${input}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${name}
            -DEXPECTED_STDERR_CONTAINS=${expected_stderr_contains}
            -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCompileFailure.cmake
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
    dump_ir_mutable_locals_golden
    --dump-ir
    examples/mutable_locals.nexs
    tests/golden/dump_ir/mutable_locals.ir.txt
)

add_nexc_golden_test(
    dump_ir_if_else_returns_golden
    --dump-ir
    tests/semantic_if_else_returns.nexs
    tests/golden/dump_ir/if_else_returns.ir.txt
)

add_nexc_golden_test(
    dump_mlir_return_42_golden
    --dump-mlir
    examples/return_42.nexs
    tests/golden/dump_mlir/return_42.mlir
)

add_nexc_golden_test(
    dump_mlir_scalar_expr_golden
    --dump-mlir
    examples/scalar_expr.nexs
    tests/golden/dump_mlir/scalar_expr.mlir
)

add_nexc_golden_test(
    dump_mlir_function_call_golden
    --dump-mlir
    examples/function_call.nexs
    tests/golden/dump_mlir/function_call.mlir
)

add_nexc_golden_test(
    dump_mlir_if_else_returns_golden
    --dump-mlir
    tests/semantic_if_else_returns.nexs
    tests/golden/dump_mlir/if_else_returns.mlir
)

add_nexc_golden_test(
    dump_mlir_comparison_golden
    --dump-mlir
    examples/comparison.nexs
    tests/golden/dump_mlir/comparison.mlir
)

add_nexc_golden_test(
    dump_mlir_mutable_locals_golden
    --dump-mlir
    examples/mutable_locals.nexs
    $<IF:$<BOOL:${MLIR_FOUND}>,tests/golden/dump_mlir/mutable_locals.mlir,tests/golden/dump_mlir_fallback/mutable_locals.mlir>
)

add_nexc_golden_test(
    dump_mlir_pipeline_walkthrough_golden
    --dump-mlir
    examples/pipeline_walkthrough.nexs
    $<IF:$<BOOL:${MLIR_FOUND}>,tests/golden/dump_mlir/pipeline_walkthrough.mlir,tests/golden/dump_mlir_fallback/pipeline_walkthrough.mlir>
)

if(MLIR_FOUND)
    add_nexc_golden_test(
        dump_llvm_return_42_golden
        --dump-llvm
        examples/return_42.nexs
        tests/golden/dump_llvm/return_42.ll
    )

    add_nexc_golden_test(
        dump_llvm_scalar_expr_golden
        --dump-llvm
        examples/scalar_expr.nexs
        tests/golden/dump_llvm/scalar_expr.ll
    )

    add_nexc_golden_test(
        dump_llvm_function_call_golden
        --dump-llvm
        examples/function_call.nexs
        tests/golden/dump_llvm/function_call.ll
    )

    add_nexc_golden_test(
        dump_llvm_mutable_locals_golden
        --dump-llvm
        examples/mutable_locals.nexs
        tests/golden/dump_llvm/mutable_locals.ll
    )

    add_nexc_golden_test(
        dump_llvm_pipeline_walkthrough_golden
        --dump-llvm
        examples/pipeline_walkthrough.nexs
        tests/golden/dump_llvm/pipeline_walkthrough.ll
    )
endif()

add_nexc_mlir_validation_test(
    validate_mlir_return_42
    examples/return_42.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_scalar_expr
    examples/scalar_expr.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_function_call
    examples/function_call.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_if_else_returns
    tests/semantic_if_else_returns.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_comparison
    examples/comparison.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_mutable_locals
    examples/mutable_locals.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_pipeline_walkthrough
    examples/pipeline_walkthrough.nexs
)

add_nexc_mlir_validation_test(
    validate_mlir_core_v0_backend_coverage
    examples/core_v0_backend_coverage.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_return_42
    examples/return_42.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_scalar_expr
    examples/scalar_expr.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_function_call
    examples/function_call.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_mutable_locals
    examples/mutable_locals.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_pipeline_walkthrough
    examples/pipeline_walkthrough.nexs
)

add_nexc_llvm_validation_test(
    validate_llvm_core_v0_backend_coverage
    examples/core_v0_backend_coverage.nexs
)

add_nexc_executable_test(
    run_executable_return_42
    examples/return_42.nexs
    42
)

add_nexc_executable_test(
    run_executable_pipeline_walkthrough
    examples/pipeline_walkthrough.nexs
    30
)

add_nexc_executable_test(
    run_executable_core_v0_backend_coverage
    examples/core_v0_backend_coverage.nexs
    33
)

add_test(
    NAME run_executable_multifile_merge
    COMMAND ${CMAKE_COMMAND}
        -DNEXC_EXECUTABLE=$<TARGET_FILE:nexc>
        -DINPUT_FIRST=${CMAKE_CURRENT_SOURCE_DIR}/examples/multifile_lib.nexs
        -DINPUT_SECOND=${CMAKE_CURRENT_SOURCE_DIR}/examples/multifile_main.nexs
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/multifile_merge_test.bin
        -DEXPECTED_RUN_EXIT_CODE=5
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableMerge.cmake
)

add_nexc_executable_test(
    run_executable_array_fixed
    examples/array_fixed.nexs
    60
)

add_nexc_executable_stdout_test(
    run_executable_hello_stdout
    examples/hello.nexs
    0
    "Hello, world!\n"
)

add_nexc_executable_stdout_test(
    run_executable_format_print_stdout
    examples/format_print.nexs
    0
    "x=42\n"
)

add_nexc_executable_stdin_stdout_test(
    run_executable_stdin_echo
    examples/stdin_echo.nexs
    "typed input\n"
    0
    "typed input\n"
)

add_nexc_executable_stdin_stdout_test(
    run_executable_stdin_parse_i32
    examples/stdin_parse_i32.nexs
    "42\n"
    42
    ""
)

add_test(
    NAME semantic_check_array_fixed
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/array_fixed.nexs
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
    NAME semantic_check_format_print
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/format_print.nexs
)

add_test(
    NAME semantic_check_mutable_locals
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/mutable_locals.nexs
)

add_test(
    NAME semantic_check_comparison
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/comparison.nexs
)

add_test(
    NAME semantic_check_pipeline_walkthrough
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/pipeline_walkthrough.nexs
)

add_test(
    NAME semantic_check_core_v0_backend_coverage
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/core_v0_backend_coverage.nexs
)

add_test(
    NAME semantic_check_stdin_echo
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/stdin_echo.nexs
)

add_test(
    NAME semantic_check_stdin_parse_i32
    COMMAND nexc --check ${CMAKE_CURRENT_SOURCE_DIR}/examples/stdin_parse_i32.nexs
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
