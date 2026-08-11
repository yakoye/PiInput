# PiInput Candidate Caret Position Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the candidate window follow the active TSF text insertion caret, with the current mouse-near position used only when the application cannot provide a text rectangle.

**Architecture:** The stable TSF shim queries the current selection geometry after Composition processing and sends a generation-bound `caret` message through the existing protocol. PiInputHost stages candidate snapshots and presents them only when the matching caret update arrives; invalid or unavailable text geometry selects the existing mouse fallback.

**Tech Stack:** C++20, Win32, Windows TSF, named-pipe protocol v1, CMake/CTest, MSVC Release.

## Global Constraints

- Preserve the stable `PiInputTSF.dll` + replaceable `PiInputHost.exe` architecture.
- Keep `HostMessageType::caret` within protocol v1; do not change existing key/reply payloads.
- Use TSF text geometry first and the current mouse-near logic only when text geometry is unavailable.
- Reject stale coordinates by exact session and generation match.
- Keep the purple π icon and the existing single-row/default, `=`-expands interaction.
- Do not install into the current Windows profile; produce a verified package for user testing.
- Preserve all unrelated dirty-worktree changes.

---

### Task 1: Define and validate the caret payload

**Files:**
- Modify: `include/piinput/host_messages.h`
- Modify: `src/host_messages.cpp`
- Modify: `tests/host_messages_tests.cpp`

**Interfaces:**
- Produces: `HostCaretUpdate { std::uint64_t generation; bool has_text_caret; std::int32_t left; std::int32_t top; std::int32_t right; std::int32_t bottom; }`
- Produces: `encode_host_caret_update(const HostCaretUpdate&) -> std::vector<std::byte>`
- Produces: `decode_host_caret_update(std::span<const std::byte>, HostPayloadError&) -> std::optional<HostCaretUpdate>`

- [ ] **Step 1: Write the failing protocol behavior tests**

Add literal round-trip cases for `{100,200,102,224}`, negative-monitor `{-1920,20,-1918,44}`, and `has_text_caret=false`. Add rejection cases for unknown flag bits, `right < left`, `bottom < top`, truncation, and trailing bytes.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-messages-tests --parallel 1
```

Expected: compilation fails because the caret payload API does not exist.

- [ ] **Step 3: Implement the fixed-width payload**

Encode generation as little-endian `uint64`, availability as `uint32`, and each coordinate as the bit-preserving `uint32` representation of `int32`. Decode with strict flags, rectangle ordering, exact length, and no Win32 types.

- [ ] **Step 4: Rebuild and run the focused CTest**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-messages-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-host-messages" --output-on-failure
```

Expected: focused test passes.

### Task 2: Stage snapshots and apply only matching caret coordinates

**Files:**
- Modify: `platform/windows/host/candidate_presenter.h`
- Modify: `platform/windows/host/candidate_presenter.cpp`
- Modify: `platform/windows/tsf/candidate_window.h`
- Modify: `platform/windows/tsf/candidate_window.cpp`
- Modify: `tests/candidate_presenter_tests.cpp`

**Interfaces:**
- Produces: `CandidatePresenter::stage(session_id, snapshot)`
- Produces: `CandidatePresenter::show_at(session_id, HostCaretUpdate)`
- Produces: `CandidateWindow::show_at_text_caret(const RECT&)`
- Consumes: `HostCaretUpdate` from Task 1.

- [ ] **Step 1: Write failing presenter tests**

Test that a staged generation 10 snapshot is displayed only by a session-matching generation 10 caret. Generation 9, generation 11, and another session must not move/show it. An unavailable caret must choose the fallback result while retaining the staged candidates.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-candidate-presenter-tests --parallel 1
```

Expected: compilation fails because `stage` and `show_at` are absent.

- [ ] **Step 3: Implement staged presentation and explicit geometry**

Keep the newest snapshot per session in the model. `show_at` verifies exact generation, updates the existing `CandidateWindow`, and calls either `show_at_text_caret` or `show_near_caret`. Reuse `place_candidate_window` for DPI and monitor clamping.

- [ ] **Step 4: Verify GREEN and geometry boundaries**

Run the candidate presenter test for 96, 144, and 192 DPI, including negative-coordinate monitors and every work-area edge.

### Task 3: Query TSF selection geometry in the stable shim

**Files:**
- Modify: `platform/windows/tsf/stable_text_service.h`
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `tests/windows_source_regression.cmake`
- Create: `tests/candidate_anchor_tests.cpp` only if geometry validation is extracted into a platform-neutral helper.

**Interfaces:**
- Produces: a synchronous read-only edit session that reads `ITfContext::GetSelection`, collapses the range to the insertion end, obtains `ITfContext::GetActiveView`, and calls `ITfContextView::GetTextExt`.
- Produces: `request_candidate_anchor(ITfContext*, generation)` which always returns a typed available/unavailable result without throwing across COM.

- [ ] **Step 1: Write the failing Windows regression**

Require `TF_ES_SYNC | TF_ES_READ`, `GetSelection`, `Collapse(TF_ANCHOR_END)`, `GetActiveView`, and `GetTextExt`; require the stable service to send a caret update only after reply confirmation and Composition edit handling.

- [ ] **Step 2: Verify RED**

```powershell
ctest --test-dir build/windows-x64 -C Release -R "piinput-windows-source-regression" --output-on-failure
```

Expected: failure stating that TSF text geometry is not queried/sent.

- [ ] **Step 3: Implement the read-only edit session**

Use COM lifetime-safe local objects, release selection range and active view on every branch, treat clipped valid text as valid, and convert any HRESULT failure into `has_text_caret=false` rather than failing the keystroke.

- [ ] **Step 4: Build the stable DLL and verify GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target PiInputTSF --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-windows-source-regression" --output-on-failure
```

Expected: DLL builds and regression passes.

### Task 4: Transport caret updates without disturbing key replies

**Files:**
- Modify: `platform/windows/tsf/pipe_client.h`
- Modify: `platform/windows/tsf/pipe_client.cpp`
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `platform/windows/host/pipe_server.cpp`
- Modify: `tests/pipe_client_tests.cpp`
- Modify: `tests/host_process_tests.ps1`

**Interfaces:**
- Produces: `PipeClient::send_caret(client_id, session_id, sequence, generation, update)`.
- Host `caret` acknowledgement uses the same envelope type and empty payload.
- Stable callback ignores valid caret acknowledgements and never disconnects the composition mirror for them.

- [ ] **Step 1: Write failing transport and host-process tests**

Assert the caret envelope type, session, generation and payload; assert that Host stages a key reply, then displays only after the matching caret request; assert caret acknowledgement does not enter key-reply decoding.

- [ ] **Step 2: Verify RED**

Build/run `piinput-pipe-client-tests` and `piinput-host-process-tests`; expected failures name missing caret transport/handling.

- [ ] **Step 3: Implement minimal ordered caret transport**

Use a separate monotonically increasing auxiliary sequence. On key reply, Host stages the snapshot. After the shim completes or skips the Composition edit, it queries and sends the caret. Host decodes, validates, calls `show_at`, and returns a caret acknowledgement.

- [ ] **Step 4: Verify GREEN**

Run the focused pipe and host-process tests and confirm no disconnect, hang, stale presentation, or failed acknowledgement.

### Task 5: Preserve and verify the purple π icon

**Files:**
- Modify: `tests/windows_source_regression.cmake`
- Modify: `docs/VERIFICATION_v0.4.1-dev.md`

**Interfaces:**
- Consumes: `platform/windows/tsf/piinput_icon.ico` and embedded `IDI_PIINPUT` resource.

- [ ] **Step 1: Add asset behavior verification**

Parse the ICO directory and require exactly the shipped 16, 20, 24, 32, 40, 48, 64, 128, and 256 pixel entries. Extract the Release DLL icon during verification and record its hash/visual inspection result as purple background with white π.

- [ ] **Step 2: Run the regression and preserve the asset**

No icon production change is expected. If the regression does not detect a missing resource entry, correct the regression rather than changing the icon.

### Task 6: Full regression, package, and handoff

**Files:**
- Modify: `VERSION`
- Modify: `CMakeLists.txt`
- Modify: `PROJECT_CONTEXT.md`
- Modify: `RELEASE_MANIFEST.md`
- Modify: `docs/安装与使用指南.md`
- Create: `docs/release_notes_v0.4.1-dev.md`
- Create: `docs/VERIFICATION_v0.4.1-dev.md`
- Refresh: `FILE_LIST.txt`
- Refresh: `SHA256SUMS.txt`

**Interfaces:**
- Produces: `artifacts/PiInput-v0.4.1-dev-windows-x64.zip` and adjacent SHA-256 file.

- [ ] **Step 1: Run focused input regressions**

Run tests covering all 406 Xiaohe legal codes, full-pinyin syllables, 3500/7000 character coverage, `yu/yv` compatibility, incomplete input, long paragraphs, phrase-first then segmented selection, Chinese punctuation, English completion, one-row/expanded candidates, Shift switching, dictionary query, installer/uninstaller, and stable Host restart.

- [ ] **Step 2: Run fresh Release build**

```powershell
cmake --build build/windows-x64 --config Release --parallel 1
```

Expected: exit code 0 for every target.

- [ ] **Step 3: Run the complete CTest suite**

```powershell
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

Expected: 0 failed tests. Report the exact passed/total count.

- [ ] **Step 4: Stage, package, and inspect artifacts**

Run the existing Release install/package workflow. Inspect ZIP contents for stable DLL, Host, installer, uninstaller, demo, query tool, dictionaries, settings, icon and Markdown guide. Verify archive SHA-256 and validate that no old LiteIME name/path appears.

- [ ] **Step 5: Run final integrity checks**

Run PowerShell/JSON parsing, `git diff --check`, FILE_LIST and SHA256SUMS verification, PE dependency checks for the stable shim, and package extraction smoke tests.

- [ ] **Step 6: Commit locally without pushing**

Commit the implementation and verification evidence with a precise message. Do not push until the user explicitly requests it.
