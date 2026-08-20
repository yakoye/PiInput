# PiInput Compact Candidate Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the redundant composition header, render a 48-DIP collapsed candidate window, and fill the first row with every ordered candidate that fits.

**Architecture:** Keep candidate ordering and grid navigation in `HostSession`; replace the coarse longest-string column heuristic with an ordered width-budget packer. Keep rendering in `CandidateWindow`, but compute window height without a header row and stop painting composition text.

**Tech Stack:** C++20, Win32/GDI, CMake/CTest, MSVC Release.

## Global Constraints

- Do not change candidate ordering or ranking.
- Keep the configured `items_per_row` as an upper bound.
- Keep `=`/Down expansion, `-`/Up navigation, digit selection, DPI scaling, and geometry locking.
- The collapsed outer window height is exactly 48 DIP.

---

### Task 1: Ordered first-row packing

**Files:**
- Modify: `tests/candidate_grid_tests.cpp`
- Modify: `src/candidate_layout.cpp`

**Interfaces:**
- Consumes: `candidate_items_per_row(std::size_t, const std::vector<std::string>&)`
- Produces: an ordered, width-budgeted first-row count in `[1, configured_items_per_row]`

- [ ] Add tests proving later long candidates do not shrink the first row and short candidates fill space after long entries.
- [ ] Run `piinput-candidate-grid-tests` and observe the old longest-string heuristic fail.
- [ ] Implement the minimal ordered 600-DIP budget packer.
- [ ] Rebuild and run `piinput-candidate-grid-tests` until green.

### Task 2: Headerless 48-DIP rendering

**Files:**
- Modify: `tests/candidate_presenter_tests.cpp`
- Modify: `platform/windows/tsf/candidate_window.h`
- Modify: `platform/windows/tsf/candidate_window.cpp`
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Produces: `candidate_window_height(std::size_t visible_rows, UINT dpi) noexcept`

- [ ] Add tests for 48-DIP single-row and 108-DIP three-row height.
- [ ] Run `piinput-candidate-presenter-tests` and observe the missing helper failure.
- [ ] Implement height calculation and remove header painting.
- [ ] Rebuild `PiInputHost` and the two focused test targets.

### Task 3: Documentation and full verification

**Files:**
- Modify: `README.md`
- Modify: `PROJECT_CONTEXT.md`
- Modify: `docs/release_notes_v0.4.9-dev.md`
- Modify: `docs/v0.4.9安装、使用与测试.md`
- Modify: `FILE_LIST.txt`
- Modify: `SHA256SUMS.txt`

- [ ] Document the compact window and ordered fill rule.
- [ ] Refresh release file/checksum metadata.
- [ ] Build all Release targets.
- [ ] Run the complete CTest suite with `--output-on-failure --parallel 1`.
- [ ] Stage and verify a Windows x64 package without installing it into the system.

