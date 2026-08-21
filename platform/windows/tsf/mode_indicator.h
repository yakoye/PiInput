#pragma once

#include "piinput/windows_compat.h"

#include <cstdint>
#include <optional>

namespace piinput::windows {

// What the popup says. English splits in two because CapsLock is the one piece
// of state the user cannot see from the letters they have not typed yet.
enum class InputModeMark : std::uint8_t {
    chinese,       // 中
    english,       // a
    english_caps,  // A
};

// A small popup beside the caret naming the mode just switched into, the way
// macOS and 小狼毫 do it.
//
// It exists because the taskbar indicator is nowhere near where the user is
// looking. A Shift press changed the mode and nothing next to the text said so,
// which is only discovered after typing a word in the wrong language.
//
// Only sticky state gets a popup. Holding Shift for a single capital does not
// qualify: the finger already knows it is held, and typing "Hello" would flash
// the popup once per capital letter.
class ModeIndicator final {
public:
    ModeIndicator() = default;
    ~ModeIndicator();
    ModeIndicator(const ModeIndicator&) = delete;
    ModeIndicator& operator=(const ModeIndicator&) = delete;

    // Shows the mark and starts the dismissal timer. Creates the window on the
    // first call; a failure to create is silently a no-op, since a missing
    // popup must never stop a key from being typed.
    //
    // `caret` is where the insertion point is on screen. Applications that keep
    // no system caret, and clicks on the taskbar indicator where the caret
    // belongs to another process, pass nothing and get the last known position.
    void show(InputModeMark mark, const std::optional<RECT>& caret) noexcept;
    void hide() noexcept;
    void destroy() noexcept;

    [[nodiscard]] HWND window() const noexcept { return window_; }

private:
    static LRESULT CALLBACK window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool ensure_window() noexcept;
    void paint() noexcept;
    void place(const std::optional<RECT>& caret) noexcept;

    HWND window_{nullptr};
    HFONT font_{nullptr};
    // Set when font_ is a stock object, which the system owns and which must
    // never be passed to DeleteObject.
    bool stock_font_{false};
    UINT font_dpi_{0U};
    InputModeMark mark_{InputModeMark::chinese};
    RECT last_caret_{};
    bool has_last_caret_{false};
};

// How long the popup stays up. Long enough to read after a glance drifts back
// from the keyboard, short enough not to sit over the next word being typed.
inline constexpr UINT mode_indicator_visible_ms = 2000U;

// The mark for a given state. Chinese wins over CapsLock because CapsLock does
// not produce capitals while Chinese is on -- the popup reports what will
// actually be typed, not what the key is called.
[[nodiscard]] InputModeMark mode_mark_for(bool english_mode, bool caps_lock) noexcept;

}  // namespace piinput::windows
