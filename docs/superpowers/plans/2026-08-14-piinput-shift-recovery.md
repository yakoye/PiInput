# PiInput Shift Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make standalone Shift reliably switch Chinese/English even when English candidates are disabled or a TSF host omits KeyUp.

**Architecture:** Keep input mode in `HostSession`, decouple it from optional `EnglishSession`, and let disabled English candidates commit literal text. Extend the small `ShiftToggleState` to recover a missing KeyUp before the next ordinary key, then use that decision in the stable TSF shim.

**Tech Stack:** C++20, Windows TSF, CMake, CTest.

## Global Constraints

- Do not change punctuation mappings in this task.
- Do not enable English candidates by default.
- Do not add network, AI, cloud or new input schemas.
- Preserve modifier-key behavior and current stable Shim/Host boundary.

---

### Task 1: Decouple English Mode from English Candidates

**Files:**
- Modify: `tests/host_session_tests.cpp`
- Modify: `src/host_session.cpp`

**Interfaces:**
- Consumes: `HostSession::apply(const HostKeyEvent&)` and `SettingsSnapshot::english.enabled`.
- Produces: English mode with literal commits when `EnglishSession` is absent.

- [ ] Add a failing test using default settings. Assert that `switch_to_english` is accepted, a text key commits the literal ASCII character with no candidates, punctuation remains ASCII, and `switch_to_chinese` succeeds.
- [ ] Run `piinput-host-session-tests` and confirm it fails because `switch_to_english` is rejected.
- [ ] Remove the `english_ == nullptr` rejection and add the minimal direct-text commit path for disabled candidates.
- [ ] Re-run `piinput-host-session-tests` and confirm it passes.

### Task 2: Recover a Missing Shift KeyUp

**Files:**
- Modify: `include/piinput/input_mode.h`
- Modify: `src/input_mode.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `platform/windows/tsf/stable_text_service.h`
- Modify: `platform/windows/tsf/stable_text_service.cpp`

**Interfaces:**
- Consumes: physical Shift state from `GetKeyState(VK_SHIFT)`.
- Produces: `bool ShiftToggleState::on_other_key_down(bool shift_still_down)` and one shared stable-Shim mode-switch helper.

- [ ] Add failing state-machine tests: missing KeyUp followed by released Shift toggles; a key while Shift remains down does not toggle; the following Shift release still does not toggle after modifier use.
- [ ] Run `piinput-core-tests` and confirm the missing-KeyUp assertion fails.
- [ ] Implement the minimal state recovery and route both normal KeyUp and recovered KeyUp through one stable-Shim helper.
- [ ] Build `PiInputTSF` and run the core plus Windows source regression tests.

### Task 3: Release Verification

**Files:**
- Modify: version/release metadata only if all tests pass.
- Create: a focused verification note for this build.

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces: a buildable Windows package for user installation.

- [ ] Build all Release targets.
- [ ] Run complete CTest with `--output-on-failure`.
- [ ] Run metadata, PowerShell parsing and `git diff --check` gates.
- [ ] Package the Windows x64 artifact and record its SHA-256.
- [ ] Do not push the repository; provide the local package for user testing.
