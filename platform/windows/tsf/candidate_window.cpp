#include "candidate_window.h"

#include "piinput/candidate_layout.h"

#include <algorithm>
#include <utility>

namespace piinput::windows {

std::optional<CandidateContextAction> candidate_context_action_from_command(
    const UINT command) noexcept {
    switch (command) {
    case 0U: return CandidateContextAction::dismiss;
    case 1U: return CandidateContextAction::pin_first;
    case 2U: return CandidateContextAction::delete_candidate;
    case 3U: return CandidateContextAction::unpin;
    default: return std::nullopt;
    }
}
namespace {

constexpr wchar_t kCandidateClass[] = L"PiInputTsfCandidateWindow";
constexpr int kPadding = 8;
constexpr int kCompactWindowHeight = 40;
constexpr int kRowHeight = 30;
constexpr int kMinimumItemWidth = 64;
constexpr int kMaximumItemWidth = 720;
constexpr int kMaximumWindowWidth = 600;
constexpr int kExpandButtonWidth = 32;
constexpr int kMenuButtonWidth = 40;
constexpr int kToolbarWidth = kExpandButtonWidth + kMenuButtonWidth;
constexpr int kToolbarMenuWidth = 168;
constexpr int kToolbarMenuRowHeight = 36;

}  // namespace

int limit_candidate_window_width(const int desired_width, const UINT dpi) noexcept {
    const int maximum = MulDiv(
        kMaximumWindowWidth, static_cast<int>((std::max)(dpi, 96U)), 96);
    return (std::clamp)(desired_width, 1, (std::max)(maximum, 1));
}

int candidate_window_height(
    const std::size_t visible_rows,
    const UINT dpi,
    const std::uint32_t single_row_height) noexcept {
    const std::size_t rows = (std::max)(visible_rows, std::size_t{1U});
    const int height_dip = static_cast<int>(single_row_height) +
        static_cast<int>(rows - 1U) * kRowHeight;
    return MulDiv(height_dip, static_cast<int>((std::max)(dpi, 96U)), 96);
}

RECT candidate_row_rect(
    const std::size_t row,
    const UINT dpi,
    const std::uint32_t single_row_height) noexcept {
    const int scale_dpi = static_cast<int>((std::max)(dpi, 96U));
    const int first_height = MulDiv(
        static_cast<int>(single_row_height), scale_dpi, 96);
    const int expansion_height = MulDiv(kRowHeight, scale_dpi, 96);
    const int top = row == 0U
        ? 0
        : first_height + static_cast<int>(row - 1U) * expansion_height;
    const int height = row == 0U ? first_height : expansion_height;
    return {0, top, 0, top + height};
}

int candidate_text_top(const RECT& row, const int text_height) noexcept {
    const int row_height = (std::max)(0L, row.bottom - row.top);
    return row.top + (std::max)(0, row_height - (std::max)(text_height, 0)) / 2;
}

CandidateToolbarAction candidate_toolbar_hit_test(
    const POINT point,
    const RECT& client,
    const bool menu_open,
    const UINT dpi,
    const std::uint32_t single_row_height) noexcept {
    const auto scale = [dpi](const int value) noexcept {
        return MulDiv(value, static_cast<int>((std::max)(dpi, 96U)), 96);
    };
    const LONG toolbar_left = client.right - scale(kToolbarWidth);
    const LONG menu_button_left = client.right - scale(kMenuButtonWidth);
    const LONG first_row_bottom = client.top + scale(static_cast<int>(single_row_height));
    if (point.x >= toolbar_left && point.x < client.right &&
        point.y >= client.top && point.y < first_row_bottom) {
        return point.x >= menu_button_left
            ? CandidateToolbarAction::open_menu
            : CandidateToolbarAction::expand_candidates;
    }
    if (!menu_open) return CandidateToolbarAction::none;
    const LONG menu_left = client.right - scale(kToolbarMenuWidth);
    const LONG menu_bottom = client.bottom;
    const LONG menu_top = menu_bottom - scale(2 * kToolbarMenuRowHeight);
    if (point.x < menu_left || point.x >= client.right ||
        point.y < menu_top || point.y >= menu_bottom) {
        return CandidateToolbarAction::none;
    }
    return point.y < menu_top + scale(kToolbarMenuRowHeight)
        ? CandidateToolbarAction::symbols
        : CandidateToolbarAction::settings;
}

RECT place_candidate_window(
    const RECT& caret,
    SIZE desired,
    const RECT& work_area,
    const UINT dpi,
    const int anchor_gap) noexcept {
    const LONG scale = static_cast<LONG>((std::max)(dpi, 96U));
    const LONG gap = (std::max)(0L, static_cast<LONG>(anchor_gap) * scale / 96L);
    const LONG width = (std::max)(desired.cx, 1L);
    const LONG height = (std::max)(desired.cy, 1L);
    const LONG margin = (std::max)(1L, 4L * scale / 96L);
    // Some applications report an insertion point far shorter than the line of
    // text it belongs to -- WeChat reports two pixels where an ordinary
    // application on the same machine reports twenty-four. Hugging the bottom
    // of such a rectangle drops the bar onto the text the user is still
    // typing, so treat the anchor as at least one line tall.
    const LONG minimum_line = (std::max)(1L, 16L * scale / 96L);
    const LONG caret_bottom = (std::max)(
        caret.bottom, caret.top + minimum_line);
    const LONG below_top = caret_bottom + gap;
    const LONG above_top = caret.top - gap - height;
    const bool below_fits = below_top + height <= work_area.bottom - margin;
    const bool above_fits = above_top >= work_area.top + margin;
    const LONG top = !below_fits && above_fits ? above_top : below_top;
    return clamp_candidate_window_rect(
        {caret.left, top, caret.left + width, top + height},
        work_area,
        dpi);
}

RECT place_candidate_window_at_text_caret(
    const RECT& caret,
    const SIZE desired,
    const RECT& work_area,
    const UINT dpi,
    const int anchor_gap) noexcept {
    return place_candidate_window(caret, desired, work_area, dpi, anchor_gap);
}

RECT clamp_candidate_window_rect(
    const RECT& candidate,
    const RECT& work_area,
    const UINT dpi) noexcept {
    const LONG scale = static_cast<LONG>((std::max)(dpi, 96U));
    const LONG desired_margin = (std::max)(1L, 4L * scale / 96L);
    const LONG work_width = (std::max)(1L, work_area.right - work_area.left);
    const LONG work_height = (std::max)(1L, work_area.bottom - work_area.top);
    const LONG horizontal_margin = (std::min)(desired_margin, (work_width - 1L) / 2L);
    const LONG vertical_margin = (std::min)(desired_margin, (work_height - 1L) / 2L);
    const LONG available_width = (std::max)(1L, work_width - 2L * horizontal_margin);
    const LONG available_height = (std::max)(1L, work_height - 2L * vertical_margin);
    const LONG width = (std::clamp)(
        (std::max)(candidate.right - candidate.left, 1L), 1L, available_width);
    const LONG height = (std::clamp)(
        (std::max)(candidate.bottom - candidate.top, 1L), 1L, available_height);
    LONG left = candidate.left;
    LONG top = candidate.top;
    left = (std::clamp)(left,
        work_area.left + horizontal_margin,
        work_area.right - horizontal_margin - width);
    top = (std::clamp)(top,
        work_area.top + vertical_margin,
        work_area.bottom - vertical_margin - height);
    return {left, top, left + width, top + height};
}

RECT stabilize_candidate_window_rect(
    const RECT* const locked,
    const RECT& proposed,
    const bool visible_rows_changed) noexcept {
    if (locked == nullptr) return proposed;
    const LONG locked_width = (std::max)(1L, locked->right - locked->left);
    const LONG proposed_width = (std::max)(1L, proposed.right - proposed.left);
    const LONG width = (std::max)(locked_width, proposed_width);
    const LONG locked_height = (std::max)(1L, locked->bottom - locked->top);
    const LONG proposed_height = (std::max)(1L, proposed.bottom - proposed.top);
    const LONG height = visible_rows_changed ? proposed_height : locked_height;
    return {locked->left, locked->top, locked->left + width, locked->top + height};
}

bool should_reanchor_candidate_window(
    const bool geometry_locked,
    const bool locked_to_text_caret,
    const bool incoming_text_caret) noexcept {
    return !geometry_locked || (!locked_to_text_caret && incoming_text_caret);
}

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

    // Owns the popup menus. A menu whose owner is not the foreground window
    // ignores clicks outside itself and stays on screen; the candidate window
    // is WS_EX_NOACTIVATE and can never be the foreground window, so it cannot
    // own its own menus.
    menu_owner_ = CreateWindowExW(
        WS_EX_TOOLWINDOW, kCandidateClass, L"", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kCandidateClass,
        L"",
        WS_POPUP,
        0,
        0,
        480,
        kCompactWindowHeight,
        nullptr,
        nullptr,
        instance,
        this);
    if (window_ == nullptr) {
        return false;
    }
    update_dpi(GetDpiForWindow(window_));
    return true;
}

void CandidateWindow::set_toolbar_handler(
    std::function<void(CandidateToolbarAction)> handler) {
    toolbar_handler_ = std::move(handler);
}

void CandidateWindow::set_candidate_select_handler(
    std::function<void(std::size_t)> handler) {
    candidate_select_handler_ = std::move(handler);
}

void CandidateWindow::set_candidate_context_handler(
    std::function<void(std::size_t, CandidateContextAction)> handler) {
    candidate_context_handler_ = std::move(handler);
}

int CandidateWindow::scaled(const int value) const noexcept {
    return MulDiv(value, static_cast<int>((std::max)(dpi_, 96U)), 96);
}

void CandidateWindow::update_dpi(const UINT dpi) {
    dpi_ = (std::max)(dpi, 96U);
    if (font_ != nullptr) DeleteObject(font_);
    font_ = CreateFontW(
        -scaled(static_cast<int>(visual_.font_size)), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
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
    const std::wstring& composition,
    const std::vector<std::wstring>& candidates,
    const std::size_t selected,
    const std::size_t active_row,
    const std::size_t first_visible_row,
    const std::size_t items_per_row,
    const std::size_t visible_rows,
    const CandidateVisualSettings visual) {
    const std::size_t normalized_items_per_row =
        (std::max)(std::size_t{1U}, items_per_row);
    const std::size_t normalized_visible_rows =
        (std::max)(std::size_t{1U}, visible_rows);
    if (composition_ == composition && candidates_ == candidates &&
        selected_ == selected && active_row_ == active_row &&
        first_visible_row_ == first_visible_row &&
        items_per_row_ == normalized_items_per_row &&
        visible_rows_ == normalized_visible_rows && visual_ == visual) {
        return;
    }
    const std::size_t previous_rows = actual_visible_rows();
    const bool font_changed = visual_.font_size != visual.font_size;
    const bool height_changed = visual_.window_height != visual.window_height;
    composition_ = composition;
    candidates_ = candidates;
    selected_ = selected;
    active_row_ = active_row;
    first_visible_row_ = first_visible_row;
    items_per_row_ = normalized_items_per_row;
    visible_rows_ = normalized_visible_rows;
    visual_ = visual;
    if (font_changed) update_dpi(dpi_);
    layout_dirty_ = true;
    visible_rows_changed_ = geometry_locked_ &&
        (previous_rows != actual_visible_rows() || height_changed);
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void CandidateWindow::show_near_caret() {
    if (window_ == nullptr) {
        return;
    }
    GUITHREADINFO info{sizeof(info)};
    RECT anchor{20, 40, 20, 40};
    const HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread = foreground == nullptr
        ? 0U
        : GetWindowThreadProcessId(foreground, nullptr);
    if (foreground_thread != 0U &&
        GetGUIThreadInfo(foreground_thread, &info) != FALSE && info.hwndCaret != nullptr) {
        POINT top_left{info.rcCaret.left, info.rcCaret.top};
        POINT bottom_right{info.rcCaret.right, info.rcCaret.bottom};
        ClientToScreen(info.hwndCaret, &top_left);
        ClientToScreen(info.hwndCaret, &bottom_right);
        anchor = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
        show_at_anchor(anchor, 4, false);
    } else {
        POINT point{};
        GetCursorPos(&point);
        anchor = {point.x, point.y, point.x, point.y};
        show_at_anchor(anchor, 20, false);
    }
}

void CandidateWindow::show_at_text_caret(const RECT& caret) {
    show_at_anchor(caret, 4, true);
}

void CandidateWindow::show_at_provisional_caret(const RECT& caret) {
    show_at_anchor(caret, 4, false);
}

void CandidateWindow::move_to_target_monitor(const POINT point) {
    // A hidden popup is initially associated with the monitor containing (0, 0).
    // Move it to the text caret first, then ask Windows for that monitor's DPI.
    // CandidatePresenter hides the window while each snapshot/caret pair is staged,
    // so this does not expose an intermediate wrong-sized popup.
    (void)SetWindowPos(window_, nullptr, point.x, point.y, 0, 0,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
    const UINT target_dpi = GetDpiForWindow(window_);
    if (target_dpi != 0U && target_dpi != dpi_) update_dpi(target_dpi);
}

void CandidateWindow::show_at_anchor(
    const RECT& anchor,
    const int anchor_gap,
    const bool text_caret) {
    if (window_ == nullptr) {
        return;
    }

    const bool reanchor = should_reanchor_candidate_window(
        geometry_locked_, locked_to_text_caret_, text_caret);
    if (reanchor && geometry_locked_) {
        geometry_locked_ = false;
        locked_rect_ = {};
    }
    POINT point{anchor.left, anchor.bottom};
    if (!geometry_locked_) move_to_target_monitor(point);

    // Each key anchors twice -- once for the staged snapshot and once for the
    // real text caret. When the window is already locked and nothing about its
    // content changed, the second pass can only reproduce the locked rectangle,
    // so skip the device context and per-item text measurement entirely.
    if (geometry_locked_ && !layout_dirty_ && !visible_rows_changed_ && shown_) {
        if (!reanchor) return;
    }

    int height = desired_height();
    int width = limit_candidate_window_width(desired_width(), dpi_);
    const POINT monitor_point = geometry_locked_
        ? POINT{locked_rect_.left, locked_rect_.top}
        : point;
    const HMONITOR monitor = MonitorFromPoint(monitor_point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    RECT placed{point.x, point.y + scaled(anchor_gap),
                point.x + width, point.y + scaled(anchor_gap) + height};
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        placed = text_caret
            ? place_candidate_window_at_text_caret(
                anchor, {width, height}, monitor_info.rcWork, dpi_, anchor_gap)
            : place_candidate_window(
                anchor, {width, height}, monitor_info.rcWork, dpi_, anchor_gap);
    }
    RECT stable = stabilize_candidate_window_rect(
        geometry_locked_ ? &locked_rect_ : nullptr,
        placed,
        visible_rows_changed_);
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        stable = clamp_candidate_window_rect(stable, monitor_info.rcWork, dpi_);
    }
    point.x = stable.left;
    point.y = stable.top;
    width = stable.right - stable.left;
    height = stable.bottom - stable.top;
    const bool already_placed = shown_ && geometry_locked_ &&
        locked_rect_.left == stable.left && locked_rect_.top == stable.top &&
        locked_rect_.right == stable.right && locked_rect_.bottom == stable.bottom;
    locked_rect_ = stable;
    if (!geometry_locked_ || reanchor) locked_to_text_caret_ = text_caret;
    geometry_locked_ = true;
    if (!already_placed) {
        update_window_region(width, height);
        SetWindowPos(window_, HWND_TOPMOST, point.x, point.y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    shown_ = true;
    layout_dirty_ = false;
    visible_rows_changed_ = false;
}

int CandidateWindow::desired_width() const {
    if (window_ == nullptr) {
        return scaled(480);
    }
    HDC dc = GetDC(window_);
    if (dc == nullptr) {
        return scaled(480);
    }
    const auto previous_font = SelectObject(dc, font_);
    int width = 2 * scaled(kPadding) + scaled(kToolbarWidth);
    for (const int item_width : item_widths(dc)) {
        width += item_width;
    }
    SelectObject(dc, previous_font);
    ReleaseDC(window_, dc);
    return (std::max)(scaled(320), width);
}

int CandidateWindow::desired_height() const noexcept {
    int height = candidate_window_height(
        actual_visible_rows(), dpi_, visual_.window_height);
    if (toolbar_menu_open_) height += scaled(2 * kToolbarMenuRowHeight);
    return height;
}

void CandidateWindow::resize_for_toolbar_menu() {
    if (window_ == nullptr || !geometry_locked_) return;
    int width = (std::max)(1L, locked_rect_.right - locked_rect_.left);
    int height = desired_height();
    locked_rect_.right = locked_rect_.left + width;
    locked_rect_.bottom = locked_rect_.top + height;
    const HMONITOR monitor = MonitorFromRect(&locked_rect_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        locked_rect_ = clamp_candidate_window_rect(locked_rect_, monitor_info.rcWork, dpi_);
    }
    width = (std::max)(1L, locked_rect_.right - locked_rect_.left);
    height = (std::max)(1L, locked_rect_.bottom - locked_rect_.top);
    update_window_region(width, height);
    SetWindowPos(window_, HWND_TOPMOST, locked_rect_.left, locked_rect_.top, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    shown_ = true;
    InvalidateRect(window_, nullptr, FALSE);
}

void CandidateWindow::update_window_region(const int width, const int height) {
    if (window_ == nullptr || width <= 0 || height <= 0) return;
    HRGN region = CreateRoundRectRgn(
        0, 0, width + 1, height + 1, scaled(12), scaled(12));
    if (region != nullptr && SetWindowRgn(window_, region, FALSE) == 0) {
        DeleteObject(region);
    }
}

std::size_t CandidateWindow::actual_visible_rows() const noexcept {
    const std::size_t first = first_visible_row_ * items_per_row_;
    if (first >= candidates_.size()) {
        return 0U;
    }
    const std::size_t remaining = candidates_.size() - first;
    const std::size_t available_rows = (remaining + items_per_row_ - 1U) / items_per_row_;
    return (std::min)(visible_rows_, available_rows);
}

std::vector<int> CandidateWindow::item_widths(HDC dc) const {
    const std::size_t first = first_visible_row_ * items_per_row_;
    if (first >= candidates_.size()) {
        return {};
    }
    const std::size_t capacity = actual_visible_rows() * items_per_row_;
    const std::size_t end = (std::min)(candidates_.size(), first + capacity);
    const std::size_t visible_columns = (std::min)(items_per_row_, end - first);
    std::vector<int> widths(visible_columns, scaled(kMinimumItemWidth));
    for (std::size_t index = first; index < end; ++index) {
        const std::size_t column = index % items_per_row_;
        // The number slot is reserved on every row, not just the active one, so
        // a column keeps the same width and the candidate text stays aligned
        // whether or not its row currently shows numbers.
        const std::wstring text =
            std::to_wstring(column + 1U) + L". " + candidates_[index];
        SIZE extent{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
        widths[column] = (std::max)(
            widths[column],
            (std::clamp)(static_cast<int>(extent.cx) + scaled(18),
                scaled(kMinimumItemWidth), scaled(kMaximumItemWidth)));
    }
    return widths;
}

void CandidateWindow::release_anchor() noexcept {
    geometry_locked_ = false;
    locked_to_text_caret_ = false;
    locked_rect_ = {};
    layout_dirty_ = true;
}

void CandidateWindow::hide() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
    toolbar_menu_open_ = false;
    geometry_locked_ = false;
    locked_to_text_caret_ = false;
    visible_rows_changed_ = false;
    shown_ = false;
    layout_dirty_ = true;
    locked_rect_ = {};
}

void CandidateWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    if (dc == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    HBRUSH background_brush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &client, background_brush);
    DeleteObject(background_brush);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);

    const int padding = scaled(kPadding);
    const int first_row_height = scaled(static_cast<int>(visual_.window_height));
    const int toolbar_width = scaled(kToolbarWidth);

    const std::size_t first = first_visible_row_ * items_per_row_;
    const std::size_t end = (std::min)(
        candidates_.size(), first + actual_visible_rows() * items_per_row_);
    const int available_width = (std::max)(
        1, static_cast<int>(client.right) - 2 * padding - toolbar_width);
    const auto widths = fit_candidate_column_widths(item_widths(dc), available_width);
    visible_item_rects_.clear();
    visible_item_indexes_.clear();
    for (std::size_t index = first; index < end; ++index) {
        const int column = static_cast<int>(index % items_per_row_);
        const int row = static_cast<int>(index / items_per_row_ - first_visible_row_);
        int left = padding;
        for (int preceding = 0; preceding < column; ++preceding) {
            left += widths[static_cast<std::size_t>(preceding)];
        }
        const int fitted_width = widths[static_cast<std::size_t>(column)];
        const RECT row_bounds = candidate_row_rect(
            static_cast<std::size_t>(row), dpi_, visual_.window_height);
        RECT item{left, row_bounds.top,
                  left + fitted_width,
                  row_bounds.bottom};
        if (candidates_[index].empty()) continue;
        visible_item_rects_.push_back(item);
        visible_item_indexes_.push_back(index);
        if (index == selected_ && index / items_per_row_ == active_row_) {
            RECT highlight{
                item.left + scaled(2),
                item.top + scaled(5),
                item.right - scaled(2),
                item.bottom - scaled(5),
            };
            HBRUSH selected_brush = CreateSolidBrush(RGB(241, 235, 255));
            HPEN selected_pen = CreatePen(PS_NULL, 0, RGB(241, 235, 255));
            const auto old_brush = SelectObject(dc, selected_brush);
            const auto old_pen = SelectObject(dc, selected_pen);
            RoundRect(dc, highlight.left, highlight.top, highlight.right, highlight.bottom,
                scaled(10), scaled(10));
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(selected_pen);
            DeleteObject(selected_brush);
        }
        const bool show_number = index / items_per_row_ == active_row_;
        // Always measure and advance past the number slot; only the active row
        // paints the digits. Skipping the slot on other rows made their text
        // start further left than the numbered row above it.
        const std::wstring number = std::to_wstring(column + 1U) + L". ";
        item.left += scaled(4);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        const int text_top = candidate_text_top(item, metrics.tmHeight);
        SetTextColor(dc, index == selected_ ? RGB(112, 63, 190) : RGB(105, 105, 116));
        if (show_number) {
            TextOutW(dc, item.left, text_top,
                number.c_str(), static_cast<int>(number.size()));
        }
        SIZE number_extent{};
        GetTextExtentPoint32W(dc, number.c_str(), static_cast<int>(number.size()),
            &number_extent);
        item.left += number_extent.cx;
        SetTextColor(dc, RGB(30, 28, 35));
        TextOutW(dc, item.left, text_top,
            candidates_[index].c_str(), static_cast<int>(candidates_[index].size()));
    }

    RECT toolbar{
        client.right - toolbar_width,
        client.top,
        client.right,
        client.top + first_row_height,
    };
    HPEN separator = CreatePen(PS_SOLID, 1, RGB(232, 230, 236));
    const auto previous_pen = SelectObject(dc, separator);
    MoveToEx(dc, toolbar.left, toolbar.top + scaled(6), nullptr);
    LineTo(dc, toolbar.left, toolbar.bottom - scaled(6));
    SelectObject(dc, previous_pen);
    DeleteObject(separator);

    const int expand_right = toolbar.left + scaled(kExpandButtonWidth);
    const int chevron_center_x = toolbar.left + scaled(kExpandButtonWidth) / 2;
    const int chevron_center_y = toolbar.top + first_row_height / 2;
    HPEN glyph_pen = CreatePen(PS_SOLID, scaled(2), RGB(92, 87, 101));
    const auto previous_glyph_pen = SelectObject(dc, glyph_pen);
    MoveToEx(dc, chevron_center_x - scaled(4), chevron_center_y - scaled(2), nullptr);
    LineTo(dc, chevron_center_x, chevron_center_y + scaled(2));
    LineTo(dc, chevron_center_x + scaled(4), chevron_center_y - scaled(2));
    MoveToEx(dc, expand_right, toolbar.top + scaled(8), nullptr);
    LineTo(dc, expand_right, toolbar.bottom - scaled(8));
    SelectObject(dc, previous_glyph_pen);
    DeleteObject(glyph_pen);

    const int square = scaled(4);
    const int square_gap = scaled(3);
    const int grid_width = 2 * square + square_gap;
    const int grid_height = 2 * square + square_gap;
    const int grid_left = expand_right + (scaled(kMenuButtonWidth) - grid_width) / 2;
    const int grid_top = toolbar.top + (first_row_height - grid_height) / 2;
    HBRUSH grid_brush = CreateSolidBrush(RGB(95, 95, 95));
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            RECT square_rect{
                grid_left + column * (square + square_gap),
                grid_top + row * (square + square_gap),
                grid_left + column * (square + square_gap) + square,
                grid_top + row * (square + square_gap) + square,
            };
            FillRect(dc, &square_rect, grid_brush);
        }
    }
    DeleteObject(grid_brush);

    HBRUSH border_brush = CreateSolidBrush(RGB(214, 212, 220));
    FrameRect(dc, &client, border_brush);
    DeleteObject(border_brush);

    if (toolbar_menu_open_) {
        const int menu_width = scaled(kToolbarMenuWidth);
        const int menu_row_height = scaled(kToolbarMenuRowHeight);
        const int menu_top = client.bottom - 2 * menu_row_height;
        RECT menu{client.right - menu_width, menu_top, client.right, client.bottom};
        FillRect(dc, &menu, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        FrameRect(dc, &menu, reinterpret_cast<HBRUSH>(COLOR_3DSHADOW + 1));
        RECT divider{menu.left, menu.top + menu_row_height,
                     menu.right, menu.top + menu_row_height + 1};
        FillRect(dc, &divider, reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1));
        RECT symbols{menu.left + scaled(14), menu.top,
                     menu.right - scaled(8), menu.top + menu_row_height};
        RECT settings{menu.left + scaled(14), menu.top + menu_row_height,
                      menu.right - scaled(8), menu.bottom};
        SetTextColor(dc, RGB(25, 25, 25));
        DrawTextW(dc, L"符号", -1, &symbols,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        DrawTextW(dc, L"设置", -1, &settings,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
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
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (message == WM_LBUTTONUP && self != nullptr) {
        RECT client{};
        GetClientRect(window, &client);
        const POINT point{
            static_cast<short>(LOWORD(lparam)),
            static_cast<short>(HIWORD(lparam)),
        };
        const auto action = candidate_toolbar_hit_test(
            point, client, self->toolbar_menu_open_, self->dpi_,
            self->visual_.window_height);
        if (action == CandidateToolbarAction::expand_candidates) {
            if (self->toolbar_handler_) self->toolbar_handler_(action);
            return 0;
        }
        if (action == CandidateToolbarAction::open_menu) {
            self->toolbar_menu_open_ = !self->toolbar_menu_open_;
            self->resize_for_toolbar_menu();
            return 0;
        }
        if (action == CandidateToolbarAction::symbols ||
            action == CandidateToolbarAction::settings) {
            self->toolbar_menu_open_ = false;
            self->resize_for_toolbar_menu();
            if (self->toolbar_handler_) self->toolbar_handler_(action);
            return 0;
        }
        if (self->toolbar_menu_open_) {
            self->toolbar_menu_open_ = false;
            self->resize_for_toolbar_menu();
            return 0;
        }
        // Clicking a candidate picks it, the same as pressing its digit.
        for (std::size_t hit = 0U; hit < self->visible_item_rects_.size(); ++hit) {
            if (!PtInRect(&self->visible_item_rects_[hit], point)) continue;
            if (self->candidate_select_handler_) {
                self->candidate_select_handler_(self->visible_item_indexes_[hit]);
            }
            return 0;
        }
    }
    if (message == WM_RBUTTONUP && self != nullptr) {
        const POINT client_point{
            static_cast<short>(LOWORD(lparam)),
            static_cast<short>(HIWORD(lparam)),
        };
        for (std::size_t hit = 0U; hit < self->visible_item_rects_.size(); ++hit) {
            if (!PtInRect(&self->visible_item_rects_[hit], client_point)) continue;
            HMENU menu = CreatePopupMenu();
            if (menu == nullptr) return 0;
            AppendMenuW(menu, MF_STRING, 1U, L"固定首位");
            AppendMenuW(menu, MF_STRING, 2U, L"删除该词");
            AppendMenuW(menu, MF_STRING, 3U, L"取消固定");
            POINT screen = client_point;
            ClientToScreen(window, &screen);
            // Foreground first, or the menu ignores clicks outside itself and
            // never closes; the trailing message is the documented way to let
            // it go again afterwards.
            const HWND owner = self->menu_owner_ != nullptr ? self->menu_owner_ : window;
            SetForegroundWindow(owner);
            const UINT command = TrackPopupMenuEx(
                menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                screen.x, screen.y, owner, nullptr);
            PostMessageW(owner, WM_NULL, 0U, 0U);
            DestroyMenu(menu);
            const auto action = candidate_context_action_from_command(command);
            if (action.has_value() && self->candidate_context_handler_) {
                self->candidate_context_handler_(
                    self->visible_item_indexes_[hit], *action);
            }
            return 0;
        }
    }
    if (message == WM_DPICHANGED && self != nullptr) {
        self->update_dpi(HIWORD(wparam));
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested != nullptr) {
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace piinput::windows
