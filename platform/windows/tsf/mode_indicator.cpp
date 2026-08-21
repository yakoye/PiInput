#include "mode_indicator.h"

#include "lang_bar_item.h"

#include <algorithm>

namespace piinput::windows {
namespace {

constexpr wchar_t kIndicatorClass[] = L"PiInputModeIndicator";
constexpr UINT kDismissTimer = 1U;

// A square barely larger than the glyph. Bigger reads as a dialog and pulls the
// eye away from the text, which is the opposite of what a passing hint should do.
constexpr int kBoxDip = 46;
constexpr int kFontDip = 24;
constexpr int kRadiusDip = 10;
constexpr int kCaretGapDip = 6;

// Nearly opaque. At 236 the text underneath showed through the panel and made
// the mark hard to read against a busy line, which defeats the point of a hint
// meant to be taken in at a glance.
constexpr BYTE kOpacity = 250U;

[[nodiscard]] int scaled(const int value, const UINT dpi) noexcept {
    return MulDiv(value, static_cast<int>((std::max)(dpi, 96U)), 96);
}

[[nodiscard]] const wchar_t* mark_text(const InputModeMark mark) noexcept {
    switch (mark) {
        case InputModeMark::chinese:
            return L"中";
        case InputModeMark::english_caps:
            return L"A";
        case InputModeMark::english:
            break;
    }
    return L"a";
}

// The module this code lives in, so the window class belongs to the shim rather
// than to whichever application happens to have loaded it.
//
// Pinned, not merely referenced. Windows does not unregister a window class
// when the module that registered it is unloaded, and this module can be:
// DllCanUnloadNow reports S_OK once the last text service is gone. The class
// would outlive the code, leaving its window procedure pointing into freed
// memory, and the next load would find the class already registered and reuse
// that dangling pointer. Pinning costs one module that was staying resident
// anyway -- the Shim is loaded into every application that takes input.
[[nodiscard]] HINSTANCE owning_module() noexcept {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&kIndicatorClass), &module) == FALSE) {
        return nullptr;
    }
    return reinterpret_cast<HINSTANCE>(module);
}

}  // namespace

InputModeMark mode_mark_for(const bool english_mode, const bool caps_lock) noexcept {
    if (!english_mode) return InputModeMark::chinese;
    return caps_lock ? InputModeMark::english_caps : InputModeMark::english;
}

ModeIndicator::~ModeIndicator() { destroy(); }

LRESULT CALLBACK ModeIndicator::window_proc(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wparam, lparam);
    }
    auto* const self =
        reinterpret_cast<ModeIndicator*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_PAINT) {
        self->paint();
        return 0;
    }
    if (message == WM_TIMER && wparam == kDismissTimer) {
        self->hide();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ModeIndicator::ensure_window() noexcept {
    if (window_ != nullptr) return true;
    const HINSTANCE instance = owning_module();
    if (instance == nullptr) return false;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kIndicatorClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&window_class);

    // WS_EX_TRANSPARENT so a click aimed at the text underneath still lands
    // there. WS_EX_NOACTIVATE so showing it never takes the caret away from the
    // application in the middle of a word.
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED |
            WS_EX_TRANSPARENT,
        kIndicatorClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (window_ == nullptr) return false;
    SetLayeredWindowAttributes(window_, 0U, kOpacity, LWA_ALPHA);
    return true;
}

void ModeIndicator::show(
    const InputModeMark mark, const std::optional<RECT>& caret) noexcept {
    if (!ensure_window()) return;
    mark_ = mark;
    place(caret);
    InvalidateRect(window_, nullptr, TRUE);
    // Restarts the countdown, so a burst of switches leaves the last mark up for
    // the full time instead of expiring on a schedule set by the first one.
    SetTimer(window_, kDismissTimer, mode_indicator_visible_ms, nullptr);
}

void ModeIndicator::place(const std::optional<RECT>& caret) noexcept {
    if (caret.has_value()) {
        last_caret_ = *caret;
        has_last_caret_ = true;
    }

    RECT anchor = last_caret_;
    if (!has_last_caret_) {
        // Clicking the taskbar indicator leaves no caret anywhere in reach, and
        // the very first switch may come before any text was typed. Centre on
        // the window the user is looking at instead of guessing a corner.
        const HWND foreground = GetForegroundWindow();
        RECT bounds{};
        if (foreground == nullptr || GetWindowRect(foreground, &bounds) == FALSE) {
            bounds = RECT{
                0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        const LONG x = (bounds.left + bounds.right) / 2;
        const LONG y = (bounds.top + bounds.bottom) / 2;
        anchor = RECT{x, y, x + 1, y + 1};
    }

    const UINT dpi = GetDpiForWindow(window_);
    const int box = scaled(kBoxDip, dpi);
    const int gap = scaled(kCaretGapDip, dpi);

    // Above the caret on purpose: the candidate window sits below it, and the
    // two would otherwise cover each other when CapsLock is pressed mid-word.
    int x = static_cast<int>(anchor.left);
    int y = static_cast<int>(anchor.top) - box - gap;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const POINT point{anchor.left, anchor.top};
    const HMONITOR screen = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (screen != nullptr && GetMonitorInfoW(screen, &monitor) != FALSE) {
        const RECT& work = monitor.rcWork;
        if (y < work.top) y = static_cast<int>(anchor.bottom) + gap;
        y = (std::clamp)(
            y,
            static_cast<int>(work.top),
            (std::max)(static_cast<int>(work.bottom) - box, static_cast<int>(work.top)));
        x = (std::clamp)(
            x,
            static_cast<int>(work.left),
            (std::max)(static_cast<int>(work.right) - box, static_cast<int>(work.left)));
    }

    HRGN region = CreateRoundRectRgn(
        0, 0, box + 1, box + 1, scaled(kRadiusDip, dpi), scaled(kRadiusDip, dpi));
    if (region != nullptr && SetWindowRgn(window_, region, FALSE) == 0) {
        DeleteObject(region);
    }
    SetWindowPos(window_, HWND_TOPMOST, x, y, box, box, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (font_ == nullptr || font_dpi_ != dpi) {
        if (font_ != nullptr && !stock_font_) DeleteObject(font_);
        font_ = CreateFontW(
            -scaled(kFontDip, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            // Greyscale, not ClearType: subpixel rendering on a layered window
            // fringes the glyph orange and blue, because the filter assumes an
            // opaque background it does not have here.
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        // A null font would leave the device context on whatever it had, which
        // for a fresh popup is the system default at its own size -- the mark
        // would come out too small to read. A stock font is at least a font.
        if (font_ == nullptr) {
            font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            stock_font_ = true;
        } else {
            stock_font_ = false;
        }
        font_dpi_ = dpi;
    }
}

void ModeIndicator::paint() noexcept {
    PAINTSTRUCT paint_state{};
    const HDC dc = BeginPaint(window_, &paint_state);
    if (dc == nullptr) return;

    RECT client{};
    GetClientRect(window_, &client);
    const bool dark = system_uses_dark_theme();
    const COLORREF background = dark ? RGB(43, 43, 43) : RGB(252, 252, 252);
    const COLORREF edge = dark ? RGB(86, 86, 86) : RGB(202, 202, 202);
    const COLORREF ink = dark ? RGB(255, 255, 255) : RGB(28, 28, 28);

    const HBRUSH fill = CreateSolidBrush(background);
    const HPEN pen = CreatePen(PS_SOLID, 1, edge);
    const auto old_brush = static_cast<HBRUSH>(SelectObject(dc, fill));
    const auto old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    const UINT dpi = GetDpiForWindow(window_);
    const int radius = scaled(kRadiusDip, dpi);
    RoundRect(dc, 0, 0, client.right, client.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(fill);

    const auto old_font = static_cast<HFONT>(SelectObject(dc, font_));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    DrawTextW(
        dc, mark_text(mark_), -1, &client,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);

    EndPaint(window_, &paint_state);
}

void ModeIndicator::hide() noexcept {
    if (window_ == nullptr) return;
    KillTimer(window_, kDismissTimer);
    ShowWindow(window_, SW_HIDE);
}

void ModeIndicator::destroy() noexcept {
    if (window_ != nullptr) {
        KillTimer(window_, kDismissTimer);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (font_ != nullptr) {
        if (!stock_font_) DeleteObject(font_);
        font_ = nullptr;
        stock_font_ = false;
    }
    font_dpi_ = 0U;
}

}  // namespace piinput::windows
