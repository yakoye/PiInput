#include "piinput/input_mode.h"

namespace piinput {

void ShiftToggleState::on_shift_down(const bool modifier_already_down) noexcept {
    if (!pressed_) {
        pressed_ = true;
        used_as_modifier_ = modifier_already_down;
    }
}

void ShiftToggleState::on_other_key_down() noexcept {
    if (pressed_) {
        used_as_modifier_ = true;
    }
}

bool ShiftToggleState::on_shift_up() noexcept {
    if (!pressed_) {
        return false;
    }
    const bool toggle = !used_as_modifier_;
    reset();
    return toggle;
}

void ShiftToggleState::reset() noexcept {
    pressed_ = false;
    used_as_modifier_ = false;
}

}  // namespace piinput
