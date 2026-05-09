# Generate MLIR for one nex input and validate it with mlir-opt.
#
# Golden tests answer "did the text change?" This helper answers a different
# question: "does MLIR itself accept the generated module as structurally valid?"
# Keeping those checks separate gives better failures when one breaks.
foreach(required_var NEXC_EXECUTABLE INPUT MLIR_OPT_EXECUTABLE OUTPUT)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunMlirVerify.cmake missing -D${required_var}=...")
    endif()
endforeach()

if(NOT DEFINED WORKING_DIRECTORY)
    set(WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

execute_process(
    COMMAND "${NEXC_EXECUTABLE}" --dump-mlir "${INPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE nexc_exit_code
    OUTPUT_VARIABLE generated_mlir
    ERROR_VARIABLE nexc_stderr
)

if(NOT nexc_exit_code EQUAL 0)
    message(FATAL_ERROR
        "nexc failed while generating MLIR for validation\n"
        "command: ${NEXC_EXECUTABLE} --dump-mlir ${INPUT}\n"
        "stderr:\n${nexc_stderr}"
    )
endif()

file(WRITE "${OUTPUT}" "${generated_mlir}")

execute_process(
    COMMAND "${MLIR_OPT_EXECUTABLE}" --verify-diagnostics "${OUTPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE mlir_opt_exit_code
    OUTPUT_VARIABLE mlir_opt_stdout
    ERROR_VARIABLE mlir_opt_stderr
)

if(NOT mlir_opt_exit_code EQUAL 0)
    message(FATAL_ERROR
        "mlir-opt rejected generated MLIR\n"
        "input: ${INPUT}\n"
        "generated MLIR file: ${OUTPUT}\n"
        "stdout:\n${mlir_opt_stdout}\n"
        "stderr:\n${mlir_opt_stderr}"
    )
endif()
