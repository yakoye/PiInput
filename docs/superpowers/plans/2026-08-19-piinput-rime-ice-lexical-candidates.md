# PiInput Rime Ice Lexical Candidates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PiInput's public Chinese candidate dictionary with the default Rime Ice dictionary and make normal candidates contain all real multi-character words before single-character fallbacks, with no mechanically assembled sentences.

**Architecture:** The dictionary builder resolves Rime Ice's master `import_tables` into one first-source-wins binary lexicon. `Engine` then performs lexical span queries directly instead of publishing `IncrementalDecoder` sentence paths. Partial-word selection reuses `SegmentSelection`, and only a confirmed final application commit writes whole-phrase, component, and adjacent-pair learning to the personal model.

**Tech Stack:** C++20, CMake/CTest, PowerShell dictionary pipeline, PiInput binary lexicon, Windows Host/TSF protocol.

**Spec:** `docs/superpowers/specs/2026-08-19-piinput-lexical-candidate-quality-design.md`

## Global Constraints

- Keep the PiInput engine, Host process, TSF Shim, candidate window, and English input path.
- The release Chinese binary lexicon uses only Rime Ice default imports: `8105`, `base`, `ext`, `tencent`, and `others`.
- Do not publish `decoded_sentence` or any multi-entry path as a normal Chinese candidate.
- List every real multi-character lexical candidate before the first-syllable single-character fallback; do not reserve a single-character quota.
- Deduplicate candidates by displayed text.
- Keep `%LOCALAPPDATA%\PiInput\UserData\user_model.tsv` as a mutable personal overlay and never package it.
- Learn only after the application confirms the final commit succeeded.
- By the user's request, implement first and add focused regression tests immediately afterwards rather than using fail-first TDD.
- Preserve the dirty worktree and do not create commits unless the user separately asks for them.

---

### Task 1: Import the Rime Ice master dictionary deterministically

**Files:**
- Modify: `include/piinput/dictionary_builder.h`
- Modify: `src/dictionary_builder.cpp`
- Modify: `tools/dictionary_builder/main.cpp`
- Modify: `tests/dictionary_builder_tests.cpp`
- Create: `tests/data/dictionary_builder/rime_master.dict.yaml`
- Create: `tests/data/dictionary_builder/rime_base.dict.yaml`
- Create: `tests/data/dictionary_builder/rime_ext.dict.yaml`

**Interfaces:**
- Produces: `RimeDictionaryImportReport` with per-source reports plus total `raw_entries`, `accepted_entries`, and `duplicate_entries`.
- Produces: `read_rime_dictionary(const std::filesystem::path&, std::uint32_t, RimeDictionaryImportReport&) -> std::vector<LexiconCandidate>`.
- Consumes: existing `read_dictionary_source(..., DictionarySourceFormat::rime_yaml, ...)` for each resolved table.

- [ ] **Step 1: Add the master-dictionary API**

Add this public shape to `dictionary_builder.h`:

```cpp
struct RimeDictionaryImportReport {
    std::vector<std::filesystem::path> source_files;
    std::size_t raw_entries{};
    std::size_t accepted_entries{};
    std::size_t duplicate_entries{};
};

[[nodiscard]] std::vector<LexiconCandidate> read_rime_dictionary(
    const std::filesystem::path& master_path,
    std::uint32_t default_weight,
    RimeDictionaryImportReport& report);
```

- [ ] **Step 2: Parse only active `import_tables` entries**

In `dictionary_builder.cpp`, read the YAML header before `...`, enter the `import_tables:` list, ignore blank/commented entries, strip inline comments, and resolve each active item relative to the master file. Append `.dict.yaml` when the table name omits it. Do not import the master's post-`...` schema extras.

- [ ] **Step 3: Preserve Rime's first-table-wins duplicate rule**

For each table in declared order, parse entries with the existing Rime reader and insert only the first `(word, canonical_pinyin)` key. Preserve its original Rime weight. Count later copies in `duplicate_entries`; keep different pronunciations until the runtime display-text deduper chooses the pronunciation matching current input.

- [ ] **Step 4: Add the CLI entry point and machine-readable report**

Extend `piinput-dictionary-builder` with:

```text
--rime-dictionary <master.dict.yaml> <default-weight>
--rime-report <report.tsv>
```

The report header is:

```text
file\traw_entries\taccepted_entries\tduplicate_entries
```

Reject a missing master file, an empty active import list, a missing imported table, or a build yielding zero accepted entries before writing output.

- [ ] **Step 5: Add importer regression coverage**

Create a fixture whose base and ext tables both contain `边框\tbian kuang` with different weights. Verify that imports resolve relative to the master, spaces become apostrophes, the base copy wins even when ext has a larger weight, commented imports are ignored, and report counts match.

- [ ] **Step 6: Build and run the focused test**

Run:

```powershell
cmake --build build/windows-x64 --config Release --target piinput_tests piinput-dictionary-builder
ctest --test-dir build/windows-x64 -C Release -R dictionary_builder --output-on-failure
```

Expected: dictionary builder tests pass and no existing source-format test regresses.

---

### Task 2: Make Rime Ice the only release Chinese candidate source

**Files:**
- Modify: `scripts/build-dictionaries.ps1`
- Modify: `tests/dictionary_script_regression.cmake`
- Modify: `dictionary_sources.json`
- Modify: `docs/词库更新说明.md`

**Interfaces:**
- Consumes: `piinput-dictionary-builder --rime-dictionary` from Task 1.
- Produces: `dicts/generated/piinput-combined.tsv`, `dicts/cache/piinput-base.lex`, and `dicts/generated/rime-ice-import-report.tsv`.

- [ ] **Step 1: Add an explicit Rime Ice root parameter**

Add:

```powershell
[string]$RimeIceRoot = (Join-Path $DictionaryRoot 'rime-ice-full')
```

Resolve `rime_ice.dict.yaml` under it and fail before changing cache files when the master or any active default table is missing.

- [ ] **Step 2: Replace the combined source argument list**

Remove release-candidate arguments for `data/base_lexicon.tsv`, `pinyin-data`, `phrase-pinyin-data`, `rime-pinyin-simp`, THUOCL, SCEL, and `dicts/user/*.tsv`. Invoke only:

```powershell
--rime-dictionary $rimeMaster 1
--rime-report $rimeReportNew
```

Keep any pronunciation data needed by build tooling isolated from the candidate TSV. Continue atomic `.new` generation, validation, and rename into cache.

- [ ] **Step 3: Record reproducible Rime Ice source state**

Write the master version plus SHA-256 for the master and five active tables into the cache manifest. Treat any hash change as a rebuild trigger.

- [ ] **Step 4: Update script regression checks**

Verify the script contains the Rime master entry point and no longer passes old Chinese candidate sources to the builder. Use a temporary fixture root so CI does not depend on the user's absolute directory.

- [ ] **Step 5: Build the user's full Rime Ice lexicon**

Run:

```powershell
& .\scripts\build-dictionaries.ps1 -RimeIceRoot 'C:\Users\color\Downloads\PiInput\dicts\rime-ice-full'
```

Expected: the report lists exactly five active tables, the generated TSV has more entries than the old 101,474-entry cache, and the cache replacement occurs only after validation succeeds.

---

### Task 3: Replace normal sentence decoding with lexical candidate layers

**Files:**
- Modify: `include/piinput/engine.h`
- Modify: `src/engine.cpp`
- Modify: `tests/lexicon_query_tests.cpp`
- Modify: `tests/incremental_decoder_tests.cpp` only where tests incorrectly assume normal `Engine::query` publishes sentence paths

**Interfaces:**
- Produces: `Engine::query_lexical_span_unlocked(...)` as a private helper shared by normal and staged queries.
- Preserves: public `Engine::query(...) -> std::vector<EngineCandidate>`.
- Changes: `Engine::query_segment(..., const SettingsSnapshot&)` receives settings so personal learning and candidate limits are consistent.

- [ ] **Step 1: Add one lexical collection helper**

The helper accepts parsed syllables, a starting offset, trailing prefix, result limit, and settings. It gathers:

```text
full-input personal phrases
full-input Rime Ice entries
all multi-character exact spans from longest to two syllables
one-entry incomplete completions from the current input prefix
first-unresolved-syllable single characters
```

Query the whole canonical string separately so learned or Rime phrases longer than the existing eight-syllable span cap remain reachable. The eight-syllable cap may remain for shorter prefix-word exploration.

- [ ] **Step 2: Remove normal publication of decoded sentence paths**

Keep parse generation and full-pinyin/double-pinyin variants, but do not append candidates with `CandidateKind::decoded_sentence`. For incomplete input, accept only a single real lexicon entry; reject candidates with `word_count > 1` or any prepended path text.

- [ ] **Step 3: Implement fixed candidate groups**

Assign groups in this order:

```text
0 = full-input personal or lexicon entry
1 = multi-character prefix entry
2 = first-syllable single character
```

Within a group: pinned first, learned next, larger `consumed_syllables`, larger Rime weight, stable text order. A learned group-1 word must not jump over a group-0 full-input phrase.

- [ ] **Step 4: Deduplicate by displayed text**

Use `candidate.word` as the final map key. When entries collide, keep the lower group; within the group keep pinned/learned evidence and then the better Rime weight. Preserve the matching canonical pinyin from the winning entry for learning and consumed-syllable accounting.

- [ ] **Step 5: Remove the phrase/single quota in staged queries**

Change `query_segment` to append every available multi-character candidate up to `result_limit`; only append single characters when capacity remains. Delete the current `limit / 3` phrase budget.

- [ ] **Step 6: Add lexical-order regressions after implementation**

Use compact test lexicons to assert:

```text
bian'kuang -> 边框, 编筐, then single characters; never 便狂/边狂/边矿
kai'wu -> 开悟, 开物, then single characters; never 开无/开五/开唔
20 real phrase entries -> indices 0..19 are phrases and the first single is index 20
duplicate 黑窗口 with two pronunciations/sources -> one displayed candidate
```

Also assert every normal result has evidence `user_phrase`, `exact_lexicon`, `prefix_lexicon`, `single_character`, or a one-word incomplete completion; none has `decoded_sentence`.

- [ ] **Step 7: Run focused engine tests**

Run:

```powershell
cmake --build build/windows-x64 --config Release --target piinput_tests piinput-ime-cli
ctest --test-dir build/windows-x64 -C Release -R 'lexicon_query|incremental_decoder' --output-on-failure
```

Expected: lexical query tests pass; decoder-internal tests may still test `IncrementalDecoder` itself, but normal Engine tests contain no assembled sentence candidates.

---

### Task 4: Preserve remaining pinyin when a normal prefix word is selected

**Files:**
- Modify: `include/piinput/session.h`
- Modify: `src/session.cpp`
- Modify: `include/piinput/segment_selection.h`
- Modify: `src/segment_selection.cpp`
- Modify: `include/piinput/host_session.h`
- Modify: `src/host_session.cpp`
- Modify: `tests/segment_selection_tests.cpp`
- Modify: `tests/host_session_tests.cpp`

**Interfaces:**
- Changes: `ImeSession::choose(std::uint64_t) -> SegmentStageResult`.
- Extends: `SegmentStageResult` with `std::vector<SegmentSelectionEntry> learning_segments` on final completion.
- Extends: `PendingLearning` with the confirmed whole phrase and selected component list.

- [ ] **Step 1: Expose immutable selected segment history**

Move the segment history value type into the public header:

```cpp
struct SegmentSelectionEntry {
    std::string word;
    std::string pinyin;
    std::size_t consumed_syllables{};
};

[[nodiscard]] const std::vector<SegmentSelectionEntry>& history() const noexcept;
```

- [ ] **Step 2: Return update-or-commit from normal choose**

For a full-input candidate, return a committed `SegmentStageResult` and clear as today. For a prefix candidate, parse the current composition, begin `SegmentSelection`, stage the candidate, keep its text in composition, refresh candidates for the remaining offset, and return `accepted=true` with no `commit_text`.

- [ ] **Step 3: Route partial normal selection through HostAction update**

In `HostSession`, treat a successful choose with no `commit_text` exactly like an in-progress staged selection: advance generation, select the first remaining lexical candidate, and return `HostAction::update`. Do not create pending learning yet.

- [ ] **Step 4: Carry learning evidence only on final completion**

When the final segment completes, return full text, full canonical pinyin, and a copy of the selected segments. Store this in `pending_learning_` under the generation sent with `HostAction::commit`.

- [ ] **Step 5: Record whole, component, and adjacent entries after commit confirmation**

On `confirm_commit(generation, true)`:

```text
record_composed_phrase(full_pinyin, full_text)
record_selection(segment.pinyin, segment.word) for each selected multi-character segment
record_composed_phrase(left.pinyin + "'" + right.pinyin, left.word + right.word)
    for each adjacent pair
```

Do not record single-character components as personal words. On `succeeded=false`, record nothing.

- [ ] **Step 6: Add post-implementation session regressions**

Verify selecting “边框” from a longer input leaves the suffix pinyin, selecting the remaining words commits once, a failed commit leaves the model unchanged, and a successful commit makes the whole phrase the first full-input candidate in a fresh session. Verify local selected words and adjacent pairs are also queryable from the same persisted model.

- [ ] **Step 7: Run focused session tests**

Run:

```powershell
cmake --build build/windows-x64 --config Release --target piinput_tests
ctest --test-dir build/windows-x64 -C Release -R 'segment_selection|host_session|user_model' --output-on-failure
```

Expected: all selection, commit-confirmation, stale-generation, pin, delete, and persistence tests pass.

---

### Task 5: Validate real Rime Ice candidate quality and performance

**Files:**
- Modify: `tests/data/unseen_generalization_corpus_2026-08-15.txt`
- Modify: `scripts/test-real-world-corpus.ps1`
- Modify: `tests/dictionary_query_cli_regression.ps1`
- Modify: `tools/benchmark/main.cpp` only if the current output cannot isolate lexical query latency

**Interfaces:**
- Consumes: full Rime Ice binary from Task 2 and lexical Engine from Task 3.
- Produces: candidate-quality and latency evidence without changing the dictionary.

- [ ] **Step 1: Add the user's quality cases**

Cover `shuliyixia`, `jiajinqu`, `biankuang`, `heichuangkou`, `kaiwu`, `houxuan`, and long inputs corresponding to “边框需要处理一下”“这个东西很丝滑”“每个人都需要开悟”. Mark expected real entries and forbidden mechanical strings separately; do not require exact Sogou order where Rime Ice weights differ.

- [ ] **Step 2: Query the generated binary lexicon**

Run the CLI against `dicts/cache/piinput-base.lex` and save a short diagnostic table containing rank, display text, consumed syllables, and evidence kind. Confirm forbidden strings are absent and display duplicates are absent.

- [ ] **Step 3: Measure hot lexical query latency**

Run the Release benchmark with the million-entry lexicon. Required result: hot P95 at most 5 ms and individual query maximum at most 20 ms. If the limit fails, optimize prefix lookup/cache bounds without reducing the phrase-before-character rule.

- [ ] **Step 4: Run the complete automated suite**

Run:

```powershell
cmake --build build/windows-x64 --config Release
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

Expected: all tests pass.

---

### Task 6: Build a local test package and document the observed boundary

**Files:**
- Modify: `docs/词库更新说明.md`
- Create: `docs/VERIFICATION_RIME_ICE_LEXICAL_CANDIDATES_2026-08-19.md`
- Modify only if packaging metadata requires it: `RELEASE_MANIFEST.md`, `SHA256SUMS.txt`

**Interfaces:**
- Consumes: verified binaries and Rime Ice cache from Tasks 1–5.
- Produces: a local Windows package for manual Notepad++ and other real-application testing.

- [ ] **Step 1: Document the source replacement and learning behavior**

Explain that Rime Ice supplies the public Chinese lexicon, PiInput supplies the engine and personal overlay, all real words precede single characters, and a first-time contextual order can differ from Sogou until the user selects once.

- [ ] **Step 2: Package without changing the release version**

Use the existing Windows packaging workflow to make a development artifact containing the newly generated `piinput-base.lex`. Do not publish or overwrite the named v0.7.0 release artifact unless the user asks for a release.

- [ ] **Step 3: Verify package contents and hashes**

Confirm the package contains the new binary lexicon and no `user_model.tsv`, old combined TSV, Rime YAML source files, or stale base lexicon. Record package path, SHA-256, lexicon entry count, build report counts, complete test result, and benchmark numbers.

- [ ] **Step 4: Hand off real-application checks**

Ask the user to install the development artifact and type the agreed quality strings. Any ordering difference is then classified as missing Rime entry, Rime weight, personal-learning behavior, or engine bug; do not add mechanical candidates to hide it.
