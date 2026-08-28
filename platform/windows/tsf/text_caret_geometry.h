#pragma once

#include "piinput/windows_compat.h"

#include <optional>

namespace piinput::windows {

[[nodiscard]] inline bool usable_text_caret_rect(const RECT& rect) noexcept {
    if (rect.right < rect.left || rect.bottom < rect.top) return false;
    // An insertion point stands as tall as its line. A rectangle with no height
    // is not a position the caret could be at, whatever else it looks like.
    //
    // MobaXterm answers GetTextExt with one fixed point of zero height, the
    // same value on every keystroke, sitting just outside its own window at the
    // bottom right. Taken as a caret it pinned the candidate bar to the corner
    // of the screen for the whole session. Rejected, the bar falls through to
    // the bottom-left of the focused window, which for a terminal is beside the
    // prompt. Width is not tested here -- a caret drawn as a bare line
    // legitimately reports zero width, and an over-wide extent is what
    // caret_rect_is_plausible is for.
    if (rect.bottom == rect.top) return false;
    return rect.left != 0 || rect.top != 0 || rect.right != 0 || rect.bottom != 0;
}

// An insertion point is a thin bar, or at most one character wide when the
// application draws a block caret. Scintilla answers GetTextExt on a collapsed
// range with the extent of the whole composition instead, and that rectangle
// reports the composition's line -- measured against the true caret it was off
// by whole line heights in 30 of 40 samples. Such a rectangle is a text extent,
// not a caret, and must not be used as one while a real caret is available.
inline constexpr LONG text_caret_max_width = 32L;

[[nodiscard]] inline bool caret_rect_is_plausible(const RECT& rect) noexcept {
    return usable_text_caret_rect(rect) && (rect.right - rect.left) <= text_caret_max_width;
}

// The selection is the application's actual insertion point. Some text stores
// return a displaced or whole-composition extent after an ITfRange is collapsed,
// so the Composition endpoint is only a compatibility fallback. A rectangle that
// is too wide to be a caret loses to one that is not, whichever it came from.
[[nodiscard]] inline std::optional<RECT> choose_text_caret_geometry(
    const RECT* const selection,
    const RECT* const composition) noexcept {
    if (selection != nullptr && caret_rect_is_plausible(*selection)) return *selection;
    if (composition != nullptr && caret_rect_is_plausible(*composition)) return *composition;
    if (selection != nullptr && usable_text_caret_rect(*selection)) return *selection;
    if (composition != nullptr && usable_text_caret_rect(*composition)) return *composition;
    return std::nullopt;
}

// ITfContextView::GetTextExt follows the DPI coordinate space of the client
// process that hosts the TSF shim. Per-monitor-aware clients already return
// physical screen coordinates; applying LogicalToPhysicalPoint again moves
// the popup away from the insertion caret and can push it onto another
// monitor. Legacy/system-aware clients still require the conversion before
// coordinates are sent to the per-monitor-aware Host process.
[[nodiscard]] inline RECT normalized_text_caret_geometry(
    const RECT& reported,
    const RECT& converted,
    const bool per_monitor_aware) noexcept {
    return per_monitor_aware ? reported : converted;
}

}  // namespace piinput::windows
