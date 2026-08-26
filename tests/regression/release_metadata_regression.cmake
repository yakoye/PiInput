cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not provided")
endif()

# VERSION is the single source of truth for the release number. Everything else
# is checked against it, so cutting a release means editing VERSION and the
# release notes, never this gate.
file(READ "${SOURCE_DIR}/VERSION" version_text)
string(STRIP "${version_text}" version_text)
if(NOT version_text MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.]+)?$")
    message(FATAL_ERROR "VERSION must name a release like 1.2.3 or 1.2.3-dev: ${version_text}")
endif()
string(REPLACE "." "\\." version_pattern "${version_text}")

file(READ "${SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${SOURCE_DIR}/README.md" readme_text)
file(READ "${SOURCE_DIR}/PROJECT_CONTEXT.md" context_text)
file(READ "${SOURCE_DIR}/scripts/package_release.ps1" package_text)
file(READ "${SOURCE_DIR}/scripts/windows/package-release.ps1" windows_package_text)
if(NOT windows_package_text MATCHES "三组文本与八向边界对照测试_2026-08-15\\.md")
    message(FATAL_ERROR "Windows release package must include the three-corpus and eight-boundary comparison report")
endif()
file(READ "${SOURCE_DIR}/build.ps1" build_text)

if(NOT cmake_text MATCHES "project\\(PiInput VERSION ${version_pattern} LANGUAGES CXX\\)")
    message(FATAL_ERROR "CMake project version must be ${version_text}")
endif()
if(NOT readme_text MATCHES "当前发布版本：`v${version_pattern}`")
    message(FATAL_ERROR "README current release version is stale, expected v${version_text}")
endif()
# The section title is written by hand each release; only its presence is a gate.
if(NOT context_text MATCHES "\n## [0-9]+\\.[0-9]+ v${version_pattern} [^\r\n]+")
    message(FATAL_ERROR "PROJECT_CONTEXT is missing the v${version_text} implementation section")
endif()
if(NOT package_text MATCHES "Get-Content .*VERSION" OR NOT package_text MATCHES "piinput-v\\$Version\\.zip")
    message(FATAL_ERROR "Release package default must derive piinput-v<version>.zip from VERSION")
endif()
string(FIND "${windows_package_text}" "BaseVersion =" base_version_position)
string(FIND "${windows_package_text}" "-replace '-dev$', ''" base_version_suffix_position)
string(FIND "${windows_package_text}" "安装、使用与测试.md" quick_guide_position)
if(base_version_position LESS 0 OR base_version_suffix_position LESS 0 OR quick_guide_position LESS 0)
    message(FATAL_ERROR "Windows package script must derive the numeric Chinese quick-guide version")
endif()
if(NOT cmake_text MATCHES "PIINPUT_YESYMBOL_DIR" OR
   NOT cmake_text MATCHES "yesymbol\\.exe" OR
   NOT windows_package_text MATCHES "bin/yesymbol\\.exe" OR
   NOT windows_package_text MATCHES "bin/licenses/YeSymbol/THIRD_PARTY_NOTICES\\.md")
    message(FATAL_ERROR "Windows release must require the pinned YeSymbol runtime and its notices")
endif()
if(NOT cmake_text MATCHES "third_party/yetool/LICENSE" OR
   NOT windows_package_text MATCHES "bin/licenses/YeTool/LICENSE")
    message(FATAL_ERROR "Windows release must retain the MIT notice for adapted YeTool templates")
endif()
if(NOT build_text MATCHES "PiInput-Install\\.exe")
    message(FATAL_ERROR "Build artifact verification must require PiInput-Install.exe")
endif()
if(NOT build_text MATCHES "PiInput-Uninstall\\.exe")
    message(FATAL_ERROR "Build artifact verification must require PiInput-Uninstall.exe")
endif()
if(NOT build_text MATCHES "legacyCompact" OR NOT build_text MATCHES "Legacy Release artifact")
    message(FATAL_ERROR "Build artifact verification must reject legacy Release files")
endif()

message(STATUS "Release metadata regression check passed")
