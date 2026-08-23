#include <windows.h>
#include <inputscope.h>

#include "controlled_tsf_host_protocol.h"

#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace piinput::tests::controlled_tsf;

struct HostState final {
    HWND edit_a{};
    HWND edit_b{};
    HWND password{};
    HWND pin{};
};

using SetInputScopeFunction = HRESULT(WINAPI*)(HWND, InputScope);

[[nodiscard]] bool set_input_scope(
    const HWND control,
    const InputScope scope) noexcept {
    const HMODULE module = LoadLibraryW(L"msctf.dll");
    if (module == nullptr) return false;
    const auto function = reinterpret_cast<SetInputScopeFunction>(
        GetProcAddress(module, "SetInputScope"));
    const bool applied = function != nullptr && SUCCEEDED(function(control, scope));
    FreeLibrary(module);
    return applied;
}

[[nodiscard]] HWND create_edit(
    const HWND parent,
    const int id,
    const DWORD style) noexcept {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | style,
        0,
        0,
        100,
        24,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

void layout_controls(const HWND window, HostState& state) noexcept {
    RECT client{};
    if (!GetClientRect(window, &client)) return;
    constexpr int margin = 12;
    constexpr int row_height = 28;
    constexpr int gap = 8;
    const int width = (client.right - client.left) - margin * 2;
    int top = margin;
    for (const HWND control :
         std::array{state.edit_a, state.edit_b, state.password, state.pin}) {
        if (control != nullptr) {
            MoveWindow(control, margin, top, width, row_height, TRUE);
        }
        top += row_height + gap;
    }
}

void clear_control(const HWND control) noexcept {
    if (control != nullptr) SetWindowTextW(control, L"");
}

LRESULT CALLBACK window_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* state = reinterpret_cast<HostState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        // DefWindowProc performs the standard non-client initialization,
        // including publishing the title used by the external controller.
        return DefWindowProcW(window, message, wparam, lparam);
    }
    case WM_CREATE:
        state = reinterpret_cast<HostState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return -1;
        state->edit_a = create_edit(window, edit_a_id, ES_MULTILINE | ES_AUTOVSCROLL);
        state->edit_b = create_edit(window, edit_b_id, 0U);
        state->password = create_edit(window, password_id, ES_PASSWORD);
        state->pin = create_edit(window, pin_id, ES_PASSWORD | ES_NUMBER);
        if (state->edit_a == nullptr || state->edit_b == nullptr ||
            state->password == nullptr || state->pin == nullptr) {
            return -1;
        }
        SendMessageW(state->password, EM_SETPASSWORDCHAR, 0x25CFU, 0L);
        SendMessageW(state->pin, EM_SETPASSWORDCHAR, 0x25CFU, 0L);
        if (!set_input_scope(state->password, IS_PASSWORD) ||
            !set_input_scope(state->pin, IS_NUMERIC_PIN)) {
            return -1;
        }
        layout_controls(window, *state);
        return 0;
    case WM_SIZE:
        if (state != nullptr) layout_controls(window, *state);
        return 0;
    case recreate_edit_b_message:
        if (state == nullptr) return FALSE;
        if (state->edit_b != nullptr) DestroyWindow(state->edit_b);
        state->edit_b = create_edit(window, edit_b_id, 0U);
        layout_controls(window, *state);
        return state->edit_b != nullptr ? TRUE : FALSE;
    case clear_all_message:
        if (state == nullptr) return FALSE;
        clear_control(state->edit_a);
        clear_control(state->edit_b);
        clear_control(state->password);
        clear_control(state->pin);
        return TRUE;
    case WM_SETFOCUS:
        if (state != nullptr && state->edit_a != nullptr) SetFocus(state->edit_a);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

[[nodiscard]] bool register_window_class() noexcept {
    WNDCLASSEXW definition{};
    definition.cbSize = sizeof(definition);
    definition.style = CS_HREDRAW | CS_VREDRAW;
    definition.lpfnWndProc = window_proc;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    definition.lpszClassName = window_class_name;
    return RegisterClassExW(&definition) != 0U || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

[[nodiscard]] HWND create_host_window(HostState& state, const bool visible) noexcept {
    const DWORD style = visible ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    return CreateWindowExW(
        0U,
        window_class_name,
        window_title,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        240,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
}

[[nodiscard]] std::wstring control_text(const HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length < 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return text;
}

[[nodiscard]] bool has_style(const HWND control, const LONG_PTR style) noexcept {
    return (GetWindowLongPtrW(control, GWL_STYLE) & style) == style;
}

[[nodiscard]] int run_self_test() {
    HostState state;
    const HWND window = create_host_window(state, false);
    if (window == nullptr) {
        std::cout << "{\"pass\":false,\"failure\":\"create_window\"}\n";
        return 1;
    }

    SetWindowTextW(state.edit_a, L"普通输入A");
    SetWindowTextW(state.edit_b, L"ordinary-b");
    SetWindowTextW(state.password, L"secret");
    SetWindowTextW(state.pin, L"1234");
    SendMessageW(state.edit_a, EM_SETSEL, 2U, 4L);
    DWORD selection_start = 0U;
    DWORD selection_end = 0U;
    SendMessageW(state.edit_a, EM_GETSEL,
        reinterpret_cast<WPARAM>(&selection_start),
        reinterpret_cast<LPARAM>(&selection_end));

    const bool fields_ok = control_text(state.edit_a) == L"普通输入A" &&
        control_text(state.edit_b) == L"ordinary-b" &&
        control_text(state.password) == L"secret" &&
        control_text(state.pin) == L"1234";
    const bool scopes_ok = has_style(state.password, ES_PASSWORD) &&
        has_style(state.pin, ES_PASSWORD | ES_NUMBER);
    const bool selection_ok = selection_start == 2U && selection_end == 4U;
    const bool recreated = SendMessageW(window, recreate_edit_b_message, 0U, 0L) == TRUE &&
        state.edit_b != nullptr && control_text(state.edit_b).empty();
    const bool cleared = SendMessageW(window, clear_all_message, 0U, 0L) == TRUE &&
        control_text(state.edit_a).empty() && control_text(state.edit_b).empty() &&
        control_text(state.password).empty() && control_text(state.pin).empty();

    DestroyWindow(window);
    const bool passed = fields_ok && scopes_ok && selection_ok && recreated && cleared;
    std::cout << "{\"pass\":" << (passed ? "true" : "false")
              << ",\"fields\":" << (fields_ok ? "true" : "false")
              << ",\"sensitive_styles\":" << (scopes_ok ? "true" : "false")
              << ",\"selection\":" << (selection_ok ? "true" : "false")
              << ",\"context_recreate\":" << (recreated ? "true" : "false")
              << ",\"clear\":" << (cleared ? "true" : "false") << "}\n";
    return passed ? 0 : 1;
}

[[nodiscard]] bool has_argument(
    const int argc,
    wchar_t** const argv,
    const std::wstring_view expected) noexcept {
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && expected == argv[index]) return true;
    }
    return false;
}

}  // namespace

int wmain(const int argc, wchar_t** const argv) {
    if (!register_window_class()) return 2;
    if (has_argument(argc, argv, L"--self-test")) return run_self_test();

    HostState state;
    const HWND window = create_host_window(state, true);
    if (window == nullptr) return 3;
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
