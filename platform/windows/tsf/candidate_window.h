#pragma once

#include "piinput/windows_compat.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace piinput::windows {

[[nodiscard]] RECT place_candidate_window(
    const RECT& caret,
    SIZE desired,
    const RECT& work_area,
    UINT dpi,
    int anchor_gap = 4) noexcept;

[[nodiscard]] RECT place_candidate_window_at_text_caret(
    const RECT& caret,
    SIZE desired,
    const RECT& work_area,
    UINT dpi,
    int anchor_gap = 4) noexcept;

[[nodiscard]] RECT clamp_candidate_window_rect(
    const RECT& candidate,
    const RECT& work_area,
    UINT dpi) noexcept;

[[nodiscard]] int limit_candidate_window_width(int desired_width, UINT dpi) noexcept;

[[nodiscard]] int candidate_window_height(
    std::size_t visible_rows,
    UINT dpi,
    std::uint32_t single_row_height = 40U) noexcept;

[[nodiscard]] RECT candidate_row_rect(
    std::size_t row,
    UINT dpi,
    std::uint32_t single_row_height) noexcept;

[[nodiscard]] int candidate_text_top(const RECT& row, int text_height) noexcept;

struct CandidateVisualSettings final {
    std::uint32_t font_size{16U};
    std::uint32_t window_height{40U};

    bool operator==(const CandidateVisualSettings&) const = default;
};

enum class CandidateToolbarAction : std::uint8_t {
    none,
    expand_candidates,
    open_menu,
    symbols,
    settings,
};

enum class CandidateContextAction : std::uint8_t {
    pin_first,
    delete_candidate,
    unpin,
    dismiss,
};

[[nodiscard]] std::optional<CandidateContextAction>
candidate_context_action_from_command(UINT command) noexcept;

[[nodiscard]] CandidateToolbarAction candidate_toolbar_hit_test(
    POINT point,
    const RECT& client,
    bool menu_open,
    UINT dpi,
    std::uint32_t single_row_height) noexcept;

[[nodiscard]] RECT stabilize_candidate_window_rect(
    const RECT* locked,
    const RECT& proposed,
    bool visible_rows_changed) noexcept;

[[nodiscard]] bool should_reanchor_candidate_window(
    bool geometry_locked,
    bool locked_to_text_caret,
    bool incoming_text_caret) noexcept;

class CandidateWindow final {
public:
    CandidateWindow() = default;
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;
    ~CandidateWindow();

    bool create(HINSTANCE instance);
    void set_toolbar_handler(std::function<void(CandidateToolbarAction)> handler);
    // Left click on a candidate. Reported by visible position; the presenter
    // knows which candidate that is.
    void set_candidate_select_handler(std::function<void(std::size_t)> handler);
    void set_candidate_context_handler(
        std::function<void(std::size_t, CandidateContextAction)> handler);
    void destroy();
    void update(
        const std::wstring& composition,
        const std::vector<std::wstring>& candidates,
        std::size_t selected,
        std::size_t active_row,
        std::size_t first_visible_row,
        std::size_t items_per_row,
        std::size_t visible_rows,
        CandidateVisualSettings visual = {});
    // Frees the geometry lock without hiding the window. The lock exists so the
    // bar does not slide right as letters are added to a word; it must be given
    // up the moment a new word opens, and relying on hide() having happened is
    // what let the bar stay at the previous word's position.
    void release_anchor() noexcept;
    void show_at_text_caret(const RECT& caret);
    // Same placement, but the lock is not marked as coming from a real text
    // caret, so the first true caret of this composition may correct it once.
    void show_at_provisional_caret(const RECT& caret);
    void show_near_caret();
    void hide();

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    [[nodiscard]] int desired_width() const;
    [[nodiscard]] int desired_height() const noexcept;
    [[nodiscard]] std::size_t actual_visible_rows() const noexcept;
    [[nodiscard]] std::vector<int> item_widths(HDC dc) const;
    void resize_for_toolbar_menu();
    void update_window_region(int width, int height);
    void show_at_anchor(const RECT& anchor, int anchor_gap, bool text_caret);
    void move_to_target_monitor(POINT point);
    void update_dpi(UINT dpi);
    [[nodiscard]] int scaled(int value) const noexcept;

    HWND window_{};
    HFONT font_{};
    UINT dpi_{96U};
    std::wstring composition_;
    std::vector<std::wstring> candidates_;
    std::size_t selected_{};
    std::size_t active_row_{};
    std::size_t first_visible_row_{};
    std::size_t items_per_row_{6U};
    std::size_t visible_rows_{3U};
    CandidateVisualSettings visual_{};
    std::function<void(CandidateToolbarAction)> toolbar_handler_;
    std::function<void(std::size_t)> candidate_select_handler_;
    std::function<void(std::size_t, CandidateContextAction)> candidate_context_handler_;
    // The candidate window is WS_EX_NOACTIVATE, so it can never become the
    // foreground window -- and a popup menu owned by a window that is not in
    // the foreground does not close when the user clicks elsewhere. This
    // hidden, ordinary window owns the menus instead.
    HWND menu_owner_{nullptr};
    std::vector<RECT> visible_item_rects_;
    std::vector<std::size_t> visible_item_indexes_;
    bool toolbar_menu_open_{};
    RECT locked_rect_{};
    bool geometry_locked_{};
    bool locked_to_text_caret_{};
    bool visible_rows_changed_{};
    // Each key anchors the window twice: once when the snapshot is staged and
    // once when the real text caret arrives. When the second pass resolves to
    // the same rectangle there is nothing to move, and repeating the region and
    // SetWindowPos work only costs time and redraws.
    bool shown_{};
    // Text measurement is the expensive part of anchoring: a DC plus one
    // GetTextExtentPoint32W per visible item. It only changes the result when
    // the candidate content changed, so a locked window that is merely being
    // re-anchored reuses its rectangle instead of measuring again.
    bool layout_dirty_{true};
};

}  // namespace piinput::windows
