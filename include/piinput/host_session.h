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
    // 刚刚透传出去的那串数字，用来给数字常用语出补全建议。
    //
    // 数字键的处理一个字节都没改：没有合成串时它们照旧直接落进文档。这个事件
    // 是叠在上面的一层——Shim 把已经打出去的数字串报过来，Host 据此决定要不要
    // 在候选窗里给出建议，选中时只补齐没打完的那部分。所以它不吃键、不改文档，
    // 最坏情况只是建议没出现。
    digit_run,
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
    // 只有 digit_run 用它，装的是刚打出去的数字串。协议 v7 起才上线，
    // 旧版本不编码也不解码。
    std::string text_payload;
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
    // 按刚打出的数字串重算建议。
    [[nodiscard]] HostReply apply_digit_run(const std::string& run);
    void clear_digit_suggestions();
    // apply() 的其余部分。拆出来是为了让「先把数字建议处理掉」成为一道没有例外
    // 的前置关卡，而不是散落在后面每个分支里各自记得清一次。
    [[nodiscard]] HostReply apply_after_digit_suggestions(const HostKeyEvent& event);
    void close_datetime_menu() noexcept;
    [[nodiscard]] const std::string& current_raw() const noexcept;
    // 合成串整体上屏时该写出的文本。分段选择状态下 current_raw() 只剩未处理的
    // 拼音，已经落定的那部分在 staged_text 里——拿 current_raw() 去提交会把它
    // 丢掉。快照和标点的边界处理都要这个值，所以只留一份定义。
    [[nodiscard]] std::string pending_composition_text() const;
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
    // 数字常用语的补全建议。非空时候选列表就是这些建议，而**合成串是空的**——
    // 数字早已落进文档，这里只是叠在上面的一层提示。每条存的是「还没打完的那
    // 部分」，选中时只插入它，所以既不需要删除已上屏的数字，也不需要重写周边
    // 文本，在 TSF 支持差的终端里一样成立。
    std::vector<std::string> digit_suggestions_;
    // 产生这批建议的那串数字。换了串就要重算，相同就不动，免得每个数字键都把
    // 候选窗重建一次。
    std::string digit_run_;
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
