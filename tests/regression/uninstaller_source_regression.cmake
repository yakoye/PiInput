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
    "disable_user_keyboard"
    "deactivate_profile"
    "unregister_machine_tsf"
    "unregister_user_tsf"
    "--machine-unregister"
    "process_is_elevated"
    "unregister_machine_profile_current_process"
    "request_host_drain"
    "HostMessageType::drain"
    "host_mutex, 10000U"
    "remove_or_schedule_legacy_runtime"
    "delete_uninstall_registry"
    "topmost_task_dialog_callback"
    "MB_TOPMOST"
    "MB_SETFOREGROUND")
    if(NOT source_text MATCHES "${required}")
        message(FATAL_ERROR "Native uninstaller is missing required behavior: ${required}")
    endif()
endforeach()

# 卸载器不得终止加载过 TSF DLL 的应用程序——那是用户的编辑器、浏览器和资源
# 管理器，它们只是碰巧把输入法 DLL 映射了进来。这条不能松。
#
# PiInputHost.exe 是另一回事：它是 PiInput 自己的常驻进程，不是「加载了 DLL
# 的应用」。原先只发 drain 请求、等 3 秒、然后不管结果继续走，于是实测卸载
# 之后它还活着，拿着控制管道和一个已被改名的可执行文件——下一次安装起不来
# Host，只能手工 kill。
#
# 所以允许终止，但只允许这一种，且必须按映像路径限定在正被卸载的安装目录内：
# 另一个账户可能登录着自己的 Host，那不是这次卸载该碰的东西。
if(source_text MATCHES "taskkill")
    message(FATAL_ERROR "Uninstaller must not shell out to taskkill")
endif()
if(source_text MATCHES "TerminateProcess")
    if(NOT source_text MATCHES "_wcsicmp\\(entry\\.szExeFile, L\"PiInputHost\\.exe\"\\)")
        message(FATAL_ERROR
            "Uninstaller may terminate only PiInputHost.exe, never an application that loaded the TSF DLL")
    endif()
    if(NOT source_text MATCHES "inside && TerminateProcess")
        message(FATAL_ERROR
            "Terminating a Host must be scoped by image path to the installation being removed")
    endif()
endif()
if(NOT source_text MATCHES "if \\(!arguments\\.silent\\)")
    message(FATAL_ERROR "Silent uninstall failures must return an error without opening a hidden dialog")
endif()
if(source_text MATCHES "LoadLibraryExW" OR source_text MATCHES "run_hidden" OR
   NOT source_text MATCHES "lpVerb = L\"runas\"")
    message(FATAL_ERROR "Uninstall must elevate only direct machine TSF cleanup and must never load product binaries from its temporary worker")
endif()

string(FIND "${source_text}" "auto problems = unregister_user_tsf();" unregister_position)
string(FIND "${source_text}" "for (const auto& root : uninstall_roots" delete_position)
if(unregister_position LESS 0 OR delete_position LESS 0 OR
   delete_position LESS unregister_position)
    message(FATAL_ERROR "Runtime deletion must occur only after DLL/Profile unregistration")
endif()

# Uninstalling and immediately installing again is the normal upgrade flow.
# PendingFileRenameOperations records paths, not files, so queueing a stable
# path that the installer is about to rewrite makes the next restart delete the
# freshly installed file and leave the registry pointing at an empty directory:
# the profile stays listed and switchable while every attempt to load it fails.
# A file that cannot be deleted now must therefore be renamed to a path
# belonging to this uninstall alone before it is queued.
set(migration "${PIINPUT_SOURCE_DIR}/platform/windows/installer/migration.cpp")
if(NOT EXISTS "${migration}")
    message(FATAL_ERROR "Installer migration source is missing")
endif()
file(READ "${migration}" migration_text)

foreach(required IN ITEMS
    "doomed_path_for"
    "GetSystemTimeAsFileTime"
    "MoveFileExW\\(path\\.c_str\\(\\), doomed\\.c_str\\(\\), MOVEFILE_REPLACE_EXISTING\\)"
    "MoveFileExW\\(doomed\\.c_str\\(\\), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT\\)")
    if(NOT migration_text MATCHES "${required}")
        message(FATAL_ERROR
            "A locked file must be renamed to a single-use path before it is queued for "
            "deletion, so a reinstall to the same path survives the next restart: ${required}")
    endif()
endforeach()

string(FIND "${migration_text}"
    "MoveFileExW(path.c_str(), doomed.c_str(), MOVEFILE_REPLACE_EXISTING)" rename_position)
string(FIND "${migration_text}"
    "MoveFileExW(doomed.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)" queue_position)
if(rename_position LESS 0 OR queue_position LESS 0 OR queue_position LESS rename_position)
    message(FATAL_ERROR
        "A locked file must leave its stable path before that path could be queued for deletion")
endif()

if(migration_text MATCHES "GetTickCount64\\(\\)")
    message(FATAL_ERROR
        "Single-use deletion names must not come from GetTickCount64: it restarts at zero "
        "after a reboot, so a name minted before one can collide with a name minted after")
endif()

if(NOT cmake_text MATCHES "add_executable\\(PiInput-Uninstall WIN32" OR
   NOT cmake_text MATCHES "target_link_libraries\\(PiInput-Uninstall PRIVATE[^\\)]*comctl32" OR
   NOT cmake_text MATCHES "MANIFESTUAC:level='asInvoker'" OR
   NOT cmake_text MATCHES "Microsoft.Windows.Common-Controls" OR
   NOT cmake_text MATCHES "version='6.0.0.0'")
    message(FATAL_ERROR "PiInput-Uninstall must be a native per-user Windows GUI target")
endif()

message(STATUS "PiInput native uninstaller source regression passed")
