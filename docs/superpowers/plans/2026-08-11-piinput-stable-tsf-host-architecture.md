# PiInput Stable TSF Shim and Host Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the versioned in-process PiInput implementation with one stable TSF shim and a restartable per-user Host so normal upgrades do not require closing applications, while fixing unintended multi-row candidate expansion.

**Architecture:** `PiInputTSF.dll` keeps only COM/TSF edit-session work, a small local composition mirror, and an IPC client. `PiInputHost.exe` owns input sessions, dictionaries, ranking, settings and candidate UI. A versioned binary protocol carries sequence and generation numbers; the installer performs a one-time migration to permanent CLSID `{13EB305F-2DA3-4CF7-8C45-16B016B801B5}` and Profile GUID `{4ED27B7C-678E-4240-827A-24DA597F8D4B}`.

**Tech Stack:** C++20, Win32, COM/TSF, Windows Named Pipes, DirectWrite/Direct2D, CMake, CTest, PowerShell 5.1-compatible packaging scripts.

## Global Constraints

- Normal engine, dictionary, ranking, settings and candidate-UI upgrades must not terminate client applications or require a system restart.
- `PiInputTSF.dll` must not load dictionaries, decode pinyin, rank candidates, draw candidate UI, access the network or synchronously write user data.
- `OnTestKeyDown` and `OnTestKeyUp` must be side-effect free; only formal key events may change session or candidate-view state.
- Candidate UI starts with exactly one row and expands only after a formal `=` or Down key event.
- Every IPC event carries protocol version, client instance, session, sequence and generation; stale generations are discarded.
- Named Pipe access is limited to the current user and LocalSystem. PiInput remains offline and never transmits input text over a network.
- Existing cross-platform `piinput_core` remains independent of Win32 and COM.
- User data stays under `%LOCALAPPDATA%\PiInput\UserData` and is not removed by ordinary upgrades.
- Do not push the repository or alter the user's live PiInput installation during implementation. Produce a package for user-controlled installation only after verification.
- Preserve all unrelated dirty-worktree changes.

---

## File and Module Map

- `include/piinput/host_protocol.h`, `src/host_protocol.cpp`: bounded binary IPC messages and validation.
- `include/piinput/host_session.h`, `src/host_session.cpp`: process-independent session state and candidate controller.
- `platform/windows/host/main.cpp`: Host process entry, single-instance lifetime and health commands.
- `platform/windows/host/pipe_server.*`: per-session secured Named Pipe server.
- `platform/windows/host/session_manager.*`: maps client/session IDs to core sessions.
- `platform/windows/host/candidate_presenter.*`: owns the existing candidate window outside applications.
- `platform/windows/tsf/pipe_client.*`: nonblocking connection, request queue and reconnect.
- `platform/windows/tsf/composition_mirror.*`: minimal local raw input/caret/generation state.
- `platform/windows/tsf/text_service.cpp`: stable shim key policy and TSF edit actions only.
- `platform/windows/installer/stable_runtime.*`: stable Shim and versioned Host layout, atomic current/rollback markers.
- `platform/windows/installer/main.cpp`: transactional Profile migration and Host upgrade.
- `platform/windows/diagnostics/main.cpp`: active Profile, Shim protocol and Host build diagnostics.
- `tests/*host*`, `tests/*protocol*`, `tests/*upgrade*`: pure and Windows integration coverage.

---

### Task 1: Reproduce and close the candidate-row state defect

**Files:**
- Modify: `include/piinput/candidate_grid.h`
- Modify: `src/candidate_grid.cpp`
- Modify: `platform/windows/tsf/text_service.cpp`
- Modify: `tests/candidate_grid_tests.cpp`
- Modify: `tests/windows_source_regression.cmake`
- Create: `include/piinput/key_event_gate.h`
- Create: `src/key_event_gate.cpp`
- Create: `tests/key_event_gate_tests.cpp`

**Interfaces:**
- Produces: `enum class TsfKeyPhase { test_down, key_down, test_up, key_up };`
- Produces: `KeyEventDecision KeyEventGate::observe(TsfKeyPhase phase, std::uint32_t virtual_key);`
- Produces: `bool KeyEventDecision::mutates_state` and `bool KeyEventDecision::expand_candidates`.

- [ ] **Step 1: Write failing key-trace and candidate-grid tests**

Add a trace containing `d r l o j u z i` with each key represented by test-down/key-down/test-up/key-up. Assert every letter generation leaves `visible_rows()==1`, and assert a test-only `VK_OEM_PLUS` does not expand while a formal key-down does.

```cpp
for (const char key : std::string("drlojuzi")) {
    gate.observe(TsfKeyPhase::test_down, key);
    const auto event = gate.observe(TsfKeyPhase::key_down, key);
    grid.reset(18);
    check(event.mutates_state && grid.visible_rows() == 1, "letters remain collapsed");
}
check(!gate.observe(TsfKeyPhase::test_down, VK_OEM_PLUS).expand_candidates,
      "TSF probing cannot expand candidates");
check(gate.observe(TsfKeyPhase::key_down, VK_OEM_PLUS).expand_candidates,
      "formal equals key expands candidates");
```

- [ ] **Step 2: Run RED tests**

Run: `cmake --build build/windows-x64 --config Release --target piinput-candidate-grid-tests piinput-key-event-gate-tests --parallel 1`

Expected: FAIL because `key_event_gate.h` and its target do not exist.

- [ ] **Step 3: Implement a pure event gate and remove side effects from TSF test callbacks**

`OnTestKeyDown/Up` may call only const decision functions. Move `shift_toggle_` mutation and any lazy-load action to `OnKeyDown/Up`. Route candidate expansion through `KeyEventDecision::expand_candidates` from formal key-down only. Reset the grid after every raw-input generation change.

- [ ] **Step 4: Run focused GREEN tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(candidate-grid|key-event-gate|windows-source-regression)" --output-on-failure`

Expected: all selected tests pass; the source gate proves test callbacks contain no calls to `shift_toggle_`, engine loading or candidate navigation.

- [ ] **Step 5: Commit only Task 1 files**

```powershell
git add include/piinput/key_event_gate.h src/key_event_gate.cpp tests/key_event_gate_tests.cpp include/piinput/candidate_grid.h src/candidate_grid.cpp tests/candidate_grid_tests.cpp platform/windows/tsf/text_service.cpp tests/windows_source_regression.cmake CMakeLists.txt
git commit -m "fix: keep candidate rows collapsed until formal navigation"
```

### Task 2: Define a bounded, versioned IPC protocol

**Files:**
- Create: `include/piinput/host_protocol.h`
- Create: `src/host_protocol.cpp`
- Create: `tests/host_protocol_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `constexpr std::uint32_t host_protocol_v1 = 1;`
- Produces: `struct HostEnvelope { uint32_t version; uint64_t client_id; uint64_t session_id; uint64_t sequence; uint64_t generation; HostMessageType type; std::vector<std::byte> payload; };`
- Produces: `std::vector<std::byte> encode_host_envelope(const HostEnvelope&);`
- Produces: `std::optional<HostEnvelope> decode_host_envelope(std::span<const std::byte>, ProtocolError&);`

- [ ] **Step 1: Write failing serialization tests**

Cover round trip, unknown type, unsupported major version, payload over 1 MiB, truncated header, length mismatch, sequence zero and trailing bytes. The decoder must return a typed error without allocation based on an unchecked length.

- [ ] **Step 2: Run RED test**

Run: `cmake --build build/windows-x64 --config Release --target piinput-host-protocol-tests --parallel 1`

Expected: FAIL because protocol files and target do not exist.

- [ ] **Step 3: Implement fixed-width little-endian encoding**

Use explicit integer readers/writers and a 1 MiB payload ceiling. Do not serialize pointers, STL object layouts or platform `wchar_t`. UTF-8 text is length-prefixed inside typed payloads.

- [ ] **Step 4: Run GREEN and sanitizer-compatible core tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R piinput-host-protocol --output-on-failure`

Expected: protocol tests pass.

- [ ] **Step 5: Commit Task 2**

```powershell
git add include/piinput/host_protocol.h src/host_protocol.cpp tests/host_protocol_tests.cpp CMakeLists.txt
git commit -m "feat: define bounded PiInput host protocol"
```

### Task 3: Extract process-independent Host sessions

**Files:**
- Create: `include/piinput/host_session.h`
- Create: `src/host_session.cpp`
- Create: `tests/host_session_tests.cpp`
- Modify: `include/piinput/session.h`
- Modify: `src/session.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `HostEnvelope` from Task 2 and existing `ImeSession`, `EnglishSession`, `CandidateGrid`.
- Produces: `class HostSession` with `HostReply apply(const HostKeyEvent&)`, `HostSnapshot snapshot() const`, and `void restore(const HostResumeState&)`.
- Produces: `struct HostSnapshot { uint64_t generation; std::string raw; size_t caret; CandidateViewState view; std::vector<Candidate> candidates; };`

- [ ] **Step 1: Write failing session tests**

Test full-pinyin and Xiaohe input, English mode, punctuation, generation increments, stale candidate selection rejection, one-row reset after editing, explicit row expansion, snapshot restore and Host restart resume.

- [ ] **Step 2: Run RED test**

Run: `cmake --build build/windows-x64 --config Release --target piinput-host-session-tests --parallel 1`

Expected: FAIL because `HostSession` does not exist.

- [ ] **Step 3: Implement HostSession as composition owner**

Wrap existing core sessions without Win32 dependencies. Candidate choices use candidate ID plus generation. Editing always collapses the view. Host restart restore accepts raw input/caret/mode only and recomputes candidates; it never trusts serialized candidate pointers or indexes.

- [ ] **Step 4: Run GREEN core tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(host-session|core|english|candidate-grid)" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 5: Commit Task 3**

```powershell
git add include/piinput/host_session.h src/host_session.cpp tests/host_session_tests.cpp include/piinput/session.h src/session.cpp CMakeLists.txt
git commit -m "refactor: extract process-independent input host sessions"
```

### Task 4: Build the per-user Host and secured pipe server

**Files:**
- Create: `platform/windows/host/main.cpp`
- Create: `platform/windows/host/pipe_security.h`
- Create: `platform/windows/host/pipe_security.cpp`
- Create: `platform/windows/host/pipe_server.h`
- Create: `platform/windows/host/pipe_server.cpp`
- Create: `platform/windows/host/session_manager.h`
- Create: `platform/windows/host/session_manager.cpp`
- Create: `tests/pipe_security_tests.cpp`
- Create: `tests/host_process_tests.ps1`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 envelopes and Task 3 HostSession.
- Produces: `PiInputHost.exe --health`, `--serve`, `--drain`, and `--build-id`.
- Produces: pipe name `\\.\pipe\PiInput.Host.v1.<WindowsSessionId>` and a current-user-only security descriptor.

- [ ] **Step 1: Write failing security and process tests**

Verify the pipe ACL contains current user and LocalSystem only; a second Host returns the first instance build ID; health replies include protocol/build ID; malformed clients are disconnected; drain rejects new sessions but completes current requests.

- [ ] **Step 2: Run RED tests**

Run: `cmake --build build/windows-x64 --config Release --target PiInputHost piinput-pipe-security-tests --parallel 1`

Expected: FAIL because Host targets do not exist.

- [ ] **Step 3: Implement bounded asynchronous pipe handling**

Use overlapped Named Pipe I/O, one bounded input/output queue per client and a process-wide session manager. Never block a pipe thread on dictionary disk I/O; load the immutable engine snapshot before accepting clients.

- [ ] **Step 4: Run GREEN Windows tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(pipe-security|host-process)" --output-on-failure`

Expected: both tests pass without requiring admin rights.

- [ ] **Step 5: Commit Task 4**

```powershell
git add platform/windows/host tests/pipe_security_tests.cpp tests/host_process_tests.ps1 CMakeLists.txt
git commit -m "feat: add secured per-user PiInput host process"
```

### Task 5: Move candidate presentation into Host

**Files:**
- Create: `platform/windows/host/candidate_presenter.h`
- Create: `platform/windows/host/candidate_presenter.cpp`
- Move/Adapt: `platform/windows/tsf/candidate_window.*` into `platform/windows/host/`
- Create: `tests/candidate_presenter_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `HostSnapshot` and a screen-space caret rectangle sent by Shim.
- Produces: `CandidatePresenter::show(session_id, generation, snapshot, caret_rect)`, `hide(session_id)`, and `focus(session_id)`.

- [ ] **Step 1: Write failing presenter-model tests**

Test one-row default, explicit expansion, 6 phrase candidates per row, configured single-character count, monitor work-area clamping, DPI scaling, focus switching and stale generation rejection.

- [ ] **Step 2: Run RED tests**

Run: `cmake --build build/windows-x64 --config Release --target piinput-candidate-presenter-tests --parallel 1`

Expected: FAIL because Host presenter does not exist.

- [ ] **Step 3: Implement no-activate Host-owned candidate window**

Reuse drawing primitives but create the HWND in Host. Use `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`; never install global hooks. Candidate state comes exclusively from HostSession.

- [ ] **Step 4: Run GREEN tests and Host build**

Run: `cmake --build build/windows-x64 --config Release --target PiInputHost piinput-candidate-presenter-tests --parallel 1`

Expected: build and tests pass.

- [ ] **Step 5: Commit Task 5**

```powershell
git add platform/windows/host platform/windows/tsf/candidate_window.cpp platform/windows/tsf/candidate_window.h tests/candidate_presenter_tests.cpp CMakeLists.txt
git commit -m "refactor: move candidate presentation out of client processes"
```

### Task 6: Implement the stable TSF Shim client

**Files:**
- Create: `platform/windows/tsf/pipe_client.h`
- Create: `platform/windows/tsf/pipe_client.cpp`
- Create: `platform/windows/tsf/composition_mirror.h`
- Create: `platform/windows/tsf/composition_mirror.cpp`
- Create: `tests/composition_mirror_tests.cpp`
- Modify: `platform/windows/tsf/text_service.h`
- Modify: `platform/windows/tsf/text_service.cpp`
- Modify: `platform/windows/tsf/dllmain.cpp`
- Modify: `platform/windows/tsf/profile_registration.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Host protocol and fixed Pipe name from Tasks 2 and 4.
- Produces: `PipeClient::send_key`, `send_focus`, `send_caret`, `resume_session`, `disconnect`.
- Produces: `CompositionMirror` with raw/caret/mode/generation and last confirmed candidate snapshot.

- [ ] **Step 1: Write failing mirror and disconnected-host tests**

Verify sequence/generation monotonicity, stale reply discard, raw-input preservation, commit rollback, no-composition pass-through while disconnected, Enter raw commit during an existing disconnected composition and reentrancy depth limited to one.

- [ ] **Step 2: Run RED tests**

Run: `cmake --build build/windows-x64 --config Release --target piinput-composition-mirror-tests PiInputTSF --parallel 1`

Expected: mirror target fails because files do not exist.

- [ ] **Step 3: Shrink TextService to TSF and transport responsibilities**

Remove engine, dictionary, ranking, English lexicon and candidate-window ownership from `TextService`. It consumes keys using the stable local policy, sends formal events to Host and applies returned edit actions only after checking both `RequestEditSession` and `session_result`. Use a message-only HWND to schedule asynchronous Host responses without recursive message loops.

- [ ] **Step 4: Enforce stable identity and source boundaries**

Register CLSID `{13EB305F-2DA3-4CF7-8C45-16B016B801B5}` and Profile `{4ED27B7C-678E-4240-827A-24DA597F8D4B}`. Add a source regression test that rejects lexicon/engine/candidate-window includes or file reads inside the TSF target.

- [ ] **Step 5: Run GREEN Shim tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(composition-mirror|windows-source-regression|host-protocol)" --output-on-failure`

Expected: all pass and `PiInputTSF.dll` links without `piinput_core`.

- [ ] **Step 6: Commit Task 6**

```powershell
git add platform/windows/tsf tests/composition_mirror_tests.cpp tests/windows_source_regression.cmake CMakeLists.txt
git commit -m "refactor: reduce PiInput TSF to a stable host shim"
```

### Task 7: Add Host restart, reconnect and upgrade handoff

**Files:**
- Create: `platform/windows/host/upgrade_coordinator.h`
- Create: `platform/windows/host/upgrade_coordinator.cpp`
- Create: `tests/host_upgrade_handoff_tests.ps1`
- Modify: `platform/windows/host/main.cpp`
- Modify: `platform/windows/tsf/pipe_client.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `PiInputHost.exe --prepare-upgrade <new-build-id>`, `--takeover`, and `--shutdown-after-drain`.
- Produces: resume handshake containing client/session ID, raw input, caret, mode and generation.

- [ ] **Step 1: Write a failing two-client handoff test**

Start Host A and two fixture clients. Keep both clients alive, start Host B, perform takeover, then assert both clients report Host B build ID and preserve distinct composition strings. Terminate Host B unexpectedly and assert restart/reconnect works.

- [ ] **Step 2: Run RED integration test**

Run: `ctest --test-dir build/windows-x64 -C Release -R piinput-host-upgrade-handoff --output-on-failure`

Expected: FAIL because takeover commands do not exist.

- [ ] **Step 3: Implement drain and fixed-pipe takeover**

Host A stops accepting new sessions, completes bounded in-flight work, closes the fixed Pipe and exits. Host B acquires the single-instance mutex, owns the same Pipe and accepts resume handshakes. Shim retries with capped exponential delays and never blocks TSF callbacks.

- [ ] **Step 4: Run handoff and 100-cycle restart stress tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-host-(upgrade-handoff|restart-stress)" --output-on-failure`

Expected: 100/100 cycles pass with no client exit, lost raw composition, deadlock or sustained handle growth.

- [ ] **Step 5: Commit Task 7**

```powershell
git add platform/windows/host/upgrade_coordinator.* platform/windows/host/main.cpp platform/windows/tsf/pipe_client.cpp tests/host_upgrade_handoff_tests.ps1 CMakeLists.txt
git commit -m "feat: support seamless PiInput host handoff"
```

### Task 8: Implement transactional stable-runtime installation and Profile migration

**Files:**
- Create: `platform/windows/installer/stable_runtime.h`
- Create: `platform/windows/installer/stable_runtime.cpp`
- Create: `tests/stable_runtime_tests.cpp`
- Create: `tests/profile_migration_tests.cpp`
- Modify: `platform/windows/installer/install_layout.*`
- Modify: `platform/windows/installer/main.cpp`
- Modify: `platform/windows/installer/uninstall_layout.*`
- Modify: `platform/windows/uninstaller/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: stable Shim path `%LOCALAPPDATA%\PiInput\Runtime\Shim\PiInputTSF.dll`.
- Produces: version roots `%LOCALAPPDATA%\PiInput\Runtime\versions\<version-build-id>`.
- Produces: validated atomic `current.json` and `rollback.json` containing relative version IDs, hashes and protocol version.

- [ ] **Step 1: Write failing layout and transaction tests**

Test traversal rejection, relative marker validation, hash mismatch, new Profile verification failure rollback, old Profile removal only after success, locked legacy runtime preservation and ordinary upgrade leaving Shim unchanged.

- [ ] **Step 2: Run RED tests**

Run: `cmake --build build/windows-x64 --config Release --target piinput-stable-runtime-tests piinput-profile-migration-tests --parallel 1`

Expected: FAIL because stable runtime interfaces do not exist.

- [ ] **Step 3: Implement first migration transaction**

Stage and hash Shim/Host, health-check Host, register the permanent Profile, add it to the user keyboard list, verify activation in a fixture context, atomically switch current, then disable/remove the legacy Profile. Roll back all new registration and markers if any step fails.

- [ ] **Step 4: Implement ordinary Host-only upgrade**

When the installed Shim hash and protocol major match, do not replace or re-register it. Install a new Host version, request Task 7 handoff, switch markers and retain the last verified version for rollback.

- [ ] **Step 5: Update uninstaller**

Remove the permanent Profile and stable Shim only during explicit uninstall. Preserve user data unless the existing opt-in removal flag is set. Never terminate client applications.

- [ ] **Step 6: Run GREEN installer tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(stable-runtime|profile-migration|installer|uninstall)" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 7: Commit Task 8**

```powershell
git add platform/windows/installer platform/windows/uninstaller tests/stable_runtime_tests.cpp tests/profile_migration_tests.cpp CMakeLists.txt
git commit -m "feat: migrate PiInput to a stable runtime profile"
```

### Task 9: Add honest diagnostics and the developer test harness

**Files:**
- Create: `platform/windows/diagnostics/main.cpp`
- Create: `tests/diagnostics_tests.ps1`
- Modify: `platform/windows/preview/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `piinput-diagnostics.exe --status --json`.
- Reports: registered Profile, active Profile, Shim path/hash/protocol, connected Host build ID, connection count, legacy loaded modules and whether they are active.

- [ ] **Step 1: Write failing diagnostics fixture tests**

Create fixtures containing an active stable Shim plus loaded legacy module records. Assert output clearly separates `loaded_legacy_modules` from `active_profile`; never label a merely mapped old DLL as the active input version.

- [ ] **Step 2: Run RED test**

Run: `ctest --test-dir build/windows-x64 -C Release -R piinput-diagnostics --output-on-failure`

Expected: FAIL because diagnostic executable does not exist.

- [ ] **Step 3: Implement diagnostics and update `PiInput-Test.exe`**

The test app connects through IPC like a real Shim client and displays protocol/Host build ID. It must remain usable for engine and UI testing without loading `PiInputTSF.dll`.

- [ ] **Step 4: Run GREEN diagnostics tests**

Run: `ctest --test-dir build/windows-x64 -C Release -R "piinput-(diagnostics|preview)" --output-on-failure`

Expected: tests pass.

- [ ] **Step 5: Commit Task 9**

```powershell
git add platform/windows/diagnostics platform/windows/preview tests/diagnostics_tests.ps1 CMakeLists.txt
git commit -m "feat: report active PiInput shim and host versions"
```

### Task 10: Package, document and verify without installing

**Files:**
- Modify: `scripts/windows/package-release.ps1`
- Modify: `RELEASE_MANIFEST.md`
- Modify: `FILE_LIST.txt`
- Modify: `SHA256SUMS.txt`
- Modify: `docs/安装与使用指南.md`
- Modify: `docs/RELEASE_WORKFLOW.md`
- Create: `docs/稳定入口与无重启升级说明.md`
- Create: `docs/VERIFICATION_v0.4.0-dev.md`
- Modify: `VERSION`

**Interfaces:**
- Produces package root executables `PiInput-Install.exe`, `PiInput-Uninstall.exe`, `PiInput-Test.exe`.
- Produces packaged `bin/PiInputTSF.dll`, `bin/PiInputHost.exe`, diagnostics and required data.

- [ ] **Step 1: Add release gates for the new architecture**

The package script must fail if Host, stable Shim, diagnostic tool, protocol metadata, uninstaller, dictionaries or docs are missing. It must reject a TSF DLL that links the core engine or contains dictionary resource names.

- [ ] **Step 2: Build all Release targets**

Run: `cmake --build build/windows-x64 --config Release --parallel 1`

Expected: exit code 0 and all executables/DLLs generated.

- [ ] **Step 3: Run the complete CTest suite**

Run: `ctest --test-dir build/windows-x64 -C Release --output-on-failure`

Expected: 100% pass, including protocol, Host restart, candidate one-row, full-pinyin, Xiaohe, dictionary, English, installer and uninstaller gates.

- [ ] **Step 4: Run static/package checks**

Run PowerShell syntax parsing for all `.ps1`; run `git diff --check`; regenerate `FILE_LIST.txt` from tracked/nonignored source files excluding build/dist/artifacts; regenerate `SHA256SUMS.txt`; verify every listed hash.

- [ ] **Step 5: Build the local ZIP without running installer**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows/package-release.ps1`

Expected: one versioned ZIP under `artifacts`, with printed SHA-256. Do not invoke the installer.

- [ ] **Step 6: Write final installation and usage instructions**

Document the one-time Profile migration, ordinary no-restart upgrades, how to verify active Host build ID, candidate one-row/`=` behavior, `PiInput-Test.exe`, dictionary placement, uninstall/reinstall and rollback.

- [ ] **Step 7: Commit Task 10 locally**

```powershell
git add scripts/windows/package-release.ps1 RELEASE_MANIFEST.md FILE_LIST.txt SHA256SUMS.txt VERSION docs
git commit -m "release: package stable-host PiInput v0.4.0 dev"
```

Do not push. Hand the ZIP path, SHA-256, verification record and installation guide to the user for manual installation and feedback.
