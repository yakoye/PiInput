cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not provided")
endif()

string(CONCAT legacy_compact "lite" "ime")
string(CONCAT legacy_hyphen "lite" "-" "ime")
string(CONCAT legacy_underscore "lite" "_" "ime")
string(CONCAT legacy_clsid "84e21a77" "-3a42-4d7b-93b8-bcdf818fc414")
string(CONCAT legacy_profile "a99f4c36" "-ea4e-4457-ae7a-861804ac7439")
set(legacy_needles
    "${legacy_compact}"
    "${legacy_hyphen}"
    "${legacy_underscore}"
    "${legacy_clsid}"
    "${legacy_profile}"
)

function(assert_clean_text label value)
    string(TOLOWER "${value}" lower_value)
    foreach(legacy_needle IN LISTS legacy_needles)
        string(FIND "${lower_value}" "${legacy_needle}" legacy_position)
        if(NOT legacy_position EQUAL -1)
            message(FATAL_ERROR "Legacy brand remains in ${label}")
        endif()
    endforeach()
endfunction()

file(GLOB root_entries
    LIST_DIRECTORIES true
    RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/*"
    "${SOURCE_DIR}/.*"
)
set(source_files)
foreach(root_entry IN LISTS root_entries)
    string(REPLACE "\\" "/" normalized_root_entry "${root_entry}")
    if(normalized_root_entry MATCHES "^(build|dist|\\.git)(/|$)")
        continue()
    endif()
    set(root_path "${SOURCE_DIR}/${root_entry}")
    if(IS_DIRECTORY "${root_path}")
        file(GLOB_RECURSE nested_files
            LIST_DIRECTORIES false
            RELATIVE "${SOURCE_DIR}"
            "${root_path}/*"
            "${root_path}/.*"
        )
        set(pending_nested_file "")
        foreach(nested_fragment IN LISTS nested_files)
            if(pending_nested_file STREQUAL "")
                set(nested_candidate "${nested_fragment}")
            else()
                set(nested_candidate "${pending_nested_file};${nested_fragment}")
            endif()
            if(EXISTS "${SOURCE_DIR}/${nested_candidate}")
                string(REPLACE ";" "\\;" escaped_nested_candidate "${nested_candidate}")
                list(APPEND source_files "${escaped_nested_candidate}")
                set(pending_nested_file "")
            else()
                set(pending_nested_file "${nested_candidate}")
            endif()
        endforeach()
        if(NOT pending_nested_file STREQUAL "")
            message(FATAL_ERROR "Unable to resolve source path: ${pending_nested_file}")
        endif()
    elseif(EXISTS "${root_path}")
        list(APPEND source_files "${root_entry}")
    endif()
endforeach()

foreach(source_file IN LISTS source_files)
    string(REPLACE "\\" "/" normalized_source_file "${source_file}")
    assert_clean_text("source path: ${normalized_source_file}" "${normalized_source_file}")
    file(STRINGS "${SOURCE_DIR}/${source_file}" source_lines ENCODING UTF-8)
    foreach(source_line IN LISTS source_lines)
        assert_clean_text("source text: ${normalized_source_file}" "${source_line}")
    endforeach()
endforeach()

find_program(GIT_EXECUTABLE NAMES git)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE repository_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(repository_result EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false ls-files -- .
                ":(exclude)build/**" ":(exclude)dist/**" ":(exclude).git/**"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            RESULT_VARIABLE paths_result
            OUTPUT_VARIABLE tracked_paths
            ERROR_VARIABLE paths_error
        )
        if(NOT paths_result EQUAL 0)
            message(FATAL_ERROR "Unable to list tracked files: ${paths_error}")
        endif()
        assert_clean_text("tracked paths" "${tracked_paths}")
    endif()
endif()

message(STATUS "PiInput brand regression check passed")
