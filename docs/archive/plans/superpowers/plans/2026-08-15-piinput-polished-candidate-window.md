# PiInput Polished Candidate Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a fixed 48-DIP, 16-DIP-font, purple-accented single-row candidate bar without changing input behavior or geometry stability.

**Architecture:** Keep the existing `CandidateWindow` lifecycle, candidate grid, DPI scaling, and no-op update guard. Add testable visual metrics and draw the border, selected candidate, expand glyph, and tool glyph in the existing Win32/GDI paint path.

**Tech Stack:** C++20, Win32/GDI, MSVC, CMake/CTest.

## Global Constraints

- Default collapsed height is exactly 48 DIP and default font size is 16 DIP.
- Candidate ranking, pagination, selection IDs, TSF focus, and Host communication are unchanged.
- Ordinary candidate updates do not move, resize, or repaint an unchanged snapshot.
- User-configured font size and row height remain supported within existing ranges.

---

### Task 1: Lock the visual contract with failing tests

**Files:**
- Modify: `tests/settings_tests.cpp`
- Modify: `tests/settings_file_tests.cpp`
- Modify: `tests/candidate_presenter_tests.cpp`
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Consumes: `CandidateSettings`, `CandidateVisualSettings`, `candidate_window_height(...)`
- Produces: regression coverage for the 16/50 defaults and purple self-drawn candidate chrome

- [ ] Change default-setting assertions from 18/48 to 16/48.
- [ ] Add assertions for a 48-DIP row at 96 DPI and 96px at 192 DPI.
- [ ] Add source regression assertions for borderless popup creation, purple rounded selection, expand glyph, and unchanged-snapshot repaint suppression.
- [ ] Build and run the focused tests; confirm they fail against the current 18/48 blue square implementation.

### Task 2: Implement the fixed-height purple candidate bar

**Files:**
- Modify: `include/piinput/settings.h`
- Modify: `platform/windows/settings/settings_file.h`
- Modify: `platform/windows/tsf/candidate_window.h`
- Modify: `platform/windows/tsf/candidate_window.cpp`

**Interfaces:**
- Produces: 16/48 default visuals, borderless `WS_POPUP`, self-drawn light border, rounded lavender selection, and two trailing controls

- [ ] Update only the default font and height values; preserve parser ranges and user overrides.
- [ ] Remove `WS_BORDER` and paint a one-pixel light border inside the client area.
- [ ] Draw the selected item using a rounded lavender brush and consistent horizontal inset.
- [ ] Split the trailing toolbar into a chevron area and a four-grid area while preserving non-activating behavior.
- [ ] Run the focused tests until green.

### Task 3: Verify, document, and package

**Files:**
- Modify: `PROJECT_CONTEXT.md`
- Modify: `docs/VERIFICATION_v0.5.0-dev.md`
- Modify: `docs/v0.5.0安装、使用与测试.md`
- Modify: `FILE_LIST.txt`
- Modify: `SHA256SUMS.txt`

**Interfaces:**
- Produces: a visually checked Windows x64 package and updated durable documentation

- [ ] Build all Release targets with single-process MSBuild if needed for stability.
- [ ] Run the complete CTest suite with output on failure.
- [ ] Open the candidate preview/test program and capture a screenshot at 96 DPI.
- [ ] Verify the window is 48px high, font is smaller, text is vertically centered, and the purple selected state is clear.
- [ ] Refresh release file/checksum metadata and regenerate the Windows x64 ZIP.
