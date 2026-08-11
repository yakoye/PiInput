#include "piinput/english_key_policy.h"

namespace piinput {

bool edit_session_succeeded(
    const std::int32_t request_result,
    const std::int32_t session_result) noexcept {
    return request_result >= 0 && session_result >= 0;
}

EnglishCommitPlan build_english_commit_plan(
    const std::string_view raw_input,
    const std::optional<std::string_view> candidate,
    const std::string_view suffix) {
    EnglishCommitPlan plan;
    plan.used_candidate = candidate.has_value();
    const std::string_view base = candidate.value_or(raw_input);
    plan.text.reserve(base.size() + suffix.size());
    plan.text.append(base);
    plan.text.append(suffix);
    return plan;
}

EnglishKeyKind classify_english_ascii_key(const char key, const bool shifted) noexcept {
    if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')) {
        return EnglishKeyKind::letter;
    }
    if (key >= '0' && key <= '9') {
        if (shifted) {
            return EnglishKeyKind::punctuation;
        }
        return key == '0' ? EnglishKeyKind::literal : EnglishKeyKind::digit;
    }
    return EnglishKeyKind::other;
}

EnglishKeyDecision EnglishKeyPolicy::decide(
    const EnglishKeyContext& context,
    const EnglishKeyKind key) noexcept {
    if (!context.english_mode || !context.enabled) {
        return {};
    }
    if (!context.composing) {
        if (key == EnglishKeyKind::letter) {
            return {
                EnglishKeyAction::start_composition,
                true,
                !context.resources_loaded,
            };
        }
        return {};
    }

    EnglishKeyAction action = EnglishKeyAction::pass_through;
    switch (key) {
    case EnglishKeyKind::letter: action = EnglishKeyAction::insert_letter; break;
    case EnglishKeyKind::punctuation:
        action = EnglishKeyAction::commit_then_punctuation;
        break;
    case EnglishKeyKind::literal:
        action = EnglishKeyAction::commit_then_literal;
        break;
    case EnglishKeyKind::digit: action = EnglishKeyAction::choose_digit; break;
    case EnglishKeyKind::space: action = EnglishKeyAction::choose_current; break;
    case EnglishKeyKind::enter: action = EnglishKeyAction::commit_raw; break;
    case EnglishKeyKind::escape: action = EnglishKeyAction::cancel; break;
    case EnglishKeyKind::backspace: action = EnglishKeyAction::backspace; break;
    case EnglishKeyKind::delete_forward:
        action = EnglishKeyAction::delete_forward;
        break;
    case EnglishKeyKind::move_left: action = EnglishKeyAction::move_left; break;
    case EnglishKeyKind::move_right: action = EnglishKeyAction::move_right; break;
    case EnglishKeyKind::move_home: action = EnglishKeyAction::move_home; break;
    case EnglishKeyKind::move_end: action = EnglishKeyAction::move_end; break;
    case EnglishKeyKind::previous_row:
        action = EnglishKeyAction::previous_row;
        break;
    case EnglishKeyKind::next_row: action = EnglishKeyAction::next_row; break;
    case EnglishKeyKind::previous_page:
        action = EnglishKeyAction::previous_page;
        break;
    case EnglishKeyKind::next_page: action = EnglishKeyAction::next_page; break;
    case EnglishKeyKind::other:
        break;
    }
    return {
        action,
        action != EnglishKeyAction::pass_through,
        false,
    };
}

}  // namespace piinput
