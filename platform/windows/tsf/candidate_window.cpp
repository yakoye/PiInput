#include "candidate_window.h"

#include <algorithm>

namespace liteime::windows {
namespace {

constexpr wchar_t kCandidateClass[] = L"LiteIMETsfCandidateWindow";
constexpr int kPadding = 8;
constexpr int kHeaderHeight = 30;
constexpr int kRowHeight = 30;
constexpr std::size_t kPageSize = 5U;

}  // namespace

CandidateWindow::~CandidateWindow() {
    destroy();
}

bool CandidateWindow::create(const HINSTANCE instance) {
    if (window_ != nullptr) {
        return true;
    }
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kCandidateClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&window_class);

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kCandidateClass,
        L"",
        WS_POPUP | WS_BORDER,
        0,
        0,
        360,
        kHeaderHeight + static_cast<int>(kPageSize) * kRowHeight + 2 * kPadding,
        nullptr,
        nullptr,
        instance,
        this);
    if (window_ == nullptr) {
        return false;
    }
    font_ = CreateFontW(
        -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return true;
}

void CandidateWindow::destroy() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void CandidateWindow::update(
    const std::wstring& schema_name,
    const std::wstring& composition,
    const std::vector<std::wstring>& candidates,
    const std::size_t selected,
    const std::size_t page_start) {
    schema_name_ = schema_name;
    composition_ = composition;
    candidates_ = candidates;
    selected_ = selected;
    page_start_ = page_start;
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, TRUE);
    }
}

void CandidateWindow::show_near_caret() {
    if (window_ == nullptr) {
        return;
    }
    GUITHREADINFO info{sizeof(info)};
    POINT point{20, 40};
    if (GetGUIThreadInfo(0U, &info) != FALSE && info.hwndCaret != nullptr) {
        point.x = info.rcCaret.left;
        point.y = info.rcCaret.bottom + 4;
        ClientToScreen(info.hwndCaret, &point);
    } else {
        GetCursorPos(&point);
        point.y += 20;
    }

    const int visible_count = static_cast<int>((std::min)(kPageSize,
        candidates_.size() > page_start_ ? candidates_.size() - page_start_ : 0U));
    const int height = kHeaderHeight + (std::max)(1, visible_count) * kRowHeight + 2 * kPadding;
    SetWindowPos(window_, HWND_TOPMOST, point.x, point.y, 400, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CandidateWindow::hide() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

void CandidateWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    if (dc == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);

    RECT header{kPadding, kPadding, client.right - kPadding, kPadding + kHeaderHeight};
    SetTextColor(dc, RGB(45, 45, 45));
    const std::wstring header_text = composition_ + L"    [" + schema_name_ + L"]";
    DrawTextW(dc, header_text.c_str(), -1, &header, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    const std::size_t end = (std::min)(candidates_.size(), page_start_ + kPageSize);
    for (std::size_t index = page_start_; index < end; ++index) {
        const int row = static_cast<int>(index - page_start_);
        RECT item{kPadding, kPadding + kHeaderHeight + row * kRowHeight,
                  client.right - kPadding, kPadding + kHeaderHeight + (row + 1) * kRowHeight};
        if (index == selected_) {
            HBRUSH selected_brush = CreateSolidBrush(RGB(225, 239, 255));
            FillRect(dc, &item, selected_brush);
            DeleteObject(selected_brush);
        }
        SetTextColor(dc, RGB(25, 25, 25));
        const std::wstring number = std::to_wstring(row + 1U) + L". ";
        const std::wstring line = number + candidates_[index];
        item.left += 4;
        DrawTextW(dc, line.c_str(), -1, &item, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    EndPaint(window_, &paint);
}

LRESULT CALLBACK CandidateWindow::window_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<CandidateWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_PAINT && self != nullptr) {
        self->paint();
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace liteime::windows
