# Compile two nex sources as one merged translation unit, run the binary, check exit code.
foreach(required_var NEXC_EXECUTABLE INPUT_FIRST INPUT_SECOND OUTPUT EXPECTED_RUN_EXIT_CODE)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunExecutableMerge.cmake missing -D${required_var}=...")
    endif()
endforeach()

if(NOT DEFINED WORKING_DIRECTORY)
    set(WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

execute_process(
    COMMAND "${NEXC_EXECUTABLE}" "${INPUT_FIRST}" "${INPUT_SECOND}" -o "${OUTPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE compile_exit_code
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_exit_code EQUAL 0)
    message(FATAL_ERROR
        "nexc failed while compiling merged sources\n"
        "command: ${NEXC_EXECUTABLE} ${INPUT_FIRST} ${INPUT_SECOND} -o ${OUTPUT}\n"
        "stderr:\n${compile_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE run_exit_code
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)

if(NOT run_exit_code EQUAL EXPECTED_RUN_EXIT_CODE)
    message(FATAL_ERROR
        "compiled executable exited with ${run_exit_code}, expected ${EXPECTED_RUN_EXIT_CODE}\n"
        "executable: ${OUTPUT}\n"
        "stdout:\n${run_stdout}\n"
        "stderr:\n${run_stderr}")
endif()
