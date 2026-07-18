if(NOT DEFINED LITEIME_SOURCE_DIR)
    message(FATAL_ERROR "LITEIME_SOURCE_DIR was not provided")
endif()

file(READ "${LITEIME_SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${LITEIME_SOURCE_DIR}/platform/windows/tsf/profile_tool.cpp" profile_text)
file(READ "${LITEIME_SOURCE_DIR}/platform/windows/tsf/profile_registration.h" registration_text)
file(READ "${LITEIME_SOURCE_DIR}/platform/windows/tsf/dllmain.cpp" dllmain_text)
file(READ "${LITEIME_SOURCE_DIR}/repair-registration.ps1" repair_text)
file(READ "${LITEIME_SOURCE_DIR}/install-dev.ps1" install_text)

if(cmake_text MATCHES "target_link_libraries\\(LiteImeTSF[^\\)]*msctf")
    message(FATAL_ERROR "LiteImeTSF must not link a non-existent msctf import library")
endif()

if(cmake_text MATCHES "target_link_libraries\\(liteime-profile[^\\)]*msctf")
    message(FATAL_ERROR "liteime-profile must not link a non-existent msctf import library")
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
    message(FATAL_ERROR "LiteIME profile must be enabled by default and visible in Settings")
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

if(NOT install_text MATCHES "Previous profile deactivation")
    message(FATAL_ERROR "Installer must treat previous profile cleanup as best effort")
endif()

message(STATUS "Windows TSF source regression checks passed")
