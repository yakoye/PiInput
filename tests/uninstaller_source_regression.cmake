if(NOT DEFINED PIINPUT_SOURCE_DIR)
    message(FATAL_ERROR "PIINPUT_SOURCE_DIR was not provided")
endif()

set(uninstaller "${PIINPUT_SOURCE_DIR}/platform/windows/uninstaller/main.cpp")
if(NOT EXISTS "${uninstaller}")
    message(FATAL_ERROR "PiInput-Uninstall.exe source is missing")
endif()

file(READ "${uninstaller}" source_text)
file(READ "${PIINPUT_SOURCE_DIR}/CMakeLists.txt" cmake_text)

foreach(required IN ITEMS
    "TaskDialogIndirect"
    "同时删除用户词库、设置和学习记录"
    "verification_checked = FALSE"
    "--worker"
    "--wait-pid"
    "--remove-user-data"
    "--silent"
    "CopyFileW"
    "OpenProcess"
    "SYNCHRONIZE"
    "--disable-user"
    "--deactivate"
    "DllUnregisterServer"
    "remove_or_schedule_legacy_runtime"
    "delete_uninstall_registry")
    if(NOT source_text MATCHES "${required}")
        message(FATAL_ERROR "Native uninstaller is missing required behavior: ${required}")
    endif()
endforeach()

if(source_text MATCHES "TerminateProcess" OR source_text MATCHES "taskkill")
    message(FATAL_ERROR "Uninstaller must not terminate applications that may have loaded the TSF DLL")
endif()

string(FIND "${source_text}" "DllUnregisterServer" unregister_position)
string(FIND "${source_text}" "for (const auto& root : uninstall_roots" delete_position)
if(unregister_position LESS 0 OR delete_position LESS 0 OR
   delete_position LESS unregister_position)
    message(FATAL_ERROR "Runtime deletion must occur only after DLL/Profile unregistration")
endif()

if(NOT cmake_text MATCHES "add_executable\\(PiInput-Uninstall WIN32" OR
   NOT cmake_text MATCHES "target_link_libraries\\(PiInput-Uninstall PRIVATE[^\\)]*comctl32" OR
   NOT cmake_text MATCHES "MANIFESTUAC:level='requireAdministrator'" OR
   NOT cmake_text MATCHES "Microsoft.Windows.Common-Controls" OR
   NOT cmake_text MATCHES "version='6.0.0.0'")
    message(FATAL_ERROR "PiInput-Uninstall must be a native elevated Windows GUI target")
endif()

message(STATUS "PiInput native uninstaller source regression passed")
