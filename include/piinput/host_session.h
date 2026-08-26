#pragma once

#include "piinput/candidate_grid.h"
#include "piinput/english_completion.h"
#include "piinput/english_session.h"
#include "piinput/punctuation.h"
#include "piinput/session.h"
#include "piinput/settings.h"
#include "piinput/symbols.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace piinput {

enum class HostInputMode : std::uint8_t {
    chinese,
    english,
};

enum class HostKeyKind : std::uint8_t {
    text,
    backspace,
    delete_forward,
    move_left,
    move_right,
    move_home,
    move_end,
    previous_candidate,
    next_candidate,
    expand_next_row,
    previous_row,
    select_digit,
    select_candidate,
    punctuation,
    space,
    enter,
    escape,
    switch_to_chinese,
    switch_to_english,
    literal_punctuation,
    open_symbol_center,
};

enum class HostAction : std::uint8_t {
    none,
    update,
    commit,
    cancel,
    pass_through,
    launch_symbol_tool,
    launch_settings,
    launch_program,
};

enum class CandidateManagementAction : std::uint8_t {
    pin_first,
    unpin,
    delete_candidate,
};

struct HostResumeState final {
    std::uint64_t generation{};
    std::string raw;
    std::size_t caret{};
    HostInputMode mode{HostInputMode::chinese};

    bool operator==(const HostResumeState&) const = default;
};

struct HostKeyEvent final {
    HostKeyKind kind{HostKeyKind::text};
    char character{};
    std::uint64_t candidate_id{};
    bool shifted{};
    std::optional<HostResumeState> resume;
};

struct HostCandidate final {
    std::uint64_t id{};
    std::string text;
    std::string pinyin;
    std::int64_t score{};

    bool operator==(const HostCandidate&) const = default;
};

struct CandidateViewState final {
    bool expanded{};
    std::size_t items_per_row{};
    std::size_t visible_rows{1U};
    std::size_t active_row{};
    std::size_t first_visible_row{};
    std::size_t active_column{};
    enum class Mode : std::uint8_t { normal, segment_selection } mode{Mode::normal};
};

using HostCandidateMode = CandidateViewState::Mode;

struct HostSnapshot final {
    std::uint64_t generation{};
    std::string raw;
    std::string composition_text;
    std::size_t caret{};
    HostInputMode mode{HostInputMode::chinese};
    CandidateViewState view;
    std::vector<HostCandidate> candidates;
};

struct HostReply final {
    bool accepted{};
    HostAction action{HostAction::none};
    std::string text;
    HostSnapshot snapshot;
};

class HostSession final {
public:
    HostSession(
        Engine& engine,
        EnglishLexicon* english_lexicon,
        SettingsSnapshot settings,
        std::string schema);
    HostSession(
        Engine& engine,
        EnglishLexicon* english_lexicon,
        SymbolIndex* symbol_index,
        SettingsSnapshot settings,
        std::string schema);

    [[nodiscard]] HostReply apply(const HostKeyEvent& event);
    [[nodiscard]] HostReply manage_candidate(
        std::uint64_t candidate_id,
        CandidateManagementAction action);
    [[nodiscard]] HostSnapshot snapshot() const;
    // Cheap composition test for callers that only need to know whether raw
    // input is pending; building a full snapshot for this copies every
    // candidate string.
    [[nodiscard]] bool composing() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] HostResumeState resume_state() const;
    void restore(const HostResumeState& state);
    void start_after_generation(std::uint64_t previous_generation) noexcept;
    [[nodiscard]] bool confirm_commit(std::uint64_t generation, bool succeeded);

private:
    struct PendingLearning final {
        std::string canonical_pinyin;
        std::string word;
        bool user_created{};
        std::vector<SegmentSelectionEntry> segments;
    };

    [[nodiscard]] bool edit(const HostKeyEvent& event);
    [[nodiscard]] HostReply choose(std::uint64_t candidate_id);
    [[nodiscard]] HostReply reply(bool accepted, HostAction action, std::string text = {}) const;
    void advance_generation(bool collapse_view);
    void rebuild_candidate_grid(bool collapse_view);
    // Recomputes english_plan_ and english_insert_at_ for the current input.
    void rebuild_english_plan();
    // Keystroke-hot accessors. They answer the questions apply() actually asks
    // without materializing a full HostSnapshot, which would copy every
    // candidate word and pinyin string on every key.
    [[nodiscard]] std::size_t current_candidate_count() const noexcept;
    // Moves the selection onto the first per-syllable choice appended below the
    // retained word rows, so entering segment selection and staging one
    // character both land on the syllable that still needs resolving.
    void select_first_segment_candidate();
    // Replaces the candidate list with the formats behind a datetime_group
    // entry. False when there are none, which leaves the entry acting as an
    // ordinary candidate.
    [[nodiscard]] bool open_datetime_menu(const std::string& reading);
    void close_datetime_menu() noexcept;
    [[nodiscard]] const std::string& current_raw() const noexcept;
    [[nodiscard]] std::size_t selected_candidate_index() const noexcept;
    [[nodiscard]] std::uint64_t candidate_id_at(std::size_t index) const noexcept;

    SettingsSnapshot settings_;
    std::string schema_;
    ImeSession chinese_;
    // Kept so the date and time formats can be regenerated when the list is
    // opened, rather than carrying strings that were current a keystroke ago.
    Engine* engine_{};
    // Non-empty while the candidate list is the date or time formats rather
    // than the dictionary. The reading says which set it is.
    std::vector<std::string> datetime_menu_;
    std::string datetime_reading_;
    EnglishLexicon* english_lexicon_{};
    SymbolIndex* symbol_index_{};
    std::unique_ptr<EnglishSession> english_;
    HostInputMode mode_{HostInputMode::chinese};
    std::uint64_t generation_{1U};
    // The English words mixed into the current Chinese row, and where they
    // sit in it. Computed once per generation in rebuild_candidate_grid(),
    // because the row's length, the snapshot and candidate selection must all
    // agree on it -- recomputing in each would risk them drifting apart.
    EnglishCompletionPlan english_plan_;
    std::size_t english_insert_at_{};
    CandidateGrid candidate_grid_;
    std::size_t normal_return_index_{};
    PunctuationTransformer punctuation_;
    std::map<std::uint64_t, PendingLearning> pending_learning_;
};

}  // namespace piinput
