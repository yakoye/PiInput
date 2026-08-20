// PiInput caret placement demo.
//
// The candidate bar has been a step behind the insertion point for several
// releases, and each attempt to fix it from inside the input method changed two
// things at once: where the caret came from, and when the bar was allowed to
// move. This tool separates them. It drives the real production CandidateWindow
// with a caret read straight from the focused application, so what is on screen
// is exactly what the placement code does with a known-good rectangle.
//
// Type in any application. Every time the insertion point moves, the bar is
// placed at it and a thin outline is drawn around the caret rectangle the tool
// used. If the bar sits under that outline, the placement code is correct and
// the remaining error is in the caret the input method supplies, or in when it
// supplies it. If the bar does not, the placement code itself is wrong and can
// be fixed here without touching the IME at all.
//
// No keyboard hook and no key logging: the caret is polled, not the keys.

#define _CRT_SECURE_NO_WARNINGS

#include "candidate_window.h"

#include "piinput/windows_compat.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr UINT poll_timer_id = 1U;
constexpr UINT poll_interval_ms = 16U;

struct CaretSample final {
    RECT rect{};
    bool valid{};
    HWND owner{};
};

// The insertion point of whatever the user is typing in, in screen pixels.
[[nodiscard]] CaretSample read_focused_caret() noexcept {
    CaretSample sample{};
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return sample;
    const DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    if (thread == 0U) return sample;

    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(thread, &info) == FALSE) return sample;
    if (info.hwndCaret == nullptr) return sample;
    if (info.rcCaret.right <= info.rcCaret.left &&
        info.rcCaret.bottom <= info.rcCaret.top) {
        return sample;
    }

    POINT top_left{info.rcCaret.left, info.rcCaret.top};
    POINT bottom_right{info.rcCaret.right, info.rcCaret.bottom};
    if (ClientToScreen(info.hwndCaret, &top_left) == FALSE ||
        ClientToScreen(info.hwndCaret, &bottom_right) == FALSE) {
        return sample;
    }
    // A zero-width caret still needs a rectangle the placement code can use.
    if (bottom_right.x <= top_left.x) bottom_right.x = top_left.x + 1;
    sample.rect = RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    sample.valid = true;
    sample.owner = info.hwndCaret;
    return sample;
}

// Outlines the rectangle that was handed to the placement code, so the two can
// be compared on screen instead of by eye against a moving text cursor.
class CaretMarker final {
public:
    bool create(const HINSTANCE instance) {
        WNDCLASSEXW description{};
        description.cbSize = sizeof(description);
        description.lpfnWndProc = DefWindowProcW;
        description.hInstance = instance;
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        description.lpszClassName = L"PiInputCaretMarker";
        if (RegisterClassExW(&description) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                WS_EX_TRANSPARENT,
            description.lpszClassName, L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        if (window_ == nullptr) return false;
        (void)SetLayeredWindowAttributes(window_, 0U, 140U, LWA_ALPHA);
        return true;
    }

    void show(const RECT& caret) {
        if (window_ == nullptr) return;
        const int width = (std::max)(caret.right - caret.left, 2L) + 2;
        const int height = (std::max)(caret.bottom - caret.top, 2L) + 2;
        (void)SetWindowPos(window_, HWND_TOPMOST,
            caret.left - 1, caret.top - 1, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        const HDC device = GetDC(window_);
        if (device == nullptr) return;
        RECT area{0, 0, width, height};
        const HBRUSH brush = CreateSolidBrush(RGB(220, 40, 40));
        FrameRect(device, &area, brush);
        DeleteObject(brush);
        (void)ReleaseDC(window_, device);
    }

    void hide() noexcept {
        if (window_ != nullptr) (void)ShowWindow(window_, SW_HIDE);
    }

private:
    HWND window_{};
};

// The candidate window is created by production code and keeps its handle
// private, so it is found the same way the Host finds it: by class name, within
// this process.
[[nodiscard]] HWND find_candidate_window() noexcept {
    HWND found = nullptr;
    DWORD owner = 0U;
    while ((found = FindWindowExW(nullptr, found,
                L"PiInputTsfCandidateWindow", nullptr)) != nullptr) {
        (void)GetWindowThreadProcessId(found, &owner);
        if (owner == GetCurrentProcessId()) return found;
    }
    return nullptr;
}

struct DemoState final {
    piinput::windows::CandidateWindow bar;
    CaretMarker marker;
    RECT last_caret{};
    bool placed{};
    unsigned long long placements{};
    const wchar_t* last_action{L"reanchor"};
    std::FILE* log{};
    std::wstring log_path;
};

DemoState* g_state = nullptr;

// The candidate window locks its geometry on the first placement and refuses
// every later caret until it is hidden. That lock is only ever wanted while a
// word is being typed: the insertion point walks right with each letter and the
// bar must not slide along with it. At every other moment the bar should sit at
// the insertion point, wherever it is -- including moves within the same line.
//
// This tool has no composition to observe, so it models the "not typing" half:
// the anchor is released on every caret move and the bar is re-placed. Whether
// that is always pixel-accurate is exactly what the log answers. The freeze
// belongs in the input method, where the composition state is known.
//
// hide() is the only call that clears the lock, so it is what releases it here.

void place_bar(DemoState& state, const RECT& caret) {
    const std::vector<std::wstring> candidates{
        L"位置", L"未知", L"味精", L"卫京", L"位", L"未",
    };
    state.bar.update(
        L"wz", candidates, 0U, 0U, 0U, 6U, 1U,
        piinput::windows::CandidateVisualSettings{});
    state.bar.hide();
    state.bar.show_at_text_caret(caret);
    state.marker.show(caret);
    state.last_action = L"reanchor";
    state.last_caret = caret;
    state.placed = true;
    ++state.placements;

    // Record what was asked for and what actually landed. dx/dy are the numbers
    // that matter: the bar is meant to sit flush under the caret, so dx is the
    // horizontal error and dy the gap below the caret's bottom edge.
    if (state.log == nullptr) return;
    RECT placed{};
    const HWND bar_window = find_candidate_window();
    if (bar_window == nullptr || GetWindowRect(bar_window, &placed) == FALSE) return;
    const HMONITOR monitor = MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST);
    const UINT dpi = GetDpiForWindow(bar_window);
    (void)std::fwprintf(state.log,
        L"%llu,%lu,%s,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%u,%p\n",
        state.placements, GetTickCount(), state.last_action,
        caret.left, caret.top, caret.right, caret.bottom,
        placed.left, placed.top, placed.right, placed.bottom,
        placed.left - caret.left, placed.top - caret.bottom,
        dpi, static_cast<void*>(monitor));
    (void)std::fflush(state.log);
}

LRESULT CALLBACK demo_window_proc(
    const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    if (message == WM_TIMER && wparam == poll_timer_id && g_state != nullptr) {
        const CaretSample sample = read_focused_caret();
        if (!sample.valid) {
            if (g_state->placed) {
                g_state->bar.hide();
                g_state->marker.hide();
                g_state->placed = false;
            }
            return 0;
        }
        const bool moved = !g_state->placed ||
            sample.rect.left != g_state->last_caret.left ||
            sample.rect.top != g_state->last_caret.top ||
            sample.rect.bottom != g_state->last_caret.bottom;
        if (moved) place_bar(*g_state, sample.rect);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    DemoState state;
    g_state = &state;
    if (!state.bar.create(instance) || !state.marker.create(instance)) {
        MessageBoxW(nullptr, L"无法创建候选窗口。", L"PiInput 光标定位演示", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = demo_window_proc;
    description.hInstance = instance;
    description.lpszClassName = L"PiInputCaretDemo";
    if (RegisterClassExW(&description) == 0) return 1;
    const HWND host = CreateWindowExW(
        0, description.lpszClassName, L"PiInput 光标定位演示",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        HWND_MESSAGE, nullptr, instance, nullptr);
    if (host == nullptr) return 1;

    MessageBoxW(nullptr,
        L"这个工具用真实的候选窗口代码，跟随任意程序的插入点。\r\n\r\n"
        L"到记事本、Notepad++ 或任何输入框里打字：\r\n"
        L"  · 候选栏应当始终贴在插入点正下方；\r\n"
        L"  · 红框标出的是本工具交给定位代码的光标矩形。\r\n\r\n"
        L"候选栏若与红框对齐，说明定位代码本身没有问题，\r\n"
        L"偏差来自输入法送来的光标值或送达的时机。\r\n\r\n"
        L"坐标会自动记录到日志文件，退出时告知路径：\r\n"
        L"  caret_* 是插入点矩形，bar_* 是候选栏实际落点，\r\n"
        L"  dx 是水平误差，dy_below_caret 是候选栏顶边到插入点底边的距离。\r\n\r\n"
        L"关闭这个对话框后开始，按 Ctrl+Alt+Q 退出。",
        L"PiInput 光标定位演示", MB_ICONINFORMATION);

    // Next to the executable when that is writable, so the log is easy to find;
    // the temp directory otherwise.
    wchar_t module_path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) != 0U) {
        std::wstring directory(module_path);
        const auto separator = directory.find_last_of(L'\\');
        if (separator != std::wstring::npos) directory.resize(separator + 1U);
        state.log_path = directory + L"piinput-caret-demo.log";
        state.log = _wfopen(state.log_path.c_str(), L"w, ccs=UTF-8");
    }
    if (state.log == nullptr) {
        wchar_t temp_path[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, temp_path) != 0U) {
            state.log_path = std::wstring(temp_path) + L"piinput-caret-demo.log";
            state.log = _wfopen(state.log_path.c_str(), L"w, ccs=UTF-8");
        }
    }
    if (state.log != nullptr) {
        (void)std::fwprintf(state.log,
            L"sample,tick_ms,action,caret_l,caret_t,caret_r,caret_b,"
            L"bar_l,bar_t,bar_r,bar_b,dx,dy_below_caret,dpi,monitor\n");
        (void)std::fflush(state.log);
    }

    (void)RegisterHotKey(host, 1, MOD_CONTROL | MOD_ALT, 'Q');
    (void)SetTimer(host, poll_timer_id, poll_interval_ms, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == 1U) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    (void)KillTimer(host, poll_timer_id);
    (void)UnregisterHotKey(host, 1);
    state.bar.destroy();
    if (state.log != nullptr) {
        (void)std::fclose(state.log);
        MessageBoxW(nullptr,
            (L"坐标记录已保存到：\r\n" + state.log_path).c_str(),
            L"PiInput 光标定位演示", MB_ICONINFORMATION);
    }
    g_state = nullptr;
    return 0;
}
