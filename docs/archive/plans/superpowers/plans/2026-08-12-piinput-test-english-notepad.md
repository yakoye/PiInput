# PiInput-Test English Candidates and Notepad Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `PiInput-Test.exe` with offline English candidate testing and a freely editable multiline test document.

**Architecture:** Keep the standalone Win32 preview process and reuse `EnglishLexicon` plus `EnglishSession` from `piinput_core`. The preview owns all test state and packaged/user dictionary paths, so no TSF DLL or Host process is loaded.

**Tech Stack:** C++20, Win32 EDIT/LISTBOX/COMBOBOX controls, CMake/CTest, PowerShell release packaging.

## Global Constraints

- Do not modify TSF registration or install the system input method.
- Do not push the repository.
- Preserve all existing dirty working-tree changes.
- The packaged executable remains named `PiInput-Test.exe`.

---

### Task 1: Lock the Preview Contract

**Files:**
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Consumes: existing preview source text and release packaging text.
- Produces: a failing gate for English mode, multiline editor, packaged dictionaries and user learning.

- [ ] Add assertions for `EnglishSession`, `english_lexicon.tsv`, `english_supplement.tsv`, `english_learning.tsv`, `ES_MULTILINE`, “中文候选”, “英文候选”, and insertion-point text replacement.
- [ ] Run `ctest --test-dir build/windows-x64 -C Release -R piinput-windows-source-regression --output-on-failure` and confirm the new contract fails because the preview lacks English integration.

### Task 2: Implement English and Free-Text Testing

**Files:**
- Modify: `platform/windows/preview/main.cpp`

**Interfaces:**
- Consumes: `EnglishLexicon::load_*_tsv`, `EnglishSession::insert/choose`, package/user data paths.
- Produces: mode selection, English candidates, editable document and caret-aware candidate commit.

- [ ] Add preview mode state and load packaged plus optional user English data.
- [ ] Route candidate refresh and candidate selection through Chinese or English state.
- [ ] Insert chosen text at the test document selection using `EM_REPLACESEL`.
- [ ] Preserve standard editing behavior in the multiline test document.
- [ ] Re-run the source regression and targeted English tests until both pass.

### Task 3: Document, Build, and Package v0.4.3-dev

**Files:**
- Modify: `VERSION`, `CMakeLists.txt`, `PROJECT_CONTEXT.md`, `README.md`, `docs/安装与使用指南.md`, `RELEASE_MANIFEST.md`
- Create: `docs/release_notes_v0.4.3-dev.md`, `docs/VERIFICATION_v0.4.3-dev.md`

**Interfaces:**
- Consumes: verified executable and existing release script.
- Produces: `PiInput-v0.4.3-dev-windows-x64.zip` containing the standalone test executable and English dictionaries.

- [ ] Update version and user-facing instructions for English mode and free text.
- [ ] Build all Windows Release targets and run the full CTest suite.
- [ ] Launch `PiInput-Test.exe` and verify both modes plus free text without loading `PiInputTSF.dll`.
- [ ] Refresh release metadata, package the verified tree, and calculate SHA-256.

