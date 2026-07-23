#pragma once

#include "piinput/windows_compat.h"

#include <string>
#include <vector>

namespace piinput::windows {

class CandidateWindow final {
public:
    CandidateWindow() = default;
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;
    ~CandidateWindow();

    bool create(HINSTANCE instance);
    void destroy();
    void update(
        const std::wstring& schema_name,
        const std::wstring& composition,
        const std::vector<std::wstring>& candidates,
        std::size_t selected,
        std::size_t active_row,
        std::size_t first_visible_row,
        std::size_t items_per_row,
        std::size_t visible_rows);
    void show_near_caret();
    void hide();

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    [[nodiscard]] int desired_width() const;
    [[nodiscard]] std::size_t actual_visible_rows() const noexcept;
    [[nodiscard]] std::vector<int> item_widths(HDC dc) const;

    HWND window_{};
    HFONT font_{};
    std::wstring schema_name_;
    std::wstring composition_;
    std::vector<std::wstring> candidates_;
    std::size_t selected_{};
    std::size_t active_row_{};
    std::size_t first_visible_row_{};
    std::size_t items_per_row_{6U};
    std::size_t visible_rows_{3U};
};

}  // namespace piinput::windows
