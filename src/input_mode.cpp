#include "piinput/input_mode.h"

namespace piinput {

void ShiftToggleState::on_shift_down(const bool modifier_already_down) noexcept {
    if (!pressed_) {
        pressed_ = true;
        used_as_modifier_ = modifier_already_down;
        suppress_unmatched_release_ = false;
    }
}

bool ShiftToggleState::on_other_key_down(const bool shift_still_down) noexcept {
    if (!pressed_) {
        if (shift_still_down) suppress_unmatched_release_ = true;
        return false;
    }
    if (shift_still_down) {
        used_as_modifier_ = true;
        return false;
    }
    const bool toggle = !used_as_modifier_;
    finish_press();
    return toggle;
}

void ShiftToggleState::note_chord_key() noexcept {
    // Deliberately the shift_still_down branch of on_other_key_down, with the
    // return value removed so no caller can turn an observation into a switch.
    if (!pressed_) {
        suppress_unmatched_release_ = true;
        return;
    }
    used_as_modifier_ = true;
}

bool ShiftToggleState::on_shift_up(const bool modifier_down) noexcept {
    // A chord, not a tap. This covers both the ordinary Ctrl+Shift press and the
    // case where the input-method switch happened mid-chord and this method was
    // activated in time to see only the release.
    if (modifier_down) {
        if (pressed_) finish_press();
        else suppress_unmatched_release_ = true;
        return false;
    }
    if (!pressed_) {
        if (suppress_unmatched_release_) {
            suppress_unmatched_release_ = false;
            return false;
        }
        suppress_unmatched_release_ = true;
        return true;
    }
    const bool toggle = !used_as_modifier_;
    finish_press();
    return toggle;
}

void ShiftToggleState::reset() noexcept {
    pressed_ = false;
    used_as_modifier_ = false;
    // Reset runs when this input method is activated or rebound, which is
    // exactly when a Shift release from the chord that switched to it can still
    // be in flight. Recovering an omitted KeyDown is worth doing, but not for
    // the very first release after a switch, where there is nothing to recover
    // from and everything to get wrong.
    suppress_unmatched_release_ = true;
}

void ShiftToggleState::finish_press() noexcept {
    pressed_ = false;
    used_as_modifier_ = false;
    suppress_unmatched_release_ = true;
}

}  // namespace piinput
