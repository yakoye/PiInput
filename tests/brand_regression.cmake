cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not provided")
endif()

string(CONCAT legacy_compact "lite" "ime")
string(CONCAT legacy_hyphen "lite" "-" "ime")
string(CONCAT legacy_underscore "lite" "_" "ime")
set(legacy_needles
    "${legacy_compact}"
    "${legacy_hyphen}"
    "${legacy_underscore}"
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

function(resolve_glob_paths output_variable base_path glob_variable)
    set(resolved_paths)
    set(pending_path "")
    foreach(path_fragment IN LISTS ${glob_variable})
        if(pending_path STREQUAL "")
            set(path_candidate "${path_fragment}")
        else()
            set(path_candidate "${pending_path};${path_fragment}")
        endif()
        if(EXISTS "${base_path}/${path_candidate}")
            string(REPLACE ";" "\\;" escaped_path_candidate "${path_candidate}")
            list(APPEND resolved_paths "${escaped_path_candidate}")
            set(pending_path "")
        else()
            set(pending_path "${path_candidate}")
        endif()
    endforeach()
    if(NOT pending_path STREQUAL "")
        message(FATAL_ERROR "Unable to resolve source path: ${pending_path}")
    endif()
    set(${output_variable} "${resolved_paths}" PARENT_SCOPE)
endfunction()

set(source_files)
find_program(GIT_EXECUTABLE NAMES git)
set(is_repository false)
if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE repository_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(repository_result EQUAL 0)
        set(is_repository true)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false ls-files
                --cached --others --exclude-standard -- .
                ":(exclude)build/**" ":(exclude)build-*/**" ":(exclude)dist/**"
                ":(exclude)artifacts/**" ":(exclude).git/**"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            RESULT_VARIABLE paths_result
            OUTPUT_VARIABLE listed_paths
            ERROR_VARIABLE paths_error
        )
        if(NOT paths_result EQUAL 0)
            message(FATAL_ERROR "Unable to list source files: ${paths_error}")
        endif()
        string(REPLACE "\r\n" "\n" listed_paths "${listed_paths}")
        string(REPLACE "\n" ";" source_files "${listed_paths}")
        list(FILTER source_files EXCLUDE REGEX "^$")
        # An intentional tracked deletion still appears in `git ls-files`
        # until the release commit is created. It has no contents to scan and
        # must not cause every following path to be concatenated into one
        # unresolved semicolon list in a legitimate dirty release workspace.
        set(existing_source_files)
        foreach(source_file IN LISTS source_files)
            if(EXISTS "${SOURCE_DIR}/${source_file}")
                list(APPEND existing_source_files "${source_file}")
            endif()
        endforeach()
        set(source_files "${existing_source_files}")
        resolve_glob_paths(safe_source_files "${SOURCE_DIR}" source_files)
        set(source_files "${safe_source_files}")
    endif()
endif()

if(NOT is_repository)
    file(GLOB root_entries
        LIST_DIRECTORIES true
        RELATIVE "${SOURCE_DIR}"
        "${SOURCE_DIR}/*"
        "${SOURCE_DIR}/.*"
    )
    resolve_glob_paths(safe_root_entries "${SOURCE_DIR}" root_entries)
    foreach(root_entry IN LISTS safe_root_entries)
        string(REPLACE "\\" "/" normalized_root_entry "${root_entry}")
        if(normalized_root_entry MATCHES "^(build($|[-/])|dist($|/)|artifacts($|/)|\\.git($|/))")
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
            resolve_glob_paths(safe_nested_files "${SOURCE_DIR}" nested_files)
            foreach(safe_nested_file IN LISTS safe_nested_files)
                string(REPLACE ";" "\\;" escaped_safe_nested_file "${safe_nested_file}")
                list(APPEND source_files "${escaped_safe_nested_file}")
            endforeach()
        elseif(EXISTS "${root_path}")
            string(REPLACE ";" "\\;" escaped_root_entry "${root_entry}")
            list(APPEND source_files "${escaped_root_entry}")
        endif()
    endforeach()
endif()

foreach(source_file IN LISTS source_files)
    string(REPLACE "\\" "/" normalized_source_file "${source_file}")
    assert_clean_text("source path: ${normalized_source_file}" "${normalized_source_file}")
    file(STRINGS "${SOURCE_DIR}/${source_file}" source_lines ENCODING UTF-8)
    foreach(source_line IN LISTS source_lines)
        assert_clean_text("source text: ${normalized_source_file}" "${source_line}")
    endforeach()
endforeach()

message(STATUS "PiInput brand regression check passed")
