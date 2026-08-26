# PiInput Multi-App Input Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate long-candidate ellipsis, cross-window composition leakage, first-context candidate delay, broken Shift toggling and full-width programmer operators without moving dictionaries back into the TSF DLL.

**Architecture:** Keep the resident `PiInputHost.exe`; add testable candidate presentation and Context-session policies, bind each TSF Context to its own mirror/session, and make update edit sessions safely fall back to asynchronous completion. Punctuation remains deterministic and local.

**Tech Stack:** C++20, Win32, TSF/COM, named pipes, CMake, CTest, MSVC Release.

## Global Constraints

- Preserve all unrelated dirty worktree changes.
- Do not load dictionaries in `PiInputTSF.dll`.
- Do not install, push or change the user's active system profile during implementation.
- Use test-first RED/GREEN cycles for every behavior change.

---

### Task 1: Adaptive long-candidate layout

**Files:**
- Modify: `include/piinput/candidate_layout.h`
- Modify: `src/candidate_layout.cpp`
- Modify: `include/piinput/candidate_grid.h`
- Modify: `src/candidate_grid.cpp`
- Modify: `src/host_session.cpp`
- Modify: `platform/windows/tsf/candidate_window.cpp`
- Test: `tests/candidate_grid_tests.cpp`
- Test: `tests/candidate_presenter_tests.cpp`

**Interfaces:**
- Produces: `candidate_items_per_row(configured, candidate_texts)` and a grid column update that preserves a valid selection.

- [ ] Add literal tests requiring 6 columns for short words, 2 for medium phrases and 1 for a long sentence.
- [ ] Run candidate tests and confirm RED because adaptive layout is absent.
- [ ] Implement the minimal policy and connect it before `CandidateGrid::reset`.
- [ ] Add a monotonic-width placement test and confirm RED.
- [ ] Keep left/top fixed while allowing width growth; measure one long item up to the monitor budget and remove unconditional 180-pixel ellipsis.
- [ ] Run candidate tests and confirm GREEN.

### Task 2: Context-scoped mirror and first update fallback

**Files:**
- Modify: `platform/windows/tsf/composition_mirror.h`
- Modify: `platform/windows/tsf/composition_mirror.cpp`
- Modify: `platform/windows/tsf/stable_text_service.h`
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Test: `tests/composition_mirror_tests.cpp`
- Test: `tests/pipe_client_tests.cpp`

**Interfaces:**
- Produces: a fresh mirror/session identity for a new Context and an owned asynchronous update completion.

- [ ] Add a mirror test proving old raw/caret and old replies cannot cross a session reset.
- [ ] Confirm RED against the current fixed session id.
- [ ] Implement explicit session reset and Context binding.
- [ ] Add an edit-result state test for sync rejection followed by successful async update and caret publication.
- [ ] Confirm RED, then implement an owned deferred update request without stack pointers.
- [ ] Run mirror, pipe and Windows source regressions and confirm GREEN.

### Task 3: Reliable Shift and mixed punctuation

**Files:**
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `platform/windows/tsf/stable_text_service.h`
- Modify: `include/piinput/host_session.h`
- Modify: `src/host_session.cpp`
- Modify: `src/punctuation.cpp`
- Test: `tests/test_main.cpp`
- Test: `tests/host_session_tests.cpp`
- Test: `tests/data/punctuation_cases.tsv`

**Interfaces:**
- Produces: focused-Context fallback for Shift and a literal-ASCII punctuation event.

- [ ] Add tests for two Shift round trips and for unavailable event Context choosing the focused Context.
- [ ] Confirm RED at the TSF policy boundary.
- [ ] Implement local mode transition first and dispatch using event Context or focused fallback.
- [ ] Add literal punctuation tests for `1.`, `++`, `--`, `==`, Chinese full stop and Chinese em dash.
- [ ] Confirm RED, implement the minimal literal punctuation policy, and confirm GREEN.

### Task 4: Verification and v0.4.6 package

**Files:**
- Modify: `VERSION`
- Modify: `CMakeLists.txt`
- Create: `docs/VERIFICATION_v0.4.6-dev.md`
- Create: `docs/v0.4.6安装、使用与测试.md`

**Interfaces:**
- Produces: `artifacts/PiInput-v0.4.6-dev-windows-x64.zip`.

- [ ] Build all Windows Release targets with one build worker.
- [ ] Run the complete CTest suite and require zero failures.
- [ ] Run the real 50 万词 Host process/performance tests.
- [ ] Validate the extracted installer, uninstaller, test app, Host, TSF DLL and data files.
- [ ] Record the Notepad4, Notepad++, Explorer and Chrome compatibility matrix; where system installation is required, mark it as user-run rather than claiming automation.
- [ ] Report the package path and SHA-256 without installing or pushing.
