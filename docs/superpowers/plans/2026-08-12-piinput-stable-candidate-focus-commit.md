# PiInput Stable Candidate, Focus and Commit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make candidate geometry stable for one composition, restore focus correctly, and make Space commit fail-safe while retaining v0.3.8 visual quality.

**Architecture:** `CandidateWindow` owns a per-composition geometry lock; `CandidatePresenter` only changes content during ordinary typing. The stable TSF Shim sends explicit focus messages, binds Resume replies to the focused `ITfContext`, and restores Host state after failed edits.

**Tech Stack:** C++20, Win32, TSF/COM, named-pipe Host protocol, CMake, CTest, MSVC Release.

## Global Constraints

- Do not load dictionaries in the TSF Shim.
- Do not add network, AI, telemetry or new input schemes.
- Do not install or push during implementation.
- Preserve all unrelated dirty worktree changes.
- Use Notepad4 for final real-window verification.

---

### Task 1: Lock candidate geometry for one composition

**Files:**
- Modify: `platform/windows/tsf/candidate_window.h`
- Modify: `platform/windows/tsf/candidate_window.cpp`
- Test: `tests/candidate_presenter_tests.cpp`

**Interfaces:**
- Produces: `CandidateWindow::geometry_for_update(...)` behavior that preserves left/top/width and changes height only when visible rows change.

- [ ] Add failing geometry tests for ordinary content updates and explicit row expansion.
- [ ] Run `piinput-candidate-presenter` and confirm RED.
- [ ] Add per-composition position/size state; reset it only in `hide()`.
- [ ] Keep ordinary redraw content-only and preserve v0.3.8 font/layout constants.
- [ ] Run presenter and Windows source regression tests and confirm GREEN.

### Task 2: Restore the focused TSF Context

**Files:**
- Modify: `platform/windows/tsf/stable_text_service.h`
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `platform/windows/tsf/pipe_client.h`
- Modify: `platform/windows/tsf/pipe_client.cpp`
- Modify: `platform/windows/host/pipe_server.cpp`
- Test: `tests/pipe_client_tests.cpp`
- Test: `tests/windows_source_regression.cmake`

**Interfaces:**
- Produces: `PipeClient::send_focus(const MirrorRequest&, bool)` and `TextService::focused_context()`.

- [ ] Add RED tests for focus envelope ordering and source gates requiring `GetFocus`/`GetTop`.
- [ ] Send focus=false on blur and hide only that session.
- [ ] On focus=true acquire the top Context, retain it by Resume sequence, and apply the Resume reply there.
- [ ] Reject stale focus and caret updates.
- [ ] Run pipe, mirror and source regressions and confirm GREEN.

### Task 3: Make Space commit fail-safe

**Files:**
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `platform/windows/tsf/composition_mirror.h`
- Modify: `platform/windows/tsf/composition_mirror.cpp`
- Test: `tests/composition_mirror_tests.cpp`
- Test: `tests/host_session_tests.cpp`

**Interfaces:**
- Consumes: `CompositionMirror::resume_state()`.
- Produces: failed edit recovery that sends Resume without accepting the pending commit.

- [ ] Add a failing sequence test: input → Space commit reply → edit failure → Resume → candidates restored.
- [ ] Confirm current code loses Host state in RED.
- [ ] Preserve mirror input on failure and enqueue a Context-bound Resume request.
- [ ] Accept the clear snapshot only after successful edit.
- [ ] Run Host Session, Composition Mirror and Host Process tests and confirm GREEN.

### Task 4: Full verification and package

**Files:**
- Modify: `VERSION`
- Modify: `CMakeLists.txt`
- Create: `docs/VERIFICATION_v0.4.5-dev.md`
- Create: `docs/v0.4.5安装、使用与测试.md`

**Interfaces:**
- Produces: `PiInput-v0.4.5-dev-windows-x64.zip`.

- [ ] Build every Windows Release target with one build worker.
- [ ] Run the complete CTest suite with zero failures.
- [ ] Launch Notepad4 and verify stable geometry, Space, focus switch and candidate restoration.
- [ ] Verify PiInput-Test free-text input through the installed TSF path.
- [ ] Build the release ZIP and verify all required files after extraction.
- [ ] Report package path and SHA-256 without installing or pushing.

