# Run one compiler command and compare its stdout and/or stderr against
# checked-in expectation files.
#
# CTest already knows how to run commands, but this wrapper gives us a reusable
# "golden file" pattern: every frontend inspection mode can say "this input must
# produce exactly this text." When the output intentionally changes, update the
# corresponding file under tests/golden/.
foreach(required_var NEXC_EXECUTABLE MODE INPUT EXPECTED_EXIT_CODE)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunGolden.cmake missing -D${required_var}=...")
    endif()
endforeach()

if(NOT DEFINED WORKING_DIRECTORY)
    set(WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

execute_process(
    COMMAND "${NEXC_EXECUTABLE}" "${MODE}" "${INPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE actual_exit_code
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
)

if(NOT actual_exit_code EQUAL EXPECTED_EXIT_CODE)
    message(FATAL_ERROR
        "golden command exited with ${actual_exit_code}, expected ${EXPECTED_EXIT_CODE}\n"
        "command: ${NEXC_EXECUTABLE} ${MODE} ${INPUT}\n"
        "stderr:\n${actual_stderr}"
    )
endif()

if(DEFINED EXPECTED)
    file(READ "${EXPECTED}" expected_stdout)

    if(NOT actual_stdout STREQUAL expected_stdout)
        message(FATAL_ERROR
            "golden stdout mismatch\n"
            "command: ${NEXC_EXECUTABLE} ${MODE} ${INPUT}\n"
            "expected file: ${EXPECTED}\n"
            "expected:\n${expected_stdout}\n"
            "actual:\n${actual_stdout}"
        )
    endif()
endif()

if(DEFINED EXPECTED_STDERR)
    file(READ "${EXPECTED_STDERR}" expected_stderr)

    if(NOT actual_stderr STREQUAL expected_stderr)
        message(FATAL_ERROR
            "golden stderr mismatch\n"
            "command: ${NEXC_EXECUTABLE} ${MODE} ${INPUT}\n"
            "expected file: ${EXPECTED_STDERR}\n"
            "expected:\n${expected_stderr}\n"
            "actual:\n${actual_stderr}"
        )
    endif()
endif()
