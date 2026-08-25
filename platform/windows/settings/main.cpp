#include "settings_file.h"
#include "windows_tool_templates.h"

#include "piinput/utf.h"

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kWindowClass[] = L"PiInputSettingsWindow";
constexpr wchar_t kShortcutEditorClass[] = L"PiInputShortcutEditorWindow";
constexpr wchar_t kToolTemplatesClass[] = L"PiInputWindowsToolTemplatesWindow";
constexpr wchar_t kMutexName[] = L"Local\\PiInputSettingsWindow";

constexpr int kTab = 1000;
constexpr int kApply = 1001;
constexpr int kDefaults = 1002;
constexpr int kClose = 1003;
constexpr int kPreview = 1004;
constexpr int kRowsCombo = 1005;
constexpr int kShortcutList = 1010;
constexpr int kShortcutAdd = 1011;
constexpr int kShortcutEdit = 1012;
constexpr int kShortcutDelete = 1013;
constexpr int kShortcutTemplates = 1014;
constexpr int kShortcutHint = 1015;
constexpr int kFirstField = 1100;
constexpr int kEditorAliases = 1200;
constexpr int kEditorPosition = 1201;
constexpr int kEditorIcon = 1202;
constexpr int kEditorName = 1203;
constexpr int kEditorTarget = 1204;
constexpr int kEditorOk = 1205;
constexpr int kEditorCancel = 1206;
constexpr int kTemplateSearch = 1300;
constexpr int kTemplateCategory = 1301;
constexpr int kTemplateList = 1302;
constexpr int kTemplateAdd = 1303;
constexpr int kTemplateCancel = 1304;

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
        {L"中文标点", L"英文标点", nullptr, nullptr, nullptr}, 0U, 0U},
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

constexpr std::array<const wchar_t*, 6U> kPages{
    L"输入", L"候选窗", L"标点符号", L"英文", L"高级", L"快捷调用"};

struct AppState final {
    std::filesystem::path settings_path;
    piinput::SettingsSnapshot settings;
    HWND tab{};
    HWND preview{};
    HFONT preview_font{};
    std::array<HWND, kRows.size()> controls{};
    std::array<HWND, kRows.size()> labels{};
    HWND shortcut_list{};
    HWND shortcut_add{};
    HWND shortcut_edit{};
    HWND shortcut_delete{};
    HWND shortcut_templates{};
    HWND shortcut_hint{};
};

struct ShortcutEditorState final {
    piinput::CustomShortcutSettings value;
    bool accepted{};
    bool done{};
    HWND aliases{};
    HWND position{};
    HWND icon{};
    HWND name{};
    HWND target{};
};

struct ToolTemplateState final {
    piinput::CustomShortcutSettings value;
    bool accepted{};
    bool done{};
    HWND search{};
    HWND category{};
    HWND list{};
};

[[nodiscard]] unsigned combo_number(HWND combo, unsigned fallback);
BOOL CALLBACK apply_gui_font(HWND child, LPARAM parameter);

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

[[nodiscard]] std::string control_text_utf8(const HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    value.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return piinput::wide_to_utf8(value.c_str());
}

[[nodiscard]] bool valid_alias_text(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 256U) return false;
    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            ch == ',' || ch == ';' || ch == ' ' || ch == '\t';
    });
}

void editor_label(
    const HWND window,
    const wchar_t* text,
    const int y) {
    (void)control(L"STATIC", text, SS_LEFT | WS_VISIBLE,
        18, y + 4, 100, 22, window, 0);
}

LRESULT CALLBACK shortcut_editor_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* state = reinterpret_cast<ShortcutEditorState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<ShortcutEditorState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_CREATE && state != nullptr) {
        constexpr int left = 120;
        constexpr int width = 390;
        editor_label(window, L"触发码", 18);
        editor_label(window, L"候选位置", 50);
        editor_label(window, L"图标", 82);
        editor_label(window, L"名称", 114);
        editor_label(window, L"调用程序", 146);
        state->aliases = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP |
            WS_BORDER | ES_AUTOHSCROLL, left, 18, width, 24, window, kEditorAliases);
        state->position = control(L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP |
            CBS_DROPDOWNLIST, left, 50, 100, 200, window, kEditorPosition);
        for (unsigned position = 2U; position <= 9U; ++position) {
            const auto text = std::to_wstring(position);
            SendMessageW(state->position, CB_ADDSTRING, 0U,
                reinterpret_cast<LPARAM>(text.c_str()));
        }
        state->icon = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP |
            WS_BORDER | ES_AUTOHSCROLL, left, 82, 100, 24, window, kEditorIcon);
        state->name = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP |
            WS_BORDER | ES_AUTOHSCROLL, left, 114, width, 24, window, kEditorName);
        state->target = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP |
            WS_BORDER | ES_AUTOHSCROLL, left, 146, width, 24, window, kEditorTarget);
        (void)control(L"STATIC",
            L"多个触发码用逗号分隔；支持 EXE、HTML、URL、ms-settings: 和 cmd:命令。",
            WS_VISIBLE | SS_LEFT, 18, 180, 492, 22, window, 0);
        (void)control(L"BUTTON", L"确定", WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            326, 211, 88, 28, window, kEditorOk);
        (void)control(L"BUTTON", L"取消", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            422, 211, 88, 28, window, kEditorCancel);
        SetWindowTextW(state->aliases, piinput::utf8_to_wide(state->value.aliases).c_str());
        SendMessageW(state->position, CB_SETCURSEL,
            static_cast<WPARAM>((std::clamp)(state->value.position, 2U, 9U) - 2U), 0U);
        SetWindowTextW(state->icon, piinput::utf8_to_wide(state->value.icon).c_str());
        SetWindowTextW(state->name, piinput::utf8_to_wide(state->value.name).c_str());
        SetWindowTextW(state->target, piinput::utf8_to_wide(state->value.target).c_str());
        EnumChildWindows(window, apply_gui_font, 0);
        SetFocus(state->aliases);
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        const int id = LOWORD(wparam);
        if (id == kEditorCancel) {
            DestroyWindow(window);
            return 0;
        }
        if (id == kEditorOk) {
            piinput::CustomShortcutSettings value;
            value.aliases = control_text_utf8(state->aliases);
            const auto selected = SendMessageW(state->position, CB_GETCURSEL, 0U, 0U);
            value.position = selected == CB_ERR ? 2U
                : static_cast<std::uint32_t>(selected + 2U);
            value.icon = control_text_utf8(state->icon);
            value.name = control_text_utf8(state->name);
            value.target = control_text_utf8(state->target);
            if (!valid_alias_text(value.aliases)) {
                MessageBoxW(window, L"请填写英文字母触发码；多个触发码用逗号分隔。",
                    L"PiInput", MB_OK | MB_ICONWARNING);
                SetFocus(state->aliases);
                return 0;
            }
            if (value.icon.size() > 32U || value.name.empty() || value.name.size() > 96U) {
                MessageBoxW(window, L"请填写名称；图标和名称不能过长。",
                    L"PiInput", MB_OK | MB_ICONWARNING);
                SetFocus(state->name);
                return 0;
            }
            if (value.target.empty() || value.target.size() > 2048U) {
                MessageBoxW(window, L"请填写要调用的程序、文件、URL 或命令。",
                    L"PiInput", MB_OK | MB_ICONWARNING);
                SetFocus(state->target);
                return 0;
            }
            state->value = std::move(value);
            state->accepted = true;
            DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && state != nullptr) {
        state->done = true;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

[[nodiscard]] bool edit_shortcut(
    const HWND owner,
    piinput::CustomShortcutSettings& value,
    const bool adding) {
    ShortcutEditorState state{.value = value};
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    constexpr int width = 548;
    constexpr int height = 292;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    const HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kShortcutEditorClass,
        adding ? L"添加快捷调用" : L"编辑快捷调用",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (window == nullptr) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        if (IsDialogMessageW(window, &message) != FALSE) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (state.accepted) value = std::move(state.value);
    return state.accepted;
}

[[nodiscard]] std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

void fill_tool_templates(ToolTemplateState& state) {
    if (state.list == nullptr) return;
    const std::wstring query = lowercase(piinput::utf8_to_wide(
        control_text_utf8(state.search)));
    const auto category_index = SendMessageW(state.category, CB_GETCURSEL, 0U, 0U);
    std::wstring category;
    if (category_index > 0 && category_index != CB_ERR) {
        const auto length = SendMessageW(state.category, CB_GETLBTEXTLEN,
            static_cast<WPARAM>(category_index), 0U);
        if (length > 0) {
            category.resize(static_cast<std::size_t>(length) + 1U);
            SendMessageW(state.category, CB_GETLBTEXT,
                static_cast<WPARAM>(category_index),
                reinterpret_cast<LPARAM>(category.data()));
            category.resize(static_cast<std::size_t>(length));
        }
    }
    ListView_DeleteAllItems(state.list);
    const auto templates = piinput::windows::windows_tool_templates();
    int row = 0;
    for (std::size_t index = 0U; index < templates.size(); ++index) {
        const auto& item = templates[index];
        if (!category.empty() && category != item.category) continue;
        const std::wstring searchable = lowercase(
            std::wstring(item.category) + L" " + item.name + L" " + item.target);
        if (!query.empty() && searchable.find(query) == std::wstring::npos) continue;
        LVITEMW list_item{};
        list_item.mask = LVIF_TEXT | LVIF_PARAM;
        list_item.iItem = row;
        list_item.pszText = const_cast<wchar_t*>(item.category);
        list_item.lParam = static_cast<LPARAM>(index);
        const int inserted = ListView_InsertItem(state.list, &list_item);
        if (inserted >= 0) {
            ListView_SetItemText(state.list, inserted, 1,
                const_cast<wchar_t*>(item.name));
            ListView_SetItemText(state.list, inserted, 2,
                const_cast<wchar_t*>(item.target));
            ++row;
        }
    }
}

void accept_tool_template(const HWND window, ToolTemplateState& state) {
    const int row = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (row < 0) return;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (ListView_GetItem(state.list, &item) == FALSE) return;
    const auto templates = piinput::windows::windows_tool_templates();
    const auto index = static_cast<std::size_t>(item.lParam);
    if (index >= templates.size()) return;
    state.value = {
        {}, 2U, "🛠", piinput::wide_to_utf8(templates[index].name),
        piinput::wide_to_utf8(templates[index].target),
    };
    state.accepted = true;
    DestroyWindow(window);
}

LRESULT CALLBACK tool_templates_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* state = reinterpret_cast<ToolTemplateState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<ToolTemplateState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_CREATE && state != nullptr) {
        (void)control(L"STATIC", L"搜索", WS_VISIBLE | SS_LEFT,
            16, 17, 42, 22, window, 0);
        state->search = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP |
            WS_BORDER | ES_AUTOHSCROLL, 60, 14, 280, 24, window, kTemplateSearch);
        (void)control(L"STATIC", L"分类", WS_VISIBLE | SS_LEFT,
            358, 17, 42, 22, window, 0);
        state->category = control(L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP |
            CBS_DROPDOWNLIST, 402, 14, 260, 260, window, kTemplateCategory);
        SendMessageW(state->category, CB_ADDSTRING, 0U,
            reinterpret_cast<LPARAM>(L"全部分类"));
        std::vector<std::wstring> categories;
        for (const auto& item : piinput::windows::windows_tool_templates()) {
            if (std::find(categories.begin(), categories.end(), item.category) ==
                categories.end()) {
                categories.emplace_back(item.category);
            }
        }
        for (const auto& category : categories) {
            SendMessageW(state->category, CB_ADDSTRING, 0U,
                reinterpret_cast<LPARAM>(category.c_str()));
        }
        SendMessageW(state->category, CB_SETCURSEL, 0U, 0U);
        state->list = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
                WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            16, 48, 646, 350, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTemplateList)),
            GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(state->list,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        const std::array<std::pair<const wchar_t*, int>, 3U> columns{{
            {L"分类", 150}, {L"名称", 170}, {L"调用目标", 320},
        }};
        for (std::size_t index = 0U; index < columns.size(); ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(columns[index].first);
            column.cx = columns[index].second;
            column.iSubItem = static_cast<int>(index);
            ListView_InsertColumn(state->list, static_cast<int>(index), &column);
        }
        (void)control(L"STATIC",
            L"选择模板后还要填写触发码；导入后就是普通快捷项，可以继续编辑或删除。",
            WS_VISIBLE | SS_LEFT, 16, 407, 500, 22, window, 0);
        (void)control(L"BUTTON", L"添加到快捷调用",
            WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            504, 436, 158, 28, window, kTemplateAdd);
        (void)control(L"BUTTON", L"取消", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            408, 436, 88, 28, window, kTemplateCancel);
        EnumChildWindows(window, apply_gui_font, 0);
        fill_tool_templates(*state);
        SetFocus(state->search);
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        const int id = LOWORD(wparam);
        if ((id == kTemplateSearch && HIWORD(wparam) == EN_CHANGE) ||
            (id == kTemplateCategory && HIWORD(wparam) == CBN_SELCHANGE)) {
            fill_tool_templates(*state);
            return 0;
        }
        if (id == kTemplateAdd) {
            accept_tool_template(window, *state);
            return 0;
        }
        if (id == kTemplateCancel) {
            DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_NOTIFY && state != nullptr) {
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header != nullptr && header->hwndFrom == state->list &&
            header->code == NM_DBLCLK) {
            accept_tool_template(window, *state);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && state != nullptr) {
        state->done = true;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

[[nodiscard]] bool choose_tool_template(
    const HWND owner,
    piinput::CustomShortcutSettings& value) {
    ToolTemplateState state;
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    constexpr int width = 700;
    constexpr int height = 520;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    const HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kToolTemplatesClass, L"PiInput - Windows 工具模板",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (window == nullptr) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        if (IsDialogMessageW(window, &message) != FALSE) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (state.accepted) value = std::move(state.value);
    return state.accepted;
}

void refresh_shortcut_buttons(AppState& state) {
    const bool selected = state.shortcut_list != nullptr &&
        ListView_GetNextItem(state.shortcut_list, -1, LVNI_SELECTED) >= 0;
    if (state.shortcut_edit != nullptr) EnableWindow(state.shortcut_edit, selected);
    if (state.shortcut_delete != nullptr) EnableWindow(state.shortcut_delete, selected);
}

void refresh_shortcut_list(AppState& state) {
    if (state.shortcut_list == nullptr) return;
    ListView_DeleteAllItems(state.shortcut_list);
    for (std::size_t index = 0U; index < state.settings.custom_shortcuts.size(); ++index) {
        const auto& shortcut = state.settings.custom_shortcuts[index];
        const std::array<std::wstring, 5U> values{
            piinput::utf8_to_wide(shortcut.aliases),
            std::to_wstring(shortcut.position),
            piinput::utf8_to_wide(shortcut.icon),
            piinput::utf8_to_wide(shortcut.name),
            piinput::utf8_to_wide(shortcut.target),
        };
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<wchar_t*>(values[0].c_str());
        item.lParam = static_cast<LPARAM>(index);
        const int inserted = ListView_InsertItem(state.shortcut_list, &item);
        for (int column = 1; inserted >= 0 && column < 5; ++column) {
            ListView_SetItemText(state.shortcut_list, inserted, column,
                const_cast<wchar_t*>(values[static_cast<std::size_t>(column)].c_str()));
        }
    }
    refresh_shortcut_buttons(state);
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
constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 530;

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
    const int shortcut_mode = page == 5 ? SW_SHOW : SW_HIDE;
    for (const HWND item : {state.shortcut_list, state.shortcut_add,
             state.shortcut_edit, state.shortcut_delete,
             state.shortcut_templates, state.shortcut_hint}) {
        if (item != nullptr) ShowWindow(item, shortcut_mode);
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
            choose(index, settings.punctuation == piinput::PunctuationMode::english ? 1 : 0);
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

        state->shortcut_list = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            24, 60, 650, 355, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutList)),
            GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(state->shortcut_list,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        const std::array<std::pair<const wchar_t*, int>, 5U> columns{{
            {L"触发码", 170}, {L"候选位置", 76}, {L"图标", 52},
            {L"名称", 125}, {L"调用程序", 221},
        }};
        for (std::size_t index = 0U; index < columns.size(); ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(columns[index].first);
            column.cx = columns[index].second;
            column.iSubItem = static_cast<int>(index);
            ListView_InsertColumn(state->shortcut_list, static_cast<int>(index), &column);
        }
        state->shortcut_add = control(L"BUTTON", L"添加", BS_PUSHBUTTON | WS_TABSTOP,
            690, 62, 102, 28, window, kShortcutAdd);
        state->shortcut_edit = control(L"BUTTON", L"编辑", BS_PUSHBUTTON | WS_TABSTOP,
            690, 98, 102, 28, window, kShortcutEdit);
        state->shortcut_delete = control(L"BUTTON", L"删除", BS_PUSHBUTTON | WS_TABSTOP,
            690, 134, 102, 28, window, kShortcutDelete);
        state->shortcut_templates = control(L"BUTTON", L"Windows 工具模板",
            BS_PUSHBUTTON | WS_TABSTOP, 690, 184, 102, 44, window, kShortcutTemplates);
        state->shortcut_hint = control(L"STATIC",
            L"多个触发码用逗号分隔。英文候选开启时可在英文状态调用；关闭时请按 Shift 切到中文。",
            SS_LEFT, 24, 424, 660, 34, window, kShortcutHint);

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
        refresh_shortcut_list(*state);
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
        if (header != nullptr && header->hwndFrom == state->shortcut_list) {
            if (header->code == LVN_ITEMCHANGED) {
                refresh_shortcut_buttons(*state);
                return 0;
            }
            if (header->code == NM_DBLCLK) {
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(kShortcutEdit, BN_CLICKED),
                    reinterpret_cast<LPARAM>(state->shortcut_edit));
                return 0;
            }
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
            refresh_shortcut_list(*state);
            update_preview(*state);
            return 0;
        }
        if (id == kShortcutAdd) {
            if (state->settings.custom_shortcuts.size() >= piinput::max_custom_shortcuts) {
                MessageBoxW(window, L"快捷调用最多 64 项。", L"PiInput",
                    MB_OK | MB_ICONWARNING);
                return 0;
            }
            piinput::CustomShortcutSettings value;
            if (edit_shortcut(window, value, true)) {
                state->settings.custom_shortcuts.push_back(std::move(value));
                refresh_shortcut_list(*state);
                const int row = static_cast<int>(state->settings.custom_shortcuts.size() - 1U);
                ListView_SetItemState(state->shortcut_list, row,
                    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state->shortcut_list, row, FALSE);
            }
            return 0;
        }
        if (id == kShortcutEdit) {
            const int row = ListView_GetNextItem(state->shortcut_list, -1, LVNI_SELECTED);
            if (row >= 0 && static_cast<std::size_t>(row) <
                    state->settings.custom_shortcuts.size()) {
                auto value = state->settings.custom_shortcuts[static_cast<std::size_t>(row)];
                if (edit_shortcut(window, value, false)) {
                    state->settings.custom_shortcuts[static_cast<std::size_t>(row)] =
                        std::move(value);
                    refresh_shortcut_list(*state);
                    ListView_SetItemState(state->shortcut_list, row,
                        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            return 0;
        }
        if (id == kShortcutDelete) {
            const int row = ListView_GetNextItem(state->shortcut_list, -1, LVNI_SELECTED);
            if (row >= 0 && static_cast<std::size_t>(row) <
                    state->settings.custom_shortcuts.size() &&
                MessageBoxW(window, L"删除选中的快捷调用？", L"PiInput",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                state->settings.custom_shortcuts.erase(
                    state->settings.custom_shortcuts.begin() + row);
                refresh_shortcut_list(*state);
            }
            return 0;
        }
        if (id == kShortcutTemplates) {
            if (state->settings.custom_shortcuts.size() >= piinput::max_custom_shortcuts) {
                MessageBoxW(window, L"快捷调用最多 64 项。", L"PiInput",
                    MB_OK | MB_ICONWARNING);
                return 0;
            }
            piinput::CustomShortcutSettings value;
            if (!choose_tool_template(window, value)) return 0;
            const bool duplicate = std::any_of(
                state->settings.custom_shortcuts.begin(),
                state->settings.custom_shortcuts.end(),
                [&](const auto& current) {
                    return current.name == value.name && current.target == value.target;
                });
            if (duplicate && MessageBoxW(window,
                    L"列表中已经有相同名称和调用目标，仍然再添加一份？",
                    L"PiInput", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
                return 0;
            }
            if (edit_shortcut(window, value, true)) {
                state->settings.custom_shortcuts.push_back(std::move(value));
                refresh_shortcut_list(*state);
                const int row = static_cast<int>(state->settings.custom_shortcuts.size() - 1U);
                ListView_SetItemState(state->shortcut_list, row,
                    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(state->shortcut_list, row, FALSE);
            }
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
        sizeof(common_controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES |
            ICC_LISTVIEW_CLASSES};
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
    WNDCLASSEXW editor_class{};
    editor_class.cbSize = sizeof(editor_class);
    editor_class.hInstance = instance;
    editor_class.lpfnWndProc = shortcut_editor_proc;
    editor_class.lpszClassName = kShortcutEditorClass;
    editor_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    editor_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassExW(&editor_class) == 0U) return 1;
    WNDCLASSEXW templates_class{};
    templates_class.cbSize = sizeof(templates_class);
    templates_class.hInstance = instance;
    templates_class.lpfnWndProc = tool_templates_proc;
    templates_class.lpszClassName = kToolTemplatesClass;
    templates_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    templates_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassExW(&templates_class) == 0U) return 1;

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
