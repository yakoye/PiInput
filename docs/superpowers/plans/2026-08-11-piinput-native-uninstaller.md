# PiInput Native Uninstaller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native, discoverable and fail-closed PiInput uninstaller that preserves user data by default and supports safe reinstall.

**Architecture:** A small pure `uninstall_layout` module owns path validation, active-version resolution, command quoting and uninstall registry metadata. `PiInput-Uninstall.exe` uses that module, performs unregister-before-delete, runs from a temporary worker copy, and schedules locked files for deletion without terminating applications. The existing installer copies the stable uninstaller and registers it in the current user's Windows uninstall list.

**Tech Stack:** C++20, Win32, TSF/COM, Task Dialog API, CMake, CTest, MSVC Release.

## Global Constraints

- All recursive deletion targets must resolve beneath `%LOCALAPPDATA%\PiInput`.
- The uninstaller must never terminate applications that loaded the TSF DLL.
- User dictionaries, settings and learning data are preserved unless the user explicitly selects removal.
- Profile or COM unregistration failure preserves the runtime and uninstall entry.
- Current work is built and packaged locally; do not push the repository and do not alter the user's live installation.

---

### Task 1: Pure uninstall layout and safety policy

**Files:**
- Create: `platform/windows/installer/uninstall_layout.h`
- Create: `platform/windows/installer/uninstall_layout.cpp`
- Create: `tests/uninstall_layout_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `UninstallLayout uninstall_layout(local_app_data, roaming_app_data)`.
- Produces: `bool validate_uninstall_layout(const UninstallLayout&)`.
- Produces: `std::optional<std::filesystem::path> resolve_active_version(const UninstallLayout&)`.
- Produces: `UninstallRegistryValues uninstall_registry_values(...)`.
- Produces: `std::wstring quote_windows_argument(std::wstring_view)`.

- [x] **Step 1: Write the failing filesystem tests**

Test literal paths under temporary roots, a valid `current.txt`, missing and traversal markers, default-preserve deletion roots, explicit user-data removal, and quoted paths containing spaces.

- [x] **Step 2: Run the test and verify RED**

Run `cmake --build build/windows-x64 --config Release --target piinput-uninstall-layout-tests --parallel 1`.
Expected: compilation fails because `uninstall_layout.h` and its interfaces do not exist.

- [x] **Step 3: Implement the minimal pure module**

The module must reject absolute marker content, `..`, separators, roots outside the expected product directory and paths that are not strict descendants. Registry strings must be fully quoted and contain `--silent` only in `QuietUninstallString`.

- [x] **Step 4: Run the focused test and verify GREEN**

Run `ctest --test-dir build/windows-x64 -C Release -R piinput-uninstall-layout --output-on-failure`.
Expected: 1/1 passed.

### Task 2: Native two-stage uninstaller

**Files:**
- Create: `platform/windows/uninstaller/main.cpp`
- Create: `tests/uninstaller_source_regression.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `UninstallLayout` and active-version resolver.
- Consumes: existing `remove_or_schedule_legacy_runtime(path)`.
- Produces: `PiInput-Uninstall.exe` with `--silent`, `--remove-user-data`, `--worker` and `--wait-pid`.

- [x] **Step 1: Write the failing source/integration gate**

Require a GUI executable target, `TaskDialogIndirect`, a default-false verification checkbox, temporary worker copy, parent wait, `--disable-user`, `--deactivate`, `DllUnregisterServer`, delayed deletion, and fail-closed ordering where runtime deletion occurs only after successful unregistration.

- [x] **Step 2: Run the gate and verify RED**

Run `ctest --test-dir build/windows-x64 -C Release -R piinput-uninstaller-source-regression --output-on-failure`.
Expected: failure because the target and implementation do not exist.

- [x] **Step 3: Implement the native executable**

Use `TASKDIALOGCONFIG` with `TDCBF_YES_BUTTON | TDCBF_NO_BUTTON` and verification text `同时删除用户词库、设置和学习记录`. Do not set `TDF_VERIFICATION_FLAG_CHECKED`. The launcher copies itself to `%TEMP%`, starts a worker and exits. The worker waits for the launcher PID, validates the product layout, unregisters, deletes or schedules version paths, optionally deletes `UserData`, removes the uninstall entry, schedules its temporary copy for deletion and reports completion.

- [x] **Step 4: Build and run focused tests**

Run `cmake --build build/windows-x64 --config Release --target PiInput-Uninstall piinput-uninstall-layout-tests --parallel 1` and the two focused CTests.
Expected: build succeeds and both tests pass.

### Task 3: Installer registration and upgrade refresh

**Files:**
- Modify: `platform/windows/installer/main.cpp`
- Modify: `tests/installer_layout_tests.cpp`
- Modify: `tests/windows_source_regression.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 registry metadata.
- Produces: stable `%LOCALAPPDATA%\PiInput\Uninstall\PiInput-Uninstall.exe`.
- Produces: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\PiInput`.

- [x] **Step 1: Extend tests and verify RED**

Require installer payload validation for `PiInput-Uninstall.exe`, stable-copy creation after successful TSF registration, uninstall registry values, and rollback that preserves a previous valid uninstaller on upgrade failure.

- [x] **Step 2: Implement installation wiring**

Copy the uninstaller from packaged `bin`, atomically replace the stable copy, write all registry values only after TSF/profile setup succeeds, and restore the previous stable copy/registry values if a later installation step fails.

- [x] **Step 3: Run installer and Windows source tests**

Run focused Release tests for installer layout, uninstall layout and Windows source regression.
Expected: all pass.

### Task 4: Embedded π icon registration

**Files:**
- Modify: `platform/windows/tsf/dllmain.cpp`
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Produces: TSF `RegisterProfile` references `PiInputTSF.dll` and icon index `0`.

- [x] **Step 1: Reproduce and identify the standalone-icon fallback**

The installed profile currently stores `IconFile=...\piinput_icon.ico`, while Windows displays the language abbreviation. Microsoft requires IME branding icons to live in a DLL or EXE.

- [x] **Step 2: Make the regression gate fail**

The gate rejected `replace_filename(L"piinput_icon.ico")` and required the module path passed to `register_profile`.

- [x] **Step 3: Register the embedded icon and verify GREEN**

`DllRegisterServer` now passes its own `PiInputTSF.dll` module path; the focused TSF build and Windows source regression pass.

### Task 5: Package, documentation and release verification

**Files:**
- Modify: `scripts/windows/package-release.ps1`
- Modify: `docs/安装与使用指南.md`
- Modify: `README.md`
- Modify: `RELEASE_MANIFEST.md`
- Modify: `VERSION`
- Modify: `tests/release_metadata_regression.cmake`
- Create: `docs/release_notes_v0.3.9-dev.md`
- Create: `docs/VERIFICATION_v0.3.9-dev.md`

**Interfaces:**
- Produces: `artifacts/PiInput-v0.3.9-dev-windows-x64.zip`.

- [x] **Step 1: Require the uninstaller in the release gate**

The package script must fail when `dist/windows-x64/bin/PiInput-Uninstall.exe` is missing and must copy it to both `bin/` and the ZIP root.

- [x] **Step 2: Update user documentation**

Document Windows Installed Apps, direct uninstaller use, the default-preserve checkbox, full removal, locked-DLL behavior, reinstall, π icon refresh and the fixed dictionary query tool.

- [x] **Step 3: Build and run all tests**

Run the complete Release build and full CTest suite with `--output-on-failure`.
Expected: every target builds and 0 tests fail.

- [x] **Step 4: Package and inspect the final ZIP**

Require root `PiInput-Install.exe`, `PiInput-Uninstall.exe`, `PiInput-Test.exe`, documents, `bin/PiInputTSF.dll`, `bin/PiInput-Uninstall.exe`, Chinese/English dictionaries and icon resources.

- [x] **Step 5: Run isolated install/uninstall fixtures and final checks**

Verify the non-system pure filesystem scenarios, packaged CMD parsing, SHA inventory, release metadata and `git diff --check`. Do not run the installer or uninstaller against the user's live system.
