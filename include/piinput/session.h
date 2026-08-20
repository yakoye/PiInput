#pragma once

#include "piinput/engine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace piinput {

enum class SessionCandidateRole : std::uint8_t {
    normal,
    segment,
    placeholder,
};

struct SessionCandidate {
    std::uint64_t id{};
    EngineCandidate candidate;
    SessionCandidateRole role{SessionCandidateRole::normal};
};

enum class CandidateViewMode : std::uint8_t {
    normal,
    segment_selection,
};

struct SegmentStageResult {
    bool accepted{};
    std::optional<std::string> commit_text;
    std::string selection_pinyin;
    bool user_created{};
    std::vector<SegmentSelectionEntry> learning_segments;
};

struct CandidateSnapshot {
    std::uint64_t generation{};
    std::string input;
    std::size_t caret{};
    std::vector<SessionCandidate> candidates;
    CandidateViewMode view_mode{CandidateViewMode::normal};
    std::string staged_text;
    std::string remaining_pinyin;
    std::size_t normal_browse_candidate_count{};
    std::size_t segment_candidate_offset{};
    SegmentSelection segment_selection;
};

class ImeSession final {
public:
    explicit ImeSession(Engine& engine, std::string schema = "full", std::size_t candidate_limit = 90U);
    ImeSession(Engine& engine, std::string schema, SettingsSnapshot settings);

    void set_schema(std::string schema);
    void set_input(std::string input);
    void insert(char character);
    bool backspace();
    bool delete_forward();
    bool move_left();
    bool move_right();
    void move_home() noexcept;
    void move_end() noexcept;
    void clear();
    void restore(CandidateSnapshot snapshot);

    [[nodiscard]] const CandidateSnapshot& snapshot() const noexcept;
    [[nodiscard]] SegmentStageResult choose(std::uint64_t candidate_id);
    // `retained_normal_candidates` is the number of ordinary dictionary
    // candidates the user is already looking at. Segment selection is appended
    // below them instead of replacing them, so pressing '=' never makes the
    // real words the user could still pick disappear.
    [[nodiscard]] bool enter_segment_selection(std::size_t retained_normal_candidates = 0U);
    [[nodiscard]] bool leave_segment_selection();
    [[nodiscard]] SegmentStageResult stage_candidate(std::uint64_t candidate_id);
    [[nodiscard]] bool undo_segment();
    void record_committed_selection(const std::string& pinyin, const std::string& word);
    void record_composed_phrase(const std::string& pinyin, const std::string& word);
    void pin_candidate(const EngineCandidate& candidate);
    void unpin_candidate(const EngineCandidate& candidate);
    void delete_candidate(const EngineCandidate& candidate);

private:
    void refresh();
    void refresh_segment();
    void publish_cached_normal_candidates();

    Engine* engine_{};
    std::string schema_;
    std::size_t candidate_limit_{90U};
    SettingsSnapshot settings_;
    CandidateSnapshot snapshot_;
    std::vector<EngineCandidate> normal_candidates_cache_;
    std::size_t retained_normal_candidates_{};
};

}  // namespace piinput
