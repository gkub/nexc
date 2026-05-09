# Compile one nex input to a native executable, run it, and check its behavior.
#
# This is the first test helper for the "real compiler" path:
#
#   nexc input.nexs -o output
#   ./output
#
# Earlier tests inspect intermediate text. This one proves the compiler driver
# can hand LLVM IR to clang, produce a host executable, and run that executable.
# Most early tests only need an exit code, but stdout is now observable too, so
# EXPECTED_STDOUT can be supplied when a program should print exact bytes.
foreach(required_var NEXC_EXECUTABLE INPUT OUTPUT EXPECTED_RUN_EXIT_CODE)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunExecutable.cmake missing -D${required_var}=...")
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

if(NOT compile_exit_code EQUAL 0)
    message(FATAL_ERROR
        "nexc failed while compiling executable\n"
        "command: ${NEXC_EXECUTABLE} ${INPUT} -o ${OUTPUT}\n"
        "stdout:\n${compile_stdout}\n"
        "stderr:\n${compile_stderr}"
    )
endif()

if(DEFINED STDIN)
    set(stdin_file "${OUTPUT}.stdin")
    file(WRITE "${stdin_file}" "${STDIN}")
    execute_process(
        COMMAND "${OUTPUT}"
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        INPUT_FILE "${stdin_file}"
        RESULT_VARIABLE run_exit_code
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
else()
    execute_process(
        COMMAND "${OUTPUT}"
        WORKING_DIRECTORY "${WORKING_DIRECTORY}"
        RESULT_VARIABLE run_exit_code
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
    )
endif()

if(NOT run_exit_code EQUAL EXPECTED_RUN_EXIT_CODE)
    message(FATAL_ERROR
        "compiled executable exited with ${run_exit_code}, expected ${EXPECTED_RUN_EXIT_CODE}\n"
        "input: ${INPUT}\n"
        "executable: ${OUTPUT}\n"
        "stdout:\n${run_stdout}\n"
        "stderr:\n${run_stderr}"
    )
endif()

if(DEFINED EXPECTED_STDOUT AND NOT run_stdout STREQUAL EXPECTED_STDOUT)
    message(FATAL_ERROR
        "compiled executable stdout mismatch\n"
        "input: ${INPUT}\n"
        "executable: ${OUTPUT}\n"
        "expected stdout:\n${EXPECTED_STDOUT}\n"
        "actual stdout:\n${run_stdout}\n"
        "stderr:\n${run_stderr}"
    )
endif()
