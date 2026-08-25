#include "settings_file.h"

#include "piinput/utf.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kWindowClass[] = L"PiInputSettingsWindow";
constexpr wchar_t kMutexName[] = L"Local\\PiInputSettingsWindow";

constexpr int kTab = 1000;
constexpr int kApply = 1001;
constexpr int kDefaults = 1002;
constexpr int kClose = 1003;
constexpr int kPreview = 1004;
constexpr int kRowsCombo = 1005;
constexpr int kFirstField = 1100;

// Every option the engine reads, described as data rather than as thirty
// near-identical blocks of control creation. Adding an option means adding a
// row here and a case in the two switch statements below.
enum class Kind : std::uint8_t { choice, number, toggle, text };

// Which value a control edits, kept separate from its control id so the layout
// can move without touching the load and store code.
enum class Field : std::uint8_t {
    schema, default_language,
    uv_compatibility, accept_u_colon, incomplete_candidates, simplified_pinyin,
    pinyin_user_learning,
    items_per_row, visible_rows, max_items, font_size, window_height, horizontal,
    equal_key, minus_key, down_key, up_key,
    punctuation_mode, bracket_style, command_enabled, command_hotkey, middle_dot_alias,
    symbol_tool,
    english_enabled, english_builtin, english_user_dictionary, english_user_learning,
    english_items_per_row,
    hot_reload, prefix_beam_width, prefix_scan_limit,
};

struct Row final {
    int page;
    Kind kind;
    Field field;
    const wchar_t* label;
    std::array<const wchar_t*, 5U> options;  // choice rows only
    unsigned minimum;                        // number rows only
    unsigned maximum;
};

constexpr std::array<const wchar_t*, 5U> kNone{};
constexpr std::array<const wchar_t*, 5U> kRowKeys{
    L"下一行", L"上一行", nullptr, nullptr, nullptr};

constexpr std::array<Row, 31U> kRows{{
    {0, Kind::choice, Field::schema, L"输入方案",
        {L"全拼", L"小鹤双拼", L"自然码", L"微软双拼", L"智能 ABC"}, 0U, 0U},
    {0, Kind::choice, Field::default_language, L"默认输入语言",
        {L"中文", L"英文", nullptr, nullptr, nullptr}, 0U, 0U},
    {0, Kind::toggle, Field::uv_compatibility, L"ü 可以用 v 输入", kNone, 0U, 0U},
    {0, Kind::toggle, Field::accept_u_colon, L"接受 u: 写法", kNone, 0U, 0U},
    {0, Kind::toggle, Field::incomplete_candidates, L"音节没打完也给候选", kNone, 0U, 0U},
    {0, Kind::toggle, Field::simplified_pinyin, L"简拼（全拼下只打声母）", kNone, 0U, 0U},
    {0, Kind::toggle, Field::pinyin_user_learning, L"记住用词习惯", kNone, 0U, 0U},

    {1, Kind::number, Field::items_per_row, L"每行候选数", kNone, 5U, 9U},
    {1, Kind::number, Field::visible_rows, L"展开候选行数", kNone, 1U, 6U},
    {1, Kind::number, Field::max_items, L"候选总数上限", kNone, 9U, 180U},
    {1, Kind::number, Field::font_size, L"候选文字大小", kNone, 10U, 28U},
    {1, Kind::number, Field::window_height, L"单行候选框高度", kNone, 20U, 72U},
    {1, Kind::toggle, Field::horizontal, L"候选横向排列", kNone, 0U, 0U},
    {1, Kind::choice, Field::equal_key, L"= 键", kRowKeys, 0U, 0U},
    {1, Kind::choice, Field::minus_key, L"- 键", kRowKeys, 0U, 0U},
    {1, Kind::choice, Field::down_key, L"↓ 键", kRowKeys, 0U, 0U},
    {1, Kind::choice, Field::up_key, L"↑ 键", kRowKeys, 0U, 0U},

    {2, Kind::choice, Field::punctuation_mode, L"中文输入时的标点",
        {L"中文标点", L"英文标点", L"程序员标点", nullptr, nullptr}, 0U, 0U},
    {2, Kind::choice, Field::bracket_style, L"Shift+[ 和 Shift+]",
        {L"{ }", L"「 」", nullptr, nullptr, nullptr}, 0U, 0U},
    {2, Kind::choice, Field::command_hotkey, L"符号面板快捷键",
        {L"Ctrl+Alt+`", L"Ctrl+`", L"不使用", nullptr, nullptr}, 0U, 0U},
    {2, Kind::toggle, Field::command_enabled, L"启用符号面板", kNone, 0U, 0U},
    {2, Kind::toggle, Field::middle_dot_alias, L"也可以用 · 打开符号面板", kNone, 0U, 0U},
    {2, Kind::text, Field::symbol_tool, L"托盘符号工具", kNone, 0U, 0U},

    {3, Kind::toggle, Field::english_enabled, L"启用英文候选", kNone, 0U, 0U},
    {3, Kind::toggle, Field::english_builtin, L"使用内置英文词库", kNone, 0U, 0U},
    {3, Kind::toggle, Field::english_user_dictionary, L"使用自定义英文词库", kNone, 0U, 0U},
    {3, Kind::toggle, Field::english_user_learning, L"记住英文用词习惯", kNone, 0U, 0U},
    {3, Kind::number, Field::english_items_per_row, L"每行英文候选数", kNone, 5U, 9U},

    {4, Kind::toggle, Field::hot_reload, L"保存后自动生效", kNone, 0U, 0U},
    {4, Kind::number, Field::prefix_beam_width, L"前缀搜索宽度", kNone, 8U, 128U},
    {4, Kind::number, Field::prefix_scan_limit, L"前缀扫描上限", kNone, 128U, 16384U},
}};

constexpr std::array<const wchar_t*, 5U> kPages{
    L"输入", L"候选窗", L"标点符号", L"英文", L"高级"};

struct AppState final {
    std::filesystem::path settings_path;
    piinput::SettingsSnapshot settings;
    HWND tab{};
    HWND preview{};
    HFONT preview_font{};
    std::array<HWND, kRows.size()> controls{};
    std::array<HWND, kRows.size()> labels{};
};

[[nodiscard]] unsigned combo_number(HWND combo, unsigned fallback);

// Numeric drop-downs answer the mouse wheel by one step, which is how these
// values are usually adjusted -- nudge the size, look at the preview, nudge
// again.
LRESULT CALLBACK numeric_wheel_proc(
    const HWND control,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam,
    const UINT_PTR,
    const DWORD_PTR reference) {
    if (message == WM_MOUSEWHEEL) {
        const auto count = SendMessageW(control, CB_GETCOUNT, 0U, 0U);
        const auto current = SendMessageW(control, CB_GETCURSEL, 0U, 0U);
        if (count != CB_ERR && current != CB_ERR && count > 0) {
            const auto next = piinput::windows::step_numeric_setting(
                static_cast<std::uint32_t>(current),
                GET_WHEEL_DELTA_WPARAM(wparam),
                0U,
                static_cast<std::uint32_t>(count - 1));
            if (static_cast<LRESULT>(next) != current) {
                SendMessageW(control, CB_SETCURSEL, static_cast<WPARAM>(next), 0U);
                const HWND parent = GetParent(control);
                if (parent != nullptr) {
                    SendMessageW(parent, WM_COMMAND,
                        MAKEWPARAM(GetDlgCtrlID(control), CBN_SELCHANGE),
                        reinterpret_cast<LPARAM>(control));
                }
            }
        }
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
    (void)reference;
}

std::filesystem::path default_settings_path() {
    PWSTR local = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)) && local != nullptr) {
        result = std::filesystem::path(local) / L"PiInput" / L"UserData" / L"settings.ini";
    }
    if (local != nullptr) CoTaskMemFree(local);
    return result;
}

std::filesystem::path command_line_settings_path() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::filesystem::path result = default_settings_path();
    if (arguments != nullptr) {
        for (int index = 1; index + 1 < count; ++index) {
            if (std::wstring_view(arguments[index]) == L"--settings") {
                result = arguments[index + 1];
                break;
            }
        }
        LocalFree(arguments);
    }
    return result;
}

HWND control(
    const wchar_t* window_class,
    const wchar_t* label,
    const DWORD style,
    const int x, const int y, const int width, const int height,
    const HWND parent,
    const int id) {
    return CreateWindowExW(
        0U, window_class, label, WS_CHILD | style,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

BOOL CALLBACK apply_gui_font(const HWND child, LPARAM) {
    SendMessageW(child, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return TRUE;
}

// One label column, one control column, one row per option. Rows are counted
// per page, so every page starts at the same height and nothing is positioned
// by hand.
constexpr int kContentLeft = 26;
constexpr int kContentTop = 54;
constexpr int kRowHeight = 27;
constexpr int kLabelWidth = 150;
constexpr int kControlLeft = kContentLeft + kLabelWidth + 10;
constexpr int kControlWidth = 190;
constexpr int kWindowWidth = 452;
constexpr int kWindowHeight = 462;

void create_rows(const HWND window, AppState& state) {
    std::array<int, kPages.size()> used{};
    for (std::size_t index = 0U; index < kRows.size(); ++index) {
        const auto& row = kRows[index];
        const auto page = static_cast<std::size_t>(row.page);
        const int id = kFirstField + static_cast<int>(index);
        const int y = kContentTop + kRowHeight * used[page];
        ++used[page];
        if (row.kind == Kind::toggle) {
            state.controls[index] = control(L"BUTTON", row.label,
                BS_AUTOCHECKBOX | WS_TABSTOP,
                kContentLeft, y + 3, kLabelWidth + 10 + kControlWidth, 20, window, id);
            continue;
        }
        state.labels[index] = control(L"STATIC", row.label, SS_LEFT,
            kContentLeft, y + 5, kLabelWidth, 20, window, 0);
        if (row.kind == Kind::text) {
            state.controls[index] = control(L"EDIT", L"",
                WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                kControlLeft, y + 2, kControlWidth, 22, window, id);
            continue;
        }
        state.controls[index] = control(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            kControlLeft, y, kControlWidth, 240, window, id);
        if (row.kind == Kind::choice) {
            for (const auto* option : row.options) {
                if (option == nullptr) break;
                SendMessageW(state.controls[index], CB_ADDSTRING, 0U,
                    reinterpret_cast<LPARAM>(option));
            }
            continue;
        }
        // Wide ranges step coarsely; listing 16384 entries one by one would be
        // unusable, and the engine accepts anything in between just the same.
        const unsigned span = row.maximum - row.minimum;
        const unsigned step = span > 200U ? 512U : span > 40U ? 8U : 1U;
        for (unsigned value = row.minimum; value <= row.maximum; value += step) {
            const auto label = std::to_wstring(value);
            SendMessageW(state.controls[index], CB_ADDSTRING, 0U,
                reinterpret_cast<LPARAM>(label.c_str()));
        }
        SetWindowSubclass(state.controls[index], numeric_wheel_proc, 1U, 0U);
    }
}

[[nodiscard]] std::size_t row_of(const Field field) {
    for (std::size_t index = 0U; index < kRows.size(); ++index) {
        if (kRows[index].field == field) return index;
    }
    return 0U;
}

// Shows the candidate bar at the size being chosen, so font size and row height
// can be judged without saving and going back to typing.
void update_preview(AppState& state) {
    if (state.preview == nullptr) return;
    const unsigned font_size =
        combo_number(state.controls[row_of(Field::font_size)], 16U);
    const unsigned height =
        combo_number(state.controls[row_of(Field::window_height)], 40U);
    if (state.preview_font != nullptr) DeleteObject(state.preview_font);
    const UINT dpi = GetDpiForWindow(state.preview);
    state.preview_font = CreateFontW(
        -MulDiv(static_cast<int>(font_size), static_cast<int>((std::max)(dpi, 96U)), 96),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const int y = kContentTop + kRowHeight * 10 + 6;
    MoveWindow(state.preview, kContentLeft, y,
        kLabelWidth + 10 + kControlWidth, static_cast<int>(height), TRUE);
    InvalidateRect(state.preview, nullptr, TRUE);
}

void draw_preview(AppState& state, const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_WINDOW));
    FrameRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_BTNSHADOW));
    const auto previous = SelectObject(item.hDC,
        state.preview_font != nullptr ? state.preview_font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(item.hDC, TRANSPARENT);

    RECT text = item.rcItem;
    text.left += 8;
    text.right -= 8;
    // The first candidate carries the same highlight the candidate bar uses.
    SIZE first{};
    const std::wstring highlighted = L"1. 现在";
    GetTextExtentPoint32W(item.hDC, highlighted.c_str(),
        static_cast<int>(highlighted.size()), &first);
    RECT marker{text.left - 3, item.rcItem.top + 3,
                text.left + first.cx + 3, item.rcItem.bottom - 3};
    const HBRUSH accent = CreateSolidBrush(RGB(241, 235, 255));
    if (accent != nullptr) {
        const auto previous_brush = SelectObject(item.hDC, accent);
        const auto previous_pen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RoundRect(item.hDC, marker.left, marker.top, marker.right, marker.bottom, 8, 8);
        SelectObject(item.hDC, previous_pen);
        SelectObject(item.hDC, previous_brush);
        DeleteObject(accent);
    }
    SetTextColor(item.hDC, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(item.hDC, L"1. 现在  2. 输入法  3. 设置", -1, &text,
        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(item.hDC, previous);
}

void show_page(AppState& state, const int page) {
    for (std::size_t index = 0U; index < kRows.size(); ++index) {
        const int mode = kRows[index].page == page ? SW_SHOW : SW_HIDE;
        if (state.controls[index] != nullptr) ShowWindow(state.controls[index], mode);
        if (state.labels[index] != nullptr) ShowWindow(state.labels[index], mode);
    }
    // The preview only means anything next to the size controls.
    if (state.preview != nullptr) {
        ShowWindow(state.preview, page == 1 ? SW_SHOW : SW_HIDE);
    }
}

unsigned combo_number(const HWND combo, const unsigned fallback) {
    const auto selection = SendMessageW(combo, CB_GETCURSEL, 0U, 0U);
    if (selection == CB_ERR) return fallback;
    std::array<wchar_t, 16U> text{};
    if (SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selection),
            reinterpret_cast<LPARAM>(text.data())) == CB_ERR) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text.data(), &end, 10);
    return end == text.data() ? fallback : static_cast<unsigned>(value);
}

// Picks the closest listed entry, since coarse ranges skip most numbers.
void select_number(const HWND combo, const unsigned value) {
    const auto count = SendMessageW(combo, CB_GETCOUNT, 0U, 0U);
    if (count == CB_ERR || count == 0) return;
    int best = 0;
    unsigned best_distance = 0xFFFFFFFFU;
    for (int index = 0; index < static_cast<int>(count); ++index) {
        std::array<wchar_t, 16U> text{};
        if (SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index),
                reinterpret_cast<LPARAM>(text.data())) == CB_ERR) {
            continue;
        }
        const auto entry = static_cast<unsigned>(std::wcstoul(text.data(), nullptr, 10));
        const unsigned distance = entry > value ? entry - value : value - entry;
        if (distance < best_distance) {
            best_distance = distance;
            best = index;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(best), 0U);
}

[[nodiscard]] int schema_index(const piinput::InputSchema schema) {
    switch (schema) {
    case piinput::InputSchema::full: return 0;
    case piinput::InputSchema::natural: return 2;
    case piinput::InputSchema::mspy: return 3;
    case piinput::InputSchema::abc: return 4;
    case piinput::InputSchema::flypy: break;
    }
    return 1;
}

[[nodiscard]] piinput::InputSchema schema_from_index(const int index) {
    switch (index) {
    case 0: return piinput::InputSchema::full;
    case 2: return piinput::InputSchema::natural;
    case 3: return piinput::InputSchema::mspy;
    case 4: return piinput::InputSchema::abc;
    default: break;
    }
    return piinput::InputSchema::flypy;
}

void load_into_controls(AppState& state) {
    const auto& settings = state.settings;
    const auto choose = [&state](const std::size_t index, const int selection) {
        SendMessageW(state.controls[index], CB_SETCURSEL,
            static_cast<WPARAM>(selection), 0U);
    };
    const auto check = [&state](const std::size_t index, const bool checked) {
        SendMessageW(state.controls[index], BM_SETCHECK,
            checked ? BST_CHECKED : BST_UNCHECKED, 0U);
    };
    const auto number = [&state](const std::size_t index, const unsigned value) {
        select_number(state.controls[index], value);
    };
    const auto row_index = [](const piinput::RowNavigationAction action) {
        return action == piinput::RowNavigationAction::previous_row ? 1 : 0;
    };
    for (std::size_t index = 0U; index < kRows.size(); ++index) {
        switch (kRows[index].field) {
        case Field::schema: choose(index, schema_index(settings.general.schema)); break;
        case Field::default_language:
            choose(index, settings.general.default_language ==
                piinput::DefaultInputLanguage::english ? 1 : 0);
            break;
        case Field::uv_compatibility: check(index, settings.pinyin.uv_compatibility); break;
        case Field::accept_u_colon: check(index, settings.pinyin.accept_u_colon); break;
        case Field::incomplete_candidates:
            check(index, settings.pinyin.incomplete_candidates); break;
        case Field::simplified_pinyin: check(index, settings.pinyin.simplified_pinyin); break;
        case Field::pinyin_user_learning: check(index, settings.pinyin.user_learning); break;
        case Field::items_per_row: number(index, settings.candidates.items_per_row); break;
        case Field::visible_rows: number(index, settings.candidates.visible_rows); break;
        case Field::max_items: number(index, settings.candidates.max_items); break;
        case Field::font_size: number(index, settings.candidates.font_size); break;
        case Field::window_height: number(index, settings.candidates.window_height); break;
        case Field::horizontal: check(index, settings.candidates.horizontal); break;
        case Field::equal_key: choose(index, row_index(settings.candidates.equal_key)); break;
        case Field::minus_key: choose(index, row_index(settings.candidates.minus_key)); break;
        case Field::down_key: choose(index, row_index(settings.candidates.down_key)); break;
        case Field::up_key: choose(index, row_index(settings.candidates.up_key)); break;
        case Field::punctuation_mode:
            choose(index, settings.punctuation == piinput::PunctuationMode::english ? 1
                : settings.punctuation == piinput::PunctuationMode::programmer ? 2 : 0);
            break;
        case Field::bracket_style:
            choose(index, settings.punctuation_bracket_style ==
                piinput::PunctuationBracketStyle::wechat ? 1 : 0);
            break;
        case Field::command_enabled: check(index, settings.commands.enabled); break;
        case Field::command_hotkey:
            choose(index, settings.commands.hotkey == piinput::CommandHotkey::ctrl_grave ? 1
                : settings.commands.hotkey == piinput::CommandHotkey::disabled ? 2 : 0);
            break;
        case Field::middle_dot_alias: check(index, settings.commands.middle_dot_alias); break;
        case Field::symbol_tool:
            SetWindowTextW(state.controls[index],
                piinput::utf8_to_wide(settings.general.symbol_tool).c_str());
            break;
        case Field::english_enabled: check(index, settings.english.enabled); break;
        case Field::english_builtin: check(index, settings.english.builtin_dictionary); break;
        case Field::english_user_dictionary:
            check(index, settings.english.user_dictionary); break;
        case Field::english_user_learning: check(index, settings.english.user_learning); break;
        case Field::english_items_per_row: number(index, settings.english.items_per_row); break;
        case Field::hot_reload: check(index, settings.general.hot_reload); break;
        case Field::prefix_beam_width: number(index, settings.pinyin.prefix_beam_width); break;
        case Field::prefix_scan_limit: number(index, settings.pinyin.prefix_scan_limit); break;
        }
    }
}

void store_from_controls(AppState& state) {
    auto& settings = state.settings;
    const auto choice = [&state](const std::size_t index) {
        const auto selection = SendMessageW(state.controls[index], CB_GETCURSEL, 0U, 0U);
        return selection == CB_ERR ? 0 : static_cast<int>(selection);
    };
    const auto checked = [&state](const std::size_t index) {
        return SendMessageW(state.controls[index], BM_GETCHECK, 0U, 0U) == BST_CHECKED;
    };
    const auto row_action = [](const int index) {
        return index == 1 ? piinput::RowNavigationAction::previous_row
                          : piinput::RowNavigationAction::next_row;
    };
    for (std::size_t index = 0U; index < kRows.size(); ++index) {
        const auto& row = kRows[index];
        const auto number = [&] { return combo_number(state.controls[index], row.minimum); };
        switch (row.field) {
        case Field::schema: settings.general.schema = schema_from_index(choice(index)); break;
        case Field::default_language:
            settings.general.default_language = choice(index) == 1
                ? piinput::DefaultInputLanguage::english
                : piinput::DefaultInputLanguage::chinese;
            break;
        case Field::uv_compatibility: settings.pinyin.uv_compatibility = checked(index); break;
        case Field::accept_u_colon: settings.pinyin.accept_u_colon = checked(index); break;
        case Field::incomplete_candidates:
            settings.pinyin.incomplete_candidates = checked(index); break;
        case Field::simplified_pinyin: settings.pinyin.simplified_pinyin = checked(index); break;
        case Field::pinyin_user_learning: settings.pinyin.user_learning = checked(index); break;
        case Field::items_per_row: settings.candidates.items_per_row = number(); break;
        case Field::visible_rows: settings.candidates.visible_rows = number(); break;
        case Field::max_items: settings.candidates.max_items = number(); break;
        case Field::font_size: settings.candidates.font_size = number(); break;
        case Field::window_height: settings.candidates.window_height = number(); break;
        case Field::horizontal: settings.candidates.horizontal = checked(index); break;
        case Field::equal_key: settings.candidates.equal_key = row_action(choice(index)); break;
        case Field::minus_key: settings.candidates.minus_key = row_action(choice(index)); break;
        case Field::down_key: settings.candidates.down_key = row_action(choice(index)); break;
        case Field::up_key: settings.candidates.up_key = row_action(choice(index)); break;
        case Field::punctuation_mode:
            settings.punctuation = choice(index) == 1 ? piinput::PunctuationMode::english
                : choice(index) == 2 ? piinput::PunctuationMode::programmer
                                     : piinput::PunctuationMode::chinese;
            break;
        case Field::bracket_style:
            settings.punctuation_bracket_style = choice(index) == 1
                ? piinput::PunctuationBracketStyle::wechat
                : piinput::PunctuationBracketStyle::sogou;
            break;
        case Field::command_enabled: settings.commands.enabled = checked(index); break;
        case Field::command_hotkey:
            settings.commands.hotkey = choice(index) == 1 ? piinput::CommandHotkey::ctrl_grave
                : choice(index) == 2 ? piinput::CommandHotkey::disabled
                                     : piinput::CommandHotkey::ctrl_alt_grave;
            break;
        case Field::middle_dot_alias: settings.commands.middle_dot_alias = checked(index); break;
        case Field::symbol_tool: {
            std::wstring value(1024U, L' ');
            const int length = GetWindowTextW(
                state.controls[index], value.data(), static_cast<int>(value.size()));
            value.resize(length > 0 ? static_cast<std::size_t>(length) : 0U);
            settings.general.symbol_tool = piinput::wide_to_utf8(value.c_str());
            break;
        }
        case Field::english_enabled: settings.english.enabled = checked(index); break;
        case Field::english_builtin: settings.english.builtin_dictionary = checked(index); break;
        case Field::english_user_dictionary:
            settings.english.user_dictionary = checked(index); break;
        case Field::english_user_learning: settings.english.user_learning = checked(index); break;
        case Field::english_items_per_row: settings.english.items_per_row = number(); break;
        case Field::hot_reload: settings.general.hot_reload = checked(index); break;
        case Field::prefix_beam_width: settings.pinyin.prefix_beam_width = number(); break;
        case Field::prefix_scan_limit: settings.pinyin.prefix_scan_limit = number(); break;
        }
    }
}

LRESULT CALLBACK window_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_CREATE && state != nullptr) {
        state->tab = CreateWindowExW(
            0U, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            12, 10, kWindowWidth - 24, kWindowHeight - 62, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTab)),
            GetModuleHandleW(nullptr), nullptr);
        for (std::size_t page = 0U; page < kPages.size(); ++page) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(kPages[page]);
            SendMessageW(state->tab, TCM_INSERTITEMW,
                static_cast<WPARAM>(page), reinterpret_cast<LPARAM>(&item));
        }
        create_rows(window, *state);
        state->preview = control(L"STATIC", L"", SS_OWNERDRAW,
            kContentLeft, kContentTop + kRowHeight * 10 + 6,
            kLabelWidth + 10 + kControlWidth, 40, window, kPreview);

        const int button_y = kWindowHeight - 38;
        const int button_right = kWindowWidth - 24;
        control(L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE,
            button_right - 270, button_y, 84, 26, window, kApply);
        control(L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE,
            button_right - 178, button_y, 84, 26, window, kDefaults);
        control(L"BUTTON", L"关闭", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE,
            button_right - 86, button_y, 84, 26, window, kClose);

        std::string error;
        state->settings = piinput::windows::load_all_settings(state->settings_path, error);
        load_into_controls(*state);
        EnumChildWindows(window, apply_gui_font, 0);
        update_preview(*state);
        show_page(*state, 0);
        return 0;
    }
    if (message == WM_DRAWITEM && state != nullptr) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (item != nullptr && item->CtlID == static_cast<UINT>(kPreview)) {
            draw_preview(*state, *item);
            return TRUE;
        }
    }
    if (message == WM_NOTIFY && state != nullptr) {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header != nullptr && header->hwndFrom == state->tab &&
            header->code == TCN_SELCHANGE) {
            show_page(*state,
                static_cast<int>(SendMessageW(state->tab, TCM_GETCURSEL, 0U, 0U)));
            return 0;
        }
    }
    if (message == WM_CTLCOLORSTATIC) {
        // Labels and checkboxes sit on the dialog itself rather than on the tab
        // body, so they need the dialog's own background to look right.
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (message == WM_COMMAND && state != nullptr) {
        const int id = LOWORD(wparam);
        if (HIWORD(wparam) == CBN_SELCHANGE &&
            (id == kFirstField + static_cast<int>(row_of(Field::font_size)) ||
                id == kFirstField + static_cast<int>(row_of(Field::window_height)))) {
            update_preview(*state);
            return 0;
        }
        if (id == kClose) {
            DestroyWindow(window);
            return 0;
        }
        if (id == kDefaults) {
            state->settings = piinput::default_settings();
            load_into_controls(*state);
            update_preview(*state);
            return 0;
        }
        if (id == kApply) {
            store_from_controls(*state);
            std::string error;
            if (piinput::windows::save_all_settings_atomic(
                    state->settings_path, state->settings, error)) {
                MessageBoxW(window, L"设置已保存，将从下一次输入开始生效。",
                    L"PiInput", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(window, L"设置保存失败，请检查文件权限。",
                    L"PiInput", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
    }
    if (message == WM_DESTROY) {
        if (state != nullptr && state->preview_font != nullptr) {
            DeleteObject(state->preview_font);
            state->preview_font = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX common_controls{
        sizeof(common_controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&common_controls);
    // CreateMutexW only promises to set ERROR_ALREADY_EXISTS; it does not
    // promise to clear the last error when it creates a fresh mutex. Reading
    // the flag without clearing it first meant whatever InitCommonControlsEx
    // happened to leave behind could be mistaken for "already running", and
    // the program exited with no window and no message at all.
    SetLastError(ERROR_SUCCESS);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Hand the existing window to the user. If there is no window to hand
        // over, the mutex is a leftover rather than a running instance, so fall
        // through and open one -- silently exiting leaves no way in.
        if (const HWND existing = FindWindowW(kWindowClass, nullptr); existing != nullptr) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            CloseHandle(mutex);
            return 0;
        }
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassExW(&window_class) == 0U) return 1;

    AppState state{.settings_path = command_line_settings_path()};
    RECT frame{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRectEx(&frame,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_APPWINDOW);
    const HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass,
        L"PiInput 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        frame.right - frame.left, frame.bottom - frame.top,
        nullptr, nullptr, instance, &state);
    if (window == nullptr) return 2;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        if (IsDialogMessageW(window, &message) != FALSE) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (mutex != nullptr) CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
