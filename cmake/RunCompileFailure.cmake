# Run a native compile command that is expected to fail.
#
# This is used for friendly backend limitation tests. For example, `hello.nexs`
# parses and type-checks today, but native compilation should explain that
# strings/println need runtime lowering instead of exposing an internal compiler
# assertion or generic unsupported-operation error.
foreach(required_var NEXC_EXECUTABLE INPUT OUTPUT EXPECTED_STDERR_CONTAINS)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunCompileFailure.cmake missing -D${required_var}=...")
    endif()
endforeach()

if(NOT DEFINED WORKING_DIRECTORY)
    set(WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

execute_process(
    COMMAND "${NEXC_EXECUTABLE}" "${INPUT}" -o "${OUTPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE compile_exit_code
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(compile_exit_code EQUAL 0)
    message(FATAL_ERROR
        "compile command unexpectedly succeeded\n"
        "command: ${NEXC_EXECUTABLE} ${INPUT} -o ${OUTPUT}\n"
        "stdout:\n${compile_stdout}\n"
        "stderr:\n${compile_stderr}"
    )
endif()

string(FIND "${compile_stderr}" "${EXPECTED_STDERR_CONTAINS}" match_index)
if(match_index EQUAL -1)
    message(FATAL_ERROR
        "compile failure stderr did not contain expected text\n"
        "command: ${NEXC_EXECUTABLE} ${INPUT} -o ${OUTPUT}\n"
        "expected substring:\n${EXPECTED_STDERR_CONTAINS}\n"
        "actual stderr:\n${compile_stderr}"
    )
endif()
