file(READ "${SOURCE_DIR}/scripts/update-dictionaries.ps1" update_script)
file(READ "${SOURCE_DIR}/scripts/build-dictionaries.ps1" build_script)
file(READ "${SOURCE_DIR}/update-dictionaries.cmd" command_script)

foreach(required_text
    "Join-Path (Split-Path -Parent $RepoRoot) \"dicts\""
    "existing cache was preserved"
    "dictionary-build-manifest.json")
    string(FIND "${update_script}${build_script}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Dictionary update safety marker is missing: ${required_text}")
    endif()
endforeach()

string(FIND "${command_script}" "ExecutionPolicy Bypass" command_position)
if(command_position EQUAL -1)
    message(FATAL_ERROR "Double-click dictionary command does not invoke PowerShell safely")
endif()
