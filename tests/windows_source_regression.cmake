if(NOT DEFINED PIINPUT_SOURCE_DIR)
    message(FATAL_ERROR "PIINPUT_SOURCE_DIR was not provided")
endif()

file(READ "${PIINPUT_SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/profile_tool.cpp" profile_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/profile_registration.h" registration_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/dllmain.cpp" dllmain_text)
file(READ "${PIINPUT_SOURCE_DIR}/repair-registration.ps1" repair_text)
file(READ "${PIINPUT_SOURCE_DIR}/refresh-installed-dev.ps1" refresh_text)
file(READ "${PIINPUT_SOURCE_DIR}/uninstall-dev.ps1" uninstall_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/windows/resolve-installed-dev.ps1" resolver_text)
file(READ "${PIINPUT_SOURCE_DIR}/install-dev.ps1" install_text)
file(READ "${PIINPUT_SOURCE_DIR}/set-candidate-page-size.ps1" candidate_settings_script_text)
file(READ "${PIINPUT_SOURCE_DIR}/setup-dev.ps1" setup_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/text_service.cpp" text_service_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/text_service.h" text_service_header_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/candidate_window.cpp" candidate_window_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/candidate_window.h" candidate_window_header_text)

if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/installer/main.cpp")
    message(FATAL_ERROR "Native PiInput installer source is missing")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/main.cpp" installer_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/install_layout.cpp" installer_layout_text)

foreach(settings_writer IN ITEMS candidate_settings_script_text install_text installer_text)
    if("${${settings_writer}}" MATCHES
        "single_syllable_page_size=[0-9]|phrase_page_size=[0-9]")
        message(FATAL_ERROR "Runtime settings writers must not emit obsolete paging keys")
    endif()
endforeach()
if(NOT candidate_settings_script_text MATCHES "ItemsPerRow" OR
   NOT candidate_settings_script_text MATCHES "VisibleRows" OR
   NOT candidate_settings_script_text MATCHES "MaxItems" OR
   NOT candidate_settings_script_text MATCHES "\\[candidates\\]")
    message(FATAL_ERROR "Candidate settings script must write the modern candidate grid section")
endif()
if(candidate_settings_script_text MATCHES "Set-Content[^\r\n]*-Encoding ASCII" OR
   install_text MATCHES "Set-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding ASCII")
    message(FATAL_ERROR "Settings writers must preserve non-ASCII INI content with UTF-8")
endif()
if(NOT candidate_settings_script_text MATCHES "Get-Content[^\r\n]*-Encoding UTF8" OR
   NOT candidate_settings_script_text MATCHES "Set-Content[^\r\n]*-Encoding UTF8" OR
   NOT install_text MATCHES "Get-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding UTF8" OR
   NOT install_text MATCHES "Set-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding UTF8")
    message(FATAL_ERROR "Settings writers must explicitly read and write UTF-8")
endif()
if(NOT installer_text MATCHES "\\[general\\]" OR
   NOT installer_text MATCHES "\\[candidates\\]" OR
   NOT installer_text MATCHES "items_per_row=6" OR
   NOT installer_text MATCHES "visible_rows=3" OR
   NOT installer_text MATCHES "max_items=90")
    message(FATAL_ERROR "Native installer defaults must use the modern settings format")
endif()
if(NOT install_text MATCHES "\\[candidates\\]" OR
   NOT install_text MATCHES "items_per_row" OR
   NOT install_text MATCHES "visible_rows" OR
   NOT install_text MATCHES "max_items")
    message(FATAL_ERROR "Developer installer must ensure modern candidate defaults")
endif()
if(NOT profile_text MATCHES "\\[general\\]")
    message(FATAL_ERROR "Profile schema writer must use the modern [general] section")
endif()

if(cmake_text MATCHES "target_link_libraries\\(PiInputTSF[^\\)]*msctf")
    message(FATAL_ERROR "PiInputTSF must not link a non-existent msctf import library")
endif()

if(cmake_text MATCHES "target_link_libraries\\(piinput-profile[^\\)]*msctf")
    message(FATAL_ERROR "piinput-profile must not link a non-existent msctf import library")
endif()

if(profile_text MATCHES "CLSID_TF_InputProcessorProfileMgr")
    message(FATAL_ERROR "CLSID_TF_InputProcessorProfileMgr is not a Windows SDK COM class")
endif()

if(NOT registration_text MATCHES "CLSID_TF_InputProcessorProfiles")
    message(FATAL_ERROR "Profile manager creation must use CLSID_TF_InputProcessorProfiles")
endif()

if(NOT registration_text MATCHES "QueryInterface\\(IID_PPV_ARGS\\(manager\\)\\)")
    message(FATAL_ERROR "Profile manager should be obtained by querying CLSID_TF_InputProcessorProfiles")
endif()

if(NOT registration_text MATCHES "RegisterProfile\\(")
    message(FATAL_ERROR "Modern TSF registration must use ITfInputProcessorProfileMgr::RegisterProfile")
endif()

if(NOT registration_text MATCHES "TRUE,[\r\n ]*0U\\);")
    message(FATAL_ERROR "PiInput profile must be enabled by default and visible in Settings")
endif()

if(NOT profile_text MATCHES "--status")
    message(FATAL_ERROR "Profile tool must expose a machine-readable status command")
endif()

if(NOT profile_text MATCHES "result == S_FALSE")
    message(FATAL_ERROR "Idempotent deactivate/unregister must accept an absent profile")
endif()

if(NOT dllmain_text MATCHES "register_profile\\(")
    message(FATAL_ERROR "DllRegisterServer must use the shared profile registration helper")
endif()

if(NOT repair_text MATCHES "Invoke-NativeBestEffort")
    message(FATAL_ERROR "Repair script must tolerate an absent or inactive previous profile")
endif()

if(NOT repair_text MATCHES "--register")
    message(FATAL_ERROR "Repair script must explicitly register the profile before activation")
endif()

if(NOT repair_text MATCHES "--status")
    message(FATAL_ERROR "Repair script must verify profile registration state")
endif()

foreach(installed_script IN ITEMS repair_text refresh_text uninstall_text)
    if(NOT "${${installed_script}}" MATCHES "resolve-installed-dev\\.ps1" OR
       NOT "${${installed_script}}" MATCHES "Resolve-PiInputInstalledDev")
        message(FATAL_ERROR "Installed-development script does not use the shared active-version resolver")
    endif()
endforeach()
if(NOT resolver_text MATCHES "current\\.txt" OR NOT resolver_text MATCHES "InprocServer32" OR
   NOT resolver_text MATCHES "versions")
    message(FATAL_ERROR "Active-version resolver must validate current.txt and COM registration under versions")
endif()
if(refresh_text MATCHES "Stop-Process" OR NOT refresh_text MATCHES "PiInput-Install\\.exe")
    message(FATAL_ERROR "Refresh must use the side-by-side installer without force-closing applications")
endif()
if(NOT uninstall_text MATCHES "Invoke-NativeRequired" OR
   uninstall_text MATCHES "Active PiInput version could not be resolved")
    message(FATAL_ERROR "Uninstall must preserve the runtime when resolution or unregistration fails")
endif()

if(NOT install_text MATCHES "Previous profile deactivation")
    message(FATAL_ERROR "Installer must treat previous profile cleanup as best effort")
endif()

if(text_service_text MATCHES "kPageSize = 5")
    message(FATAL_ERROR "TSF candidate paging must not return to the fixed five-item layout")
endif()

if(NOT text_service_text MATCHES "VK_OEM_MINUS" OR NOT text_service_text MATCHES "VK_OEM_PLUS")
    message(FATAL_ERROR "TSF must support minus/equal candidate paging")
endif()

if(NOT text_service_text MATCHES "toggle_input_mode")
    message(FATAL_ERROR "TSF must support standalone Shift input-mode switching")
endif()

if(NOT text_service_text MATCHES "punctuation_\\.transform")
    message(FATAL_ERROR "TSF Chinese input mode must route printable punctuation through PunctuationTransformer")
endif()

if(NOT candidate_window_text MATCHES "const int column" OR NOT candidate_window_text MATCHES "item_width")
    message(FATAL_ERROR "Candidate window must use horizontal column layout")
endif()

if(NOT text_service_header_text MATCHES "CandidateGrid candidate_grid_")
    message(FATAL_ERROR "TSF must own the shared CandidateGrid state")
endif()
if(NOT text_service_header_text MATCHES "SettingsManager" OR
   NOT text_service_header_text MATCHES "settings_manager_")
    message(FATAL_ERROR "TSF must own the shared SettingsManager")
endif()
if(NOT text_service_header_text MATCHES "SettingsPollThrottle settings_poll_throttle_" OR
   NOT text_service_text MATCHES "settings_poll_throttle_\\.should_poll" OR
   NOT text_service_text MATCHES "SettingsPollThrottle::Clock::now")
    message(FATAL_ERROR "TSF settings file polling must be throttled with steady_clock")
endif()
if(NOT text_service_text MATCHES "SettingsManager>\\(data_root / L\"settings\\.ini\"\\)" OR
   NOT text_service_text MATCHES "settings_manager_->poll\\(\\)" OR
   NOT text_service_text MATCHES "settings_manager_->apply_pending_at_composition_boundary\\(\\)" OR
   NOT text_service_text MATCHES "apply_settings_at_composition_boundary")
    message(FATAL_ERROR "TSF must poll and apply settings only at composition boundaries")
endif()
if(text_service_header_text MATCHES "page_start_" OR
   text_service_text MATCHES "move_candidate_page" OR
   text_service_text MATCHES "current_page_size")
    message(FATAL_ERROR "TSF must not retain the legacy candidate page state")
endif()
if(NOT text_service_text MATCHES "candidate_grid_\\.move_row" OR
   NOT text_service_text MATCHES "candidate_grid_\\.candidate_index_for_digit")
    message(FATAL_ERROR "TSF row navigation and digit selection must use CandidateGrid")
endif()
if(NOT text_service_header_text MATCHES "EnglishLexicon" OR
   NOT text_service_header_text MATCHES "EnglishSession" OR
   NOT text_service_text MATCHES "settings_\\.english\\.enabled" OR
   NOT text_service_text MATCHES "english_lexicon\\.tsv" OR
   NOT text_service_text MATCHES "english_downloaded\\.tsv" OR
   NOT text_service_text MATCHES "english_user\\.tsv" OR
   NOT text_service_text MATCHES "english_learning\\.tsv")
    message(FATAL_ERROR "TSF must own and lazily initialize the optional local English resources")
endif()
if(NOT text_service_text MATCHES "load_builtin_tsv\\(english_downloaded_path_\\)")
    message(FATAL_ERROR "TSF must load the downloaded English TSV as an optional built-in source")
endif()
if(NOT text_service_text MATCHES "settings_\\.english\\.items_per_row")
    message(FATAL_ERROR "TSF must apply the English-only candidate column count")
endif()
if(NOT text_service_text MATCHES "english_session_->set_candidate_limit")
    message(FATAL_ERROR "TSF must refresh the English session when max_items changes")
endif()
if(NOT text_service_text MATCHES "map_composition_caret" OR
   NOT text_service_text MATCHES "range->ShiftEnd")
    message(FATAL_ERROR "TSF must map the internal Composition caret into the host selection")
endif()
if(NOT text_service_header_text MATCHES "english_key_policy\\.h" OR
   NOT text_service_text MATCHES "EnglishKeyPolicy::decide")
    message(FATAL_ERROR "TSF must use the tested EnglishKeyPolicy decision layer")
endif()
if(NOT text_service_text MATCHES "classify_english_ascii_key" OR
   NOT text_service_text MATCHES "build_english_commit_plan" OR
   NOT text_service_text MATCHES "edit_session_succeeded")
    message(FATAL_ERROR "TSF must use the tested English key, commit, and edit-session helpers")
endif()
if(NOT text_service_text MATCHES "result = selection\\.range->Collapse")
    message(FATAL_ERROR "TSF must stop when the initial selection cannot be collapsed safely")
endif()
if(NOT candidate_window_header_text MATCHES "items_per_row" OR
   NOT candidate_window_header_text MATCHES "visible_rows" OR
   NOT candidate_window_text MATCHES "actual_visible_rows")
    message(FATAL_ERROR "Candidate window must render the configured multi-row grid")
endif()
if(NOT candidate_window_text MATCHES "fit_candidate_column_widths" OR
   NOT candidate_window_text MATCHES "maximum_width = \\(std::max\\)\\(1, work_width - 16\\)" OR
   NOT candidate_window_text MATCHES "maximum_height = \\(std::max\\)\\(1, work_height - 16\\)")
    message(FATAL_ERROR "Candidate window must keep narrow-work-area geometry non-negative")
endif()

if(NOT cmake_text MATCHES "add_executable\\(PiInput-Install")
    message(FATAL_ERROR "CMake must build PiInput-Install.exe")
endif()
if(NOT installer_layout_text MATCHES "versions")
    message(FATAL_ERROR "Installer must use side-by-side version directories")
endif()
if(NOT installer_text MATCHES "current_marker_value\\(version_root\\)" OR
   installer_text MATCHES "output << version_root\\.wstring\\(\\)")
    message(FATAL_ERROR "Installer current.txt must contain only the active version directory name")
endif()
if(installer_text MATCHES "TerminateProcess" OR installer_text MATCHES "Stop-Process")
    message(FATAL_ERROR "Installer must not force-close user applications")
endif()
if(installer_text MATCHES "Dev[/\\\\]bin[/\\\\]PiInputTSF\\.dll")
    message(FATAL_ERROR "Installer must not overwrite the legacy fixed TSF DLL path")
endif()
if(NOT installer_text MATCHES "--silent")
    message(FATAL_ERROR "Installer must support silent integration verification")
endif()
if(NOT installer_text MATCHES "ERROR_PROC_NOT_FOUND")
    message(FATAL_ERROR "Missing DllRegisterServer must report ERROR_PROC_NOT_FOUND instead of a stale last-error value")
endif()
if(NOT installer_text MATCHES "DllUnregisterServer")
    message(FATAL_ERROR "A failed first installation must best-effort unregister a partially created TSF profile")
endif()
if(NOT setup_text MATCHES "PiInput-Install\\.exe" OR NOT setup_text MATCHES "--silent")
    message(FATAL_ERROR "setup-dev.ps1 must use the native side-by-side installer")
endif()

message(STATUS "Windows TSF source regression checks passed")
