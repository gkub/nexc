# Generate LLVM IR for one nex input and validate it with llvm-as.
#
# Golden tests answer "did the printed LLVM text change?" This helper answers
# "does LLVM's own assembler accept the generated IR?" Keeping those checks
# separate makes failures easier to understand while the backend is young.
foreach(required_var NEXC_EXECUTABLE INPUT LLVM_AS_EXECUTABLE OUTPUT)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "RunLlvmVerify.cmake missing -D${required_var}=...")
    endif()
endforeach()

if(NOT DEFINED WORKING_DIRECTORY)
    set(WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

execute_process(
    COMMAND "${NEXC_EXECUTABLE}" --dump-llvm "${INPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE nexc_exit_code
    OUTPUT_VARIABLE generated_llvm
    ERROR_VARIABLE nexc_stderr
)

if(NOT nexc_exit_code EQUAL 0)
    message(FATAL_ERROR
        "nexc failed while generating LLVM IR for validation\n"
        "command: ${NEXC_EXECUTABLE} --dump-llvm ${INPUT}\n"
        "stderr:\n${nexc_stderr}"
    )
endif()

file(WRITE "${OUTPUT}" "${generated_llvm}")

execute_process(
    COMMAND "${LLVM_AS_EXECUTABLE}" "-o" "${OUTPUT}.bc" "${OUTPUT}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE llvm_as_exit_code
    OUTPUT_VARIABLE llvm_as_stdout
    ERROR_VARIABLE llvm_as_stderr
)

if(NOT llvm_as_exit_code EQUAL 0)
    message(FATAL_ERROR
        "llvm-as rejected generated LLVM IR\n"
        "input: ${INPUT}\n"
        "generated LLVM IR file: ${OUTPUT}\n"
        "stdout:\n${llvm_as_stdout}\n"
        "stderr:\n${llvm_as_stderr}"
    )
endif()
