#include "mode_indicator.h"

#include "client_identity.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using piinput::windows::InputModeMark;
using piinput::windows::mode_indicator_visible_ms;
using piinput::windows::ModeIndicator;
using piinput::windows::mode_mark_for;
using piinput::windows::client_id_from;
using piinput::windows::process_client_id;

// The whole point of splitting English in two is that CapsLock is the one bit of
// state the keyboard does not show on most laptops.
void test_marks_report_what_gets_typed() {
    check(mode_mark_for(false, false) == InputModeMark::chinese, "中文应显示中");
    check(mode_mark_for(true, false) == InputModeMark::english, "英文小写应显示 a");
    check(mode_mark_for(true, true) == InputModeMark::english_caps, "英文大写应显示 A");

    // CapsLock does not produce capitals while Chinese is on: the letters still
    // go through the pinyin decoder in lower case. Reporting A there would be a
    // lie about what the next keystroke does.
    check(
        mode_mark_for(false, true) == InputModeMark::chinese,
        "中文模式下开着大写锁定仍应显示中");
}

void test_visible_time_is_a_glance_not_a_dialog() {
    check(mode_indicator_visible_ms >= 1500U, "提示至少要停留够读一眼");
    check(mode_indicator_visible_ms <= 3000U, "提示不应久留遮挡正文");
}

[[nodiscard]] RECT work_area() {
    RECT area{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0U, &area, 0U) == FALSE) {
        area = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    return area;
}

[[nodiscard]] bool popup_rect(ModeIndicator& indicator, RECT& rect) {
    const HWND window = indicator.window();
    return window != nullptr && GetWindowRect(window, &rect) != FALSE;
}

void test_popup_sits_above_the_caret() {
    const RECT area = work_area();
    const LONG caret_top = area.top + (area.bottom - area.top) / 2;
    const RECT caret{area.left + 200, caret_top, area.left + 201, caret_top + 20};

    ModeIndicator indicator;
    indicator.show(InputModeMark::chinese, caret);
    RECT popup{};
    if (!popup_rect(indicator, popup)) {
        check(false, "提示窗应已创建");
        return;
    }
    // Below the caret is where the candidate window goes; the two must not
    // cover each other when CapsLock is pressed in the middle of a word.
    check(popup.bottom <= caret.top, "提示窗应位于光标上方，给候选框让出下方");
    check(popup.right > popup.left && popup.bottom > popup.top, "提示窗应有实际尺寸");
}

void test_popup_flips_below_when_there_is_no_room_above() {
    const RECT area = work_area();
    const RECT caret{area.left + 200, area.top, area.left + 201, area.top + 20};

    ModeIndicator indicator;
    indicator.show(InputModeMark::english_caps, caret);
    RECT popup{};
    if (!popup_rect(indicator, popup)) {
        check(false, "提示窗应已创建");
        return;
    }
    check(popup.top >= area.top, "贴着屏幕顶端时提示窗不应跑出工作区");
    check(popup.top >= caret.top, "上方放不下时应翻到光标下方");
}

void test_popup_stays_inside_the_work_area() {
    const RECT area = work_area();
    // A caret hard against the right edge, as in a maximised window.
    const RECT caret{area.right - 2, area.bottom - 40, area.right - 1, area.bottom - 20};

    ModeIndicator indicator;
    indicator.show(InputModeMark::english, caret);
    RECT popup{};
    if (!popup_rect(indicator, popup)) {
        check(false, "提示窗应已创建");
        return;
    }
    check(popup.right <= area.right, "提示窗右边不应越出工作区");
    check(popup.left >= area.left, "提示窗左边不应越出工作区");
    check(popup.bottom <= area.bottom, "提示窗底边不应越出工作区");
}

void test_hide_and_destroy_are_safe_to_repeat() {
    ModeIndicator indicator;
    // Hiding one that was never shown must not touch a null window.
    indicator.hide();
    indicator.destroy();

    const RECT area = work_area();
    const RECT caret{area.left + 100, area.top + 300, area.left + 101, area.top + 320};
    indicator.show(InputModeMark::chinese, caret);
    check(indicator.window() != nullptr, "show 之后应存在窗口");
    check(IsWindowVisible(indicator.window()) != FALSE, "show 之后窗口应可见");

    indicator.hide();
    check(IsWindowVisible(indicator.window()) == FALSE, "hide 之后窗口应隐藏");

    // Showing again reuses the same window rather than leaking a new one.
    const HWND first = indicator.window();
    indicator.show(InputModeMark::english, caret);
    check(indicator.window() == first, "再次 show 应复用同一窗口");

    indicator.destroy();
    check(indicator.window() == nullptr, "destroy 之后句柄应清空");
    indicator.destroy();
}

// Clicking the taskbar indicator means the caret belongs to another process and
// GetGUIThreadInfo has nothing for us. The popup still has to land somewhere the
// user is looking rather than at the origin.
//
// This one checks the monitor the popup actually landed on, not the primary
// one. With no caret the position is anchored to whatever window happens to be
// in front, which during a full test run is some other test's window and not
// necessarily on the primary display -- comparing against the primary work area
// made this assertion depend on the state of the desktop around it.
void test_missing_caret_falls_back_to_a_visible_spot() {
    ModeIndicator indicator;
    indicator.show(InputModeMark::english, std::nullopt);
    RECT popup{};
    if (!popup_rect(indicator, popup)) {
        check(false, "没有光标信息时也应创建提示窗");
        return;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR screen = MonitorFromWindow(indicator.window(), MONITOR_DEFAULTTONEAREST);
    if (screen == nullptr || GetMonitorInfoW(screen, &monitor) == FALSE) {
        check(false, "应能取到提示窗所在的显示器");
        return;
    }
    const RECT& area = monitor.rcWork;
    check(popup.left >= area.left && popup.right <= area.right, "回退位置应在工作区内");
    check(popup.top >= area.top && popup.bottom <= area.bottom, "回退位置应在工作区内");
}

void test_last_caret_is_reused_when_none_is_given() {
    const RECT area = work_area();
    const LONG caret_top = area.top + (area.bottom - area.top) / 2;
    const RECT caret{area.left + 320, caret_top, area.left + 321, caret_top + 20};

    ModeIndicator indicator;
    indicator.show(InputModeMark::chinese, caret);
    RECT first{};
    if (!popup_rect(indicator, first)) {
        check(false, "提示窗应已创建");
        return;
    }

    // Shift switches the language while typing, then the taskbar icon switches
    // it back with no caret to be had. The mark should not jump to the middle of
    // the screen for the second one.
    indicator.show(InputModeMark::english, std::nullopt);
    RECT second{};
    if (!popup_rect(indicator, second)) {
        check(false, "提示窗应仍然存在");
        return;
    }
    check(first.left == second.left, "缺少光标时应沿用上一次的横向位置");
    check(first.top == second.top, "缺少光标时应沿用上一次的纵向位置");
}

// The Host keys a session on this id plus a counter that restarts at 1 in every
// process. Windows recycles process ids and the Host keeps sessions for the whole
// login, so an id that is only the process id lets a new process be handed a dead
// one -- including whether that session had been switched to English, which shows
// up as a fresh window coming up in English against the configured default.
void test_client_id_separates_processes_that_share_an_id() {
    constexpr std::uint64_t recycled = 4242U;
    const std::uint64_t first = client_id_from(recycled, 132000000000000000ULL);
    const std::uint64_t second = client_id_from(recycled, 132000000000000001ULL);
    check(first != second, "同一进程号、不同启动时刻必须得到不同标识");
    check(first != recycled, "标识不能就是进程号本身");

    // One tick apart is the closest two processes can ever be; a whole second
    // apart must obviously differ too.
    const std::uint64_t later = client_id_from(recycled, 132000010000000000ULL);
    check(later != first && later != second, "相隔较久的两次启动也应不同");
}

void test_client_id_is_stable_and_never_zero() {
    check(process_client_id() == process_client_id(), "同一进程内标识必须稳定");
    check(process_client_id() != 0U, "零是协议里的无效客户端，不能产生");
    // A process that reports no creation time still needs a usable id.
    check(client_id_from(0U, 0U) != 0U, "取不到启动时刻时也不能产生零");
}

// Different processes alive at the same moment must not collide either.
void test_client_id_separates_distinct_processes() {
    constexpr std::uint64_t born = 132000000000000000ULL;
    const std::uint64_t a = client_id_from(1000U, born);
    const std::uint64_t b = client_id_from(1001U, born);
    check(a != b, "同一时刻启动的不同进程必须得到不同标识");
}

// Everything above checks geometry, which is the part that can be asserted.
// Whether the popup actually looks right -- rounded corners, the glyph centred,
// the colours readable against the theme -- only a pair of eyes can judge, so
// this puts one on screen and pumps messages until the dismissal timer fires.
int run_demo(const InputModeMark mark) {
    ModeIndicator indicator;
    indicator.show(mark, std::nullopt);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (indicator.window() != nullptr && IsWindowVisible(indicator.window()) == FALSE) {
            break;
        }
    }
    return 0;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc >= 2 && std::string(argv[1]) == "--demo") {
        const std::string which = argc >= 3 ? argv[2] : "chinese";
        if (which == "english") return run_demo(InputModeMark::english);
        if (which == "caps") return run_demo(InputModeMark::english_caps);
        return run_demo(InputModeMark::chinese);
    }
    test_marks_report_what_gets_typed();
    test_client_id_separates_processes_that_share_an_id();
    test_client_id_is_stable_and_never_zero();
    test_client_id_separates_distinct_processes();
    test_visible_time_is_a_glance_not_a_dialog();
    test_popup_sits_above_the_caret();
    test_popup_flips_below_when_there_is_no_room_above();
    test_popup_stays_inside_the_work_area();
    test_hide_and_destroy_are_safe_to_repeat();
    test_missing_caret_falls_back_to_a_visible_spot();
    test_last_caret_is_reused_when_none_is_given();
    if (failures == 0) std::cout << "mode indicator tests passed\n";
    return failures == 0 ? 0 : 1;
}
