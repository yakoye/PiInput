#pragma once

#include <string>
#include <string_view>

namespace piinput {

// Smart punctuation is deliberately independent of TSF and the Host.  The
// caller supplies a stable text snapshot; this engine supplies only a semantic
// decision.  That keeps application callback quirks out of the punctuation
// rules and makes every rule directly testable.
enum class SmartPunctuationAction {
    transform,
    literal,
    provisional,
};

struct SmartPunctuationContext final {
    char symbol{};
    std::string_view left_text;
    std::string_view right_text;
    bool composing{};
};

struct SmartPunctuationDecision final {
    SmartPunctuationAction action{SmartPunctuationAction::transform};
    std::string_view rule_id{"PUNC-DEFAULT"};
    std::string_view chinese_text;
    std::string_view context_type{"UNKNOWN"};
};

struct SmartPunctuationResolution final {
    bool keep_ascii{};
    std::string_view rule_id{"PUNC-PENDING-CHINESE"};
    std::string_view chinese_text;
    bool continue_provisional{};
};

class SmartPunctuationEngine final {
public:
    [[nodiscard]] SmartPunctuationDecision decide(
        const SmartPunctuationContext& context) const noexcept;
    [[nodiscard]] SmartPunctuationResolution resolve_provisional(
        char symbol,
        char next_character,
        std::string_view provisional_rule_id = {},
        std::string_view accumulated_text = {}) const noexcept;

    [[nodiscard]] static bool is_ascii_digit(char value) noexcept;

private:
    // decide() trims the right context to this line before calling this, so
    // every rule inside reads `right_text` as "text the caret sits in front
    // of" rather than "anything at all after the caret".
    [[nodiscard]] SmartPunctuationDecision decide_on_line(
        const SmartPunctuationContext& context) const noexcept;
};

}  // namespace piinput
