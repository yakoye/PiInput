#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace piinput {

enum class EnglishKeyKind {
    letter,
    punctuation,
    literal,
    digit,
    space,
    enter,
    escape,
    backspace,
    delete_forward,
    move_left,
    move_right,
    move_home,
    move_end,
    previous_row,
    next_row,
    previous_page,
    next_page,
    other,
};

enum class EnglishKeyAction {
    pass_through,
    start_composition,
    insert_letter,
    commit_then_punctuation,
    commit_then_literal,
    choose_digit,
    choose_current,
    commit_raw,
    cancel,
    backspace,
    delete_forward,
    move_left,
    move_right,
    move_home,
    move_end,
    previous_row,
    next_row,
    previous_page,
    next_page,
};

struct EnglishCommitPlan {
    std::string text;
    bool used_candidate{};

    bool operator==(const EnglishCommitPlan&) const = default;
};

[[nodiscard]] bool edit_session_succeeded(
    std::int32_t request_result,
    std::int32_t session_result) noexcept;

[[nodiscard]] EnglishCommitPlan build_english_commit_plan(
    std::string_view raw_input,
    std::optional<std::string_view> candidate,
    std::string_view suffix);

struct EnglishKeyContext {
    bool english_mode{};
    bool enabled{};
    bool composing{};
    bool resources_loaded{};
};

struct EnglishKeyDecision {
    EnglishKeyAction action{EnglishKeyAction::pass_through};
    bool consume{};
    bool load_resources{};

    bool operator==(const EnglishKeyDecision&) const = default;
};

class EnglishKeyPolicy final {
public:
    [[nodiscard]] static EnglishKeyDecision decide(
        const EnglishKeyContext& context,
        EnglishKeyKind key) noexcept;
};

[[nodiscard]] EnglishKeyKind classify_english_ascii_key(
    char key,
    bool shifted) noexcept;

}  // namespace piinput
