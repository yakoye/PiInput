# PiInput Candidate Toolbar and Visual Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a non-activating candidate-toolbar menu with Symbols and Settings, plus a native settings program for schema, default language, candidate font size, and single-row height.

**Architecture:** Keep decoding and dictionary code unchanged. Extend the shared settings snapshot with validated visual values, render and hit-test the toolbar in the Host-owned candidate popup, route toolbar actions through the focused Host session, and persist visual values through a small native Win32 settings executable.

**Tech Stack:** C++20, Win32, GDI, CMake, CTest, PowerShell packaging tests.

## Global Constraints

- Preserve the existing dirty worktree; do not reset or remove unrelated files.
- Do not add AI, cloud lookup, voice, telemetry, or networking.
- Candidate rendering must not load dictionaries or write learning data.
- The toolbar must not consume candidate numbers or steal editor focus.
- Visual changes apply at the next composition boundary so the visible candidate window does not jump.
- Do not push the repository unless the user later requests it.

---

### Task 1: Candidate visual settings model

**Files:**
- Modify: `include/piinput/settings.h`
- Modify: `src/settings.cpp`
- Modify: `tests/settings_tests.cpp`
- Modify: `platform/windows/installer/main.cpp`

**Interfaces:**
- Produces: `CandidateSettings::font_size` and `CandidateSettings::window_height` as validated `std::uint32_t` DIP values.
- Consumes: existing `parse_settings_text`, `serialize_default_settings`, and `SettingsManager` boundary publication.

- [x] Write tests asserting defaults 18/48, accepted endpoints 10/28 and 20/72, rejection outside those ranges, preservation of unrelated INI keys, and serialized defaults.
- [ ] Build and run `piinput-settings-tests`; confirm RED because the fields and parser do not exist.
- [ ] Add the two fields, validation, serialization, and installer defaults without changing other candidate settings.
- [ ] Rebuild and run `piinput-settings-tests`; confirm GREEN.

### Task 2: Candidate popup visual metrics and toolbar hit testing

**Files:**
- Modify: `platform/windows/tsf/candidate_window.h`
- Modify: `platform/windows/tsf/candidate_window.cpp`
- Modify: `tests/candidate_presenter_tests.cpp`

**Interfaces:**
- Produces: `CandidateVisualSettings`, pure `candidate_window_height(rows,dpi,height_dip)`, toolbar geometry/hit-test helpers, and `CandidateToolbarAction { none, open_menu, symbols, settings }`.
- Consumes: current candidate content and grid state.

- [x] Write pure tests for 18/48 defaults, DPI scaling, 10/20 and 28/72 limits, vertical centering, candidate-area exclusion, toolbar hit boxes, and menu item hit boxes.
- [ ] Run `piinput-candidate-presenter-tests`; confirm RED for missing visual and toolbar APIs.
- [ ] Implement metric calculation and hit-test helpers, then pass settings into `CandidateWindow::update`.
- [ ] Draw the compact trailing tool icon and two-item non-activating popup; handle `WM_MOUSEMOVE`, `WM_LBUTTONUP`, Esc and session hide without changing candidate numbering.
- [ ] Rebuild and run `piinput-candidate-presenter-tests`; confirm GREEN.

### Task 3: Route Symbols and Settings actions

**Files:**
- Modify: `include/piinput/host_session.h`
- Modify: `src/host_session.cpp`
- Modify: `platform/windows/host/candidate_presenter.h`
- Modify: `platform/windows/host/candidate_presenter.cpp`
- Modify: `platform/windows/host/session_manager.h`
- Modify: `platform/windows/host/session_manager.cpp`
- Modify: `platform/windows/host/main.cpp`
- Test: `tests/host_session_tests.cpp`
- Test: `tests/session_manager_tests.cpp`
- Test: `tests/candidate_presenter_tests.cpp`

**Interfaces:**
- Produces: `HostKeyKind::open_symbol_center`, `HostSession::apply` symbol-center transition, and a presenter callback for settings launch.
- Consumes: existing symbol resolution, focused session identity, session generation rules, and Host presentation updates.

- [ ] Write tests proving direct symbol action produces the same symbol candidates as `;;f`, does not commit literal command text, and only targets the focused session.
- [ ] Run focused Host and presenter tests; confirm RED.
- [ ] Add the explicit Host action and presenter callbacks; do not synthesize keystrokes.
- [ ] Route settings clicks to a launcher callback and symbol clicks to the focused session, then restage/show the returned snapshot.
- [ ] Rebuild and run the focused tests; confirm GREEN.

### Task 4: Native settings executable and atomic writer

**Files:**
- Create: `platform/windows/settings/main.cpp`
- Create: `platform/windows/settings/settings_file.h`
- Create: `platform/windows/settings/settings_file.cpp`
- Create: `tests/settings_file_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `PiInput-Settings.exe`, `load_candidate_visual_settings(path)`, and `save_candidate_visual_settings_atomic(path,font_size,window_height)`.
- Consumes: the shared parser, current `settings.ini`, and `%LOCALAPPDATA%\PiInput\UserData`.

- [ ] Write tests for loading missing/valid/invalid files, retaining unrelated sections and comments, atomic replacement, and rejecting invalid ranges without modifying the original.
- [ ] Build the new test target; confirm RED because the writer API is missing.
- [x] Implement the narrow file editor and native dialog with schema/default-language selectors, two visual selectors, preview, Apply, Restore defaults and Close.
- [ ] Ensure one settings-process instance and foreground an existing settings window on repeated clicks.
- [ ] Rebuild and run the settings-file tests; confirm GREEN.

### Task 5: Runtime hot reload and package integration

**Files:**
- Modify: `platform/windows/host/host_runtime.cpp`
- Modify: `platform/windows/host/candidate_presenter.cpp`
- Modify: `platform/windows/installer/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/windows_source_regression.cmake`
- Modify: `tests/installer_layout_tests.cpp`

**Interfaces:**
- Consumes: `SettingsManager` and `PiInput-Settings.exe`.
- Produces: next-composition visual setting publication and installed settings executable.

- [ ] Add regression tests requiring the Host to pass font/height settings to the presenter at a composition boundary and requiring installation/package layout to contain `PiInput-Settings.exe`.
- [ ] Run the focused tests and confirm RED.
- [ ] Wire visual snapshots without reloading Engine or dictionaries, add the settings target to install/package rules, and launch it with the user-data settings path.
- [ ] Run focused tests and confirm GREEN.

### Task 6: Documentation and complete verification

**Files:**
- Modify: `docs/安装与使用指南.md`
- Create: `docs/v0.5.0安装、使用与测试.md`
- Create: `docs/VERIFICATION_v0.5.0-dev.md`
- Modify: `PROJECT_CONTEXT.md`
- Modify: `VERSION`
- Modify: `FILE_LIST.txt`
- Modify: `SHA256SUMS.txt`

**Interfaces:**
- Produces: user-facing installation/settings instructions and reproducible verification evidence.

- [ ] Document the toolbar, symbol menu, settings ranges, config location, reset behavior, and troubleshooting.
- [ ] Run a fresh Release all-target build and full CTest; record exact totals and failures.
- [ ] Build the package and verify both `PiInputHost.exe` and `PiInput-Settings.exe` are present.
- [ ] Use the portable test console, Notepad4, Notepad++, and Word to enter Chinese text and exercise toolbar symbols/settings, recording cold-start timing, caret placement, clarity, size change, focus, and cross-window behavior.
- [ ] Refresh manifest/hash metadata only after all project files are final, run `git diff --check`, and record any Windows test limitation honestly.
