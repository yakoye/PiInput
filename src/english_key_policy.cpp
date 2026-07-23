#include "piinput/english_key_policy.h"

namespace piinput {

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
