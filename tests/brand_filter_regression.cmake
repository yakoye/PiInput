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
string(CONCAT legacy_underscore "lite" "_" "ime")
string(CONCAT legacy_clsid "84E21A77" "-3A42-4D7B-93B8-BCDF818FC414")
string(CONCAT legacy_profile "A99F4C36" "-EA4E-4457-AE7A-861804AC7439")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef fixture_suffix)
set(matrix_root "${TEST_BINARY_DIR}/matrix-${fixture_suffix}")
file(MAKE_DIRECTORY "${matrix_root}")
file(WRITE "${matrix_root}/empty.gitconfig" "")
set(ENV{GIT_CONFIG_NOSYSTEM} "1")
set(ENV{GIT_CONFIG_GLOBAL} "${matrix_root}/empty.gitconfig")

function(init_fixture_repository fixture_root)
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
        COMMAND "${GIT_EXECUTABLE}" config core.quotePath true
        WORKING_DIRECTORY "${fixture_root}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endfunction()

function(stage_fixture fixture_root)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" add -f -- .
        WORKING_DIRECTORY "${fixture_root}"
        RESULT_VARIABLE add_result
        ERROR_VARIABLE add_error
    )
    if(NOT add_result EQUAL 0)
        message(FATAL_ERROR "Unable to stage fixture paths: ${add_error}")
    endif()
endfunction()

function(run_gate label fixture_root expected_result)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${fixture_root}
            -P ${SOURCE_DIR}/tests/brand_regression.cmake
        RESULT_VARIABLE gate_result
        OUTPUT_VARIABLE gate_output
        ERROR_VARIABLE gate_error
    )
    set(gate_log "${gate_output}\n${gate_error}")
    if(expected_result STREQUAL "PASS")
        if(NOT gate_result EQUAL 0)
            message(FATAL_ERROR "${label}: expected PASS\n${gate_log}")
        endif()
    elseif(expected_result STREQUAL "REJECT")
        if(gate_result EQUAL 0 OR NOT gate_log MATCHES "Legacy brand")
            message(FATAL_ERROR "${label}: expected legacy-brand rejection\n${gate_log}")
        endif()
    else()
        message(FATAL_ERROR "Unknown expected result: ${expected_result}")
    endif()
endfunction()

set(generated_root "${matrix_root}/generated")
file(MAKE_DIRECTORY "${generated_root}/build" "${generated_root}/dist" "${generated_root}/src")
file(WRITE "${generated_root}/build/clean.txt" "${legacy_compact}\n")
file(WRITE "${generated_root}/dist/clean.txt" "${legacy_hyphen}\n")
file(WRITE "${generated_root}/src/中文;clean.txt" "PiInput\n")
init_fixture_repository("${generated_root}")
file(WRITE "${generated_root}/.git/ignored-brand.txt" "${legacy_underscore}\n")
stage_fixture("${generated_root}")
run_gate("generated directories and special paths" "${generated_root}" "PASS")

set(untracked_root "${matrix_root}/untracked")
file(MAKE_DIRECTORY "${untracked_root}/src")
file(WRITE "${untracked_root}/src/probe.txt" "${legacy_compact}\n")
init_fixture_repository("${untracked_root}")
run_gate("untracked source content" "${untracked_root}" "REJECT")

set(archive_root "${matrix_root}/archive")
file(MAKE_DIRECTORY "${archive_root}/src")
file(WRITE "${archive_root}/src/clean.txt" "PiInput\n")
run_gate("source archive without git metadata" "${archive_root}" "PASS")

set(root_special_clean "${matrix_root}/root-special-clean")
file(MAKE_DIRECTORY "${root_special_clean}")
file(WRITE "${root_special_clean}/中文;clean.txt" "PiInput\n")
run_gate("clean root semicolon path" "${root_special_clean}" "PASS")

set(root_special_dirty "${matrix_root}/root-special-dirty")
file(MAKE_DIRECTORY "${root_special_dirty}")
file(WRITE "${root_special_dirty}/${legacy_compact};probe.txt" "${legacy_compact}\n")
run_gate("legacy root semicolon path" "${root_special_dirty}" "REJECT")

set(detection_values
    "compact|${legacy_compact}"
    "hyphen|${legacy_hyphen}"
    "underscore|${legacy_underscore}"
    "clsid|${legacy_clsid}"
    "profile|${legacy_profile}"
)
foreach(detection_case IN LISTS detection_values)
    string(REPLACE "|" ";" detection_parts "${detection_case}")
    list(GET detection_parts 0 detection_label)
    list(GET detection_parts 1 detection_value)
    set(detection_root "${matrix_root}/detect-${detection_label}")
    file(MAKE_DIRECTORY "${detection_root}/src")
    file(WRITE "${detection_root}/src/probe.txt" "${detection_value}\n")
    run_gate("detect ${detection_label}" "${detection_root}" "REJECT")
endforeach()

message(STATUS "Brand gate regression matrix passed")
