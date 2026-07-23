#pragma once

namespace piinput {

enum class EnglishKeyKind {
    letter,
    punctuation,
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

}  // namespace piinput
