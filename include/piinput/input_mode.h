#pragma once

namespace piinput {

class ShiftToggleState final {
public:
    void on_shift_down(bool modifier_already_down = false) noexcept;
    [[nodiscard]] bool on_other_key_down(bool shift_still_down = true) noexcept;
    // `modifier_down` is whether Ctrl, Alt or Win is held at the moment of the
    // release. Ctrl+Shift is how Windows switches input methods, and the switch
    // lands between the Shift press and its release: the newly activated method
    // sees only a lone release. Without this it read that as a bare Shift tap and
    // flipped to English every time the user switched into it.
    [[nodiscard]] bool on_shift_up(bool modifier_down = false) noexcept;
    void reset() noexcept;

private:
    void finish_press() noexcept;

    bool pressed_{};
    bool used_as_modifier_{};
    bool suppress_unmatched_release_{};
};

}  // namespace piinput
