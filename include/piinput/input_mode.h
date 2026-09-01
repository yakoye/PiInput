#pragma once

namespace piinput {

class ShiftToggleState final {
public:
    void on_shift_down(bool modifier_already_down = false) noexcept;
    [[nodiscard]] bool on_other_key_down(bool shift_still_down = true) noexcept;
    // Records that some other key went down while Shift was held, without
    // deciding anything. Returns nothing because there is nothing to decide:
    // Shift is still down, so this can only ever mean "a chord, not a tap".
    //
    // It exists so the observation can be made from OnTestKeyDown, which is the
    // only callback some applications make for a key they will not be given.
    // MobaXterm asks about every key and delivers only the ones PiInput claims,
    // so Shift+Insert, Shift+Delete and Shift+arrow were invisible to the
    // toggle and read as bare Shift taps. Being idempotent is what makes it
    // safe there: a probe may be repeated or never followed by a real event.
    void note_chord_key() noexcept;
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
