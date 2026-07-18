#include "liteime/engine.h"
#include "liteime/symbols.h"
#include "liteime/utf.h"
#include "liteime/windows_compat.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t window_class_name[] = L"LiteIMEPreviewWindow";
constexpr int id_output = 1000;
constexpr int id_input = 1001;
constexpr int id_schema = 1002;
constexpr int id_candidates = 1003;
constexpr int id_status = 1004;
constexpr int id_clear = 1005;

struct CandidateValue {
    std::string text;
    std::string pinyin;
};

struct AppState {
    liteime::Engine engine;
    liteime::SymbolIndex symbols;
    std::filesystem::path lexicon_path;
    std::filesystem::path user_model_path;
    HWND output{};
    HWND input{};
    HWND schema{};
    HWND candidates{};
    HWND status{};
    HWND clear_button{};
    HFONT font{};
    WNDPROC original_input_proc{};
    std::vector<CandidateValue> values;
};

[[nodiscard]] std::filesystem::path module_directory() {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

[[nodiscard]] std::filesystem::path local_app_data() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        throw std::runtime_error("SHGetKnownFolderPath failed");
    }
    const std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

[[nodiscard]] std::filesystem::path command_line_lexicon() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return {};
    }
    std::filesystem::path result;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--lexicon") {
            result = argv[index + 1];
            break;
        }
    }
    LocalFree(argv);
    return result;
}

[[nodiscard]] std::filesystem::path find_lexicon() {
    const auto explicit_path = command_line_lexicon();
    if (!explicit_path.empty()) {
        return explicit_path;
    }

    const auto user_directory = local_app_data() / L"LiteIME" / L"UserData" / L"lexicons";
    const auto combined = user_directory / L"liteime-imported.lex";
    if (std::filesystem::exists(combined)) {
        return combined;
    }
    const auto base_compiled = user_directory / L"liteime-base.lex";
    if (std::filesystem::exists(base_compiled)) {
        return base_compiled;
    }

    const auto installed_data = module_directory().parent_path() / L"data" / L"base_lexicon.tsv";
    if (std::filesystem::exists(installed_data)) {
        return installed_data;
    }
    const auto sample = module_directory().parent_path() / L"data" / L"sample_lexicon.tsv";
    if (std::filesystem::exists(sample)) {
        return sample;
    }
    throw std::runtime_error("No lexicon found. Run import-dicts.ps1 or pass --lexicon <path>.");
}

[[nodiscard]] std::wstring get_window_text(const HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void set_font(const HWND window, const HFONT font) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

[[nodiscard]] std::string selected_schema(const AppState& state) {
    const LRESULT index = SendMessageW(state.schema, CB_GETCURSEL, 0U, 0U);
    switch (index) {
    case 1: return "flypy";
    case 2: return "natural";
    case 3: return "mspy";
    case 4: return "abc";
    default: return "full";
    }
}

void update_candidates(AppState& state) {
    SendMessageW(state.candidates, LB_RESETCONTENT, 0U, 0U);
    state.values.clear();
    const std::wstring wide_input = get_window_text(state.input);
    const std::string input = liteime::wide_to_utf8(wide_input.c_str());
    if (input.empty()) {
        SetWindowTextW(state.status, L"输入拼音后按空格、Enter 或数字键上屏；以分号开头可搜索符号，例如 ;sheshidu");
        return;
    }

    if (input.front() == ';') {
        const auto results = state.symbols.search(input.substr(1U), 20U);
        for (std::size_t index = 0; index < results.size(); ++index) {
            const auto& result = results[index];
            state.values.push_back({result.symbol, {}});
            const std::wstring line = liteime::utf8_to_wide(
                std::to_string(index + 1U) + ". " + result.symbol + "    " + result.name + "    [" + result.category + "]");
            SendMessageW(state.candidates, LB_ADDSTRING, 0U, reinterpret_cast<LPARAM>(line.c_str()));
        }
    } else {
        const auto results = state.engine.query(input, selected_schema(state), 20U);
        for (std::size_t index = 0; index < results.size(); ++index) {
            const auto& result = results[index];
            state.values.push_back({result.word, result.pinyin});
            const std::wstring line = liteime::utf8_to_wide(
                std::to_string(index + 1U) + ". " + result.word + "    " + result.pinyin + "    " +
                std::to_string(result.base_weight));
            SendMessageW(state.candidates, LB_ADDSTRING, 0U, reinterpret_cast<LPARAM>(line.c_str()));
        }
    }

    if (!state.values.empty()) {
        SendMessageW(state.candidates, LB_SETCURSEL, 0U, 0U);
        const std::wstring status = L"词库：" + state.lexicon_path.wstring() + L"    空格/Enter 选择第一项，1~9 选择对应项";
        SetWindowTextW(state.status, status.c_str());
    } else {
        SetWindowTextW(state.status,
            L"没有候选：当前词库中没有这组拼音。v0.1.4 已加入内置基础词库；请重新运行 setup-dev.cmd。 ");
    }
}

void append_output(const HWND output, const std::wstring& value) {
    const int length = GetWindowTextLengthW(output);
    SendMessageW(output, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(output, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str()));
}

bool commit_index(AppState& state, const std::size_t index) {
    if (index >= state.values.size()) {
        return false;
    }
    const CandidateValue selected = state.values[index];
    const std::wstring value = liteime::utf8_to_wide(selected.text);
    append_output(state.output, value);
    if (!selected.pinyin.empty()) {
        state.engine.record_selection(selected.pinyin, selected.text);
        state.engine.save_user_model(state.user_model_path);
    }
    SetWindowTextW(state.input, L"");
    SetFocus(state.input);
    SetWindowTextW(state.status, (L"已上屏：" + value).c_str());
    return true;
}

bool commit_selected(AppState& state) {
    LRESULT selected = SendMessageW(state.candidates, LB_GETCURSEL, 0U, 0U);
    if (selected == LB_ERR) {
        selected = 0;
    }
    return commit_index(state, static_cast<std::size_t>(selected));
}

void move_selection(AppState& state, const int delta) {
    if (state.values.empty()) {
        return;
    }
    LRESULT selected = SendMessageW(state.candidates, LB_GETCURSEL, 0U, 0U);
    if (selected == LB_ERR) {
        selected = 0;
    }
    const int count = static_cast<int>(state.values.size());
    const int next = (static_cast<int>(selected) + delta + count) % count;
    SendMessageW(state.candidates, LB_SETCURSEL, static_cast<WPARAM>(next), 0U);
}

LRESULT CALLBACK input_proc(const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(GetParent(window), GWLP_USERDATA));
    if (state != nullptr && message == WM_KEYDOWN) {
        if (wparam == VK_SPACE || wparam == VK_RETURN) {
            if (commit_selected(*state)) {
                return 0;
            }
        } else if (wparam == VK_DOWN) {
            move_selection(*state, 1);
            return 0;
        } else if (wparam == VK_UP) {
            move_selection(*state, -1);
            return 0;
        } else if (wparam >= L'1' && wparam <= L'9') {
            const std::size_t index = static_cast<std::size_t>(wparam - L'1');
            if (commit_index(*state, index)) {
                return 0;
            }
        } else if (wparam == VK_ESCAPE) {
            SetWindowTextW(state->input, L"");
            return 0;
        }
    }
    return CallWindowProcW(state != nullptr ? state->original_input_proc : DefWindowProcW,
        window, message, wparam, lparam);
}

LRESULT CALLBACK window_proc(const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HWND output_label = CreateWindowExW(0, L"STATIC", L"上屏：", WS_CHILD | WS_VISIBLE,
            16, 14, 56, 28, window, nullptr, nullptr, nullptr);
        state->output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            72, 10, 556, 72, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_output)), nullptr, nullptr);
        state->clear_button = CreateWindowExW(0, L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            642, 10, 70, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_clear)), nullptr, nullptr);

        HWND input_label = CreateWindowExW(0, L"STATIC", L"输入：", WS_CHILD | WS_VISIBLE,
            16, 96, 56, 28, window, nullptr, nullptr, nullptr);
        state->input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            72, 92, 470, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_input)), nullptr, nullptr);
        state->schema = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            552, 92, 160, 200, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_schema)), nullptr, nullptr);
        state->candidates = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL,
            16, 136, 696, 280, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_candidates)), nullptr, nullptr);
        state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            16, 428, 696, 46, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_status)), nullptr, nullptr);

        for (const wchar_t* name : {L"全拼", L"小鹤双拼", L"自然码双拼", L"微软双拼", L"智能 ABC 双拼"}) {
            SendMessageW(state->schema, CB_ADDSTRING, 0U, reinterpret_cast<LPARAM>(name));
        }
        SendMessageW(state->schema, CB_SETCURSEL, 0U, 0U);
        for (const HWND control : {output_label, state->output, state->clear_button, input_label,
                                   state->input, state->schema, state->candidates, state->status}) {
            set_font(control, state->font);
        }
        state->original_input_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            state->input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(input_proc)));
        update_candidates(*state);
        SetFocus(state->input);
        return 0;
    }
    case WM_COMMAND:
        if (state != nullptr) {
            const int control_id = LOWORD(wparam);
            const int notification = HIWORD(wparam);
            if ((control_id == id_input && notification == EN_CHANGE) ||
                (control_id == id_schema && notification == CBN_SELCHANGE)) {
                try {
                    update_candidates(*state);
                } catch (const std::exception& error) {
                    SetWindowTextW(state->status, liteime::utf8_to_wide(error.what()).c_str());
                }
            } else if (control_id == id_candidates && notification == LBN_DBLCLK) {
                commit_selected(*state);
            } else if (control_id == id_clear && notification == BN_CLICKED) {
                SetWindowTextW(state->output, L"");
                SetFocus(state->input);
            }
        }
        return 0;
    case WM_SIZE:
        if (state != nullptr) {
            const int width = LOWORD(lparam);
            const int height = HIWORD(lparam);
            MoveWindow(state->output, 72, 10, (std::max)(120, width - 204), 72, TRUE);
            MoveWindow(state->clear_button, width - 102, 10, 86, 32, TRUE);
            MoveWindow(state->input, 72, 92, (std::max)(100, width - 258), 32, TRUE);
            MoveWindow(state->schema, width - 176, 92, 160, 200, TRUE);
            MoveWindow(state->candidates, 16, 136, width - 32, (std::max)(100, height - 196), TRUE);
            MoveWindow(state->status, 16, height - 52, width - 32, 42, TRUE);
        }
        return 0;
    case WM_DESTROY:
        if (state != nullptr && state->font != nullptr) {
            DeleteObject(state->font);
            state->font = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);

        AppState state;
        state.lexicon_path = find_lexicon();
        state.user_model_path = local_app_data() / L"LiteIME" / L"UserData" / L"user_model.tsv";
        state.engine.load_lexicon(state.lexicon_path);
        state.engine.load_user_model(state.user_model_path);
        state.symbols.load_tsv(module_directory().parent_path() / L"data" / L"symbols.tsv");

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance;
        window_class.lpfnWndProc = window_proc;
        window_class.lpszClassName = window_class_name;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassExW(&window_class) == 0U) {
            throw std::runtime_error("RegisterClassExW failed");
        }

        const std::wstring title = L"LiteIME 输入核心预览 v" + liteime::utf8_to_wide(LITEIME_VERSION);
        HWND window = CreateWindowExW(0, window_class_name, title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 560,
            nullptr, nullptr, instance, &state);
        if (window == nullptr) {
            throw std::runtime_error("CreateWindowExW failed");
        }
        ShowWindow(window, show_command);
        UpdateWindow(window);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, liteime::utf8_to_wide(error.what()).c_str(),
            L"LiteIME Preview Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
