cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not provided")
endif()
if(NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "TEST_BINARY_DIR was not provided")
endif()

find_program(GIT_EXECUTABLE NAMES git REQUIRED)

string(CONCAT legacy_compact "lite" "ime")
string(CONCAT legacy_hyphen "lite" "-" "ime")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef fixture_suffix)
set(fixture_root "${TEST_BINARY_DIR}/fixture-${fixture_suffix}")

file(MAKE_DIRECTORY
    "${fixture_root}/build/nested"
    "${fixture_root}/dist/nested"
    "${fixture_root}/src"
)
file(WRITE "${fixture_root}/build/nested/${legacy_compact}.txt" "generated\n")
file(WRITE "${fixture_root}/dist/nested/${legacy_hyphen}.txt" "generated\n")
file(WRITE "${fixture_root}/src/clean.txt" "PiInput\n")

execute_process(
    COMMAND "${GIT_EXECUTABLE}" init --quiet
    WORKING_DIRECTORY "${fixture_root}"
    RESULT_VARIABLE init_result
    ERROR_VARIABLE init_error
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "Unable to initialize fixture repository: ${init_error}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" add -- build dist src
    WORKING_DIRECTORY "${fixture_root}"
    RESULT_VARIABLE add_result
    ERROR_VARIABLE add_error
)
if(NOT add_result EQUAL 0)
    message(FATAL_ERROR "Unable to stage fixture paths: ${add_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${fixture_root}
        -P ${SOURCE_DIR}/tests/brand_regression.cmake
    RESULT_VARIABLE gate_result
    OUTPUT_VARIABLE gate_output
    ERROR_VARIABLE gate_error
)
if(NOT gate_result EQUAL 0)
    message(FATAL_ERROR
        "Brand gate rejected generated tracked paths\n"
        "stdout:\n${gate_output}\n"
        "stderr:\n${gate_error}"
    )
endif()

message(STATUS "Brand gate generated-path filtering passed")
