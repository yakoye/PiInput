#pragma once

#include "liteime/windows_compat.h"

#include <string>
#include <vector>

namespace liteime::windows {

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
        std::size_t page_start,
        std::size_t page_size);
    void show_near_caret();
    void hide();

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void paint();
    [[nodiscard]] int desired_width() const;
    [[nodiscard]] std::vector<int> item_widths(HDC dc) const;

    HWND window_{};
    HFONT font_{};
    std::wstring schema_name_;
    std::wstring composition_;
    std::vector<std::wstring> candidates_;
    std::size_t selected_{};
    std::size_t page_start_{};
    std::size_t page_size_{6U};
};

}  // namespace liteime::windows
