cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not provided")
endif()

string(CONCAT legacy_compact "lite" "ime")
string(CONCAT legacy_hyphen "lite" "-" "ime")
set(legacy_pattern "(${legacy_compact}|${legacy_hyphen})")

execute_process(
    COMMAND git ls-files
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE paths_result
    OUTPUT_VARIABLE tracked_paths
    ERROR_VARIABLE paths_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT paths_result EQUAL 0)
    message(FATAL_ERROR "Unable to list tracked files: ${paths_error}")
endif()

string(REPLACE "\r\n" "\n" tracked_paths "${tracked_paths}")
string(REPLACE "\r" "\n" tracked_paths "${tracked_paths}")
string(REPLACE "\n" ";" tracked_path_list "${tracked_paths}")
foreach(tracked_path IN LISTS tracked_path_list)
    string(REPLACE "\\" "/" normalized_path "${tracked_path}")
    if(normalized_path MATCHES "^(build|dist|\\.git)(/|$)")
        continue()
    endif()

    string(TOLOWER "${normalized_path}" normalized_path_lower)
    if(normalized_path_lower MATCHES "${legacy_pattern}")
        message(FATAL_ERROR "Legacy brand remains in tracked path: ${tracked_path}")
    endif()
endforeach()

execute_process(
    COMMAND git grep -I -n -i -E -e "${legacy_pattern}" -- . ":(exclude)build/**" ":(exclude)dist/**" ":(exclude).git/**"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE grep_result
    OUTPUT_VARIABLE grep_output
    ERROR_VARIABLE grep_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(grep_result EQUAL 0)
    message(FATAL_ERROR "Legacy brand remains in tracked text:\n${grep_output}")
elseif(NOT grep_result EQUAL 1)
    message(FATAL_ERROR "Unable to scan tracked text: ${grep_error}")
endif()

message(STATUS "PiInput brand regression check passed")
