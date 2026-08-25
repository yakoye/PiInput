#include <windows.h>
#include <msctf.h>
#include <objbase.h>
#include <tlhelp32.h>

#include "controlled_tsf_host_protocol.h"
#include "profile_registration.h"

#include <array>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace piinput::tests::controlled_tsf;

struct ChildProcess final {
    PROCESS_INFORMATION information{};

    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess() {
        if (information.hThread != nullptr) CloseHandle(information.hThread);
        if (information.hProcess != nullptr) CloseHandle(information.hProcess);
    }
};

struct ComApartment final {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    ~ComApartment() {
        if (SUCCEEDED(result)) CoUninitialize();
    }
};

struct ProfileActivationGuard final {
    ITfInputProcessorProfileMgr* manager{};
    TF_INPUTPROCESSORPROFILE previous{};
    bool has_previous{};
    bool activated{};

    ProfileActivationGuard() = default;
    ProfileActivationGuard(const ProfileActivationGuard&) = delete;
    ProfileActivationGuard& operator=(const ProfileActivationGuard&) = delete;
    ~ProfileActivationGuard() {
        if (manager != nullptr) {
            if (activated && has_previous) {
                (void)manager->ActivateProfile(
                    previous.dwProfileType,
                    previous.langid,
                    previous.clsid,
                    previous.guidProfile,
                    previous.hkl,
                    TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
            }
            manager->Release();
        }
    }

    [[nodiscard]] HRESULT activate_piinput() noexcept {
        HRESULT result = piinput::windows::tsf::create_profile_manager(&manager);
        if (FAILED(result)) return result;
        has_previous = SUCCEEDED(manager->GetActiveProfile(
            GUID_TFCAT_TIP_KEYBOARD, &previous));
        result = piinput::windows::tsf::activate_profile();
        activated = SUCCEEDED(result);
        return result;
    }
};

[[nodiscard]] std::filesystem::path sibling_host_path() {
    std::wstring module(32768U, L'\0');
    const DWORD copied = GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (copied == 0U || copied >= module.size()) return {};
    module.resize(copied);
    return std::filesystem::path(module).parent_path() /
        L"piinput-controlled-tsf-host.exe";
}

[[nodiscard]] bool launch_host(
    const std::filesystem::path& executable,
    ChildProcess& child) {
    std::wstring command = L"\"" + executable.native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    return CreateProcessW(
        executable.c_str(),
        command.data(),
        nullptr,
        nullptr,
        FALSE,
        0U,
        nullptr,
        executable.parent_path().c_str(),
        &startup,
        &child.information) != FALSE;
}

[[nodiscard]] HWND wait_for_host_window(const DWORD process_id) noexcept {
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
        const HWND window = FindWindowW(window_class_name, window_title);
        if (window != nullptr) {
            DWORD owner = 0U;
            GetWindowThreadProcessId(window, &owner);
            if (owner == process_id) return window;
        }
        Sleep(50U);
    }
    return nullptr;
}

[[nodiscard]] bool focus_control(const HWND window, const HWND control) noexcept {
    if (window == nullptr || control == nullptr) return false;
    DWORD ignored = 0U;
    const DWORD target_thread = GetWindowThreadProcessId(window, &ignored);
    const DWORD current_thread = GetCurrentThreadId();
    const bool attached = target_thread != current_thread &&
        AttachThreadInput(current_thread, target_thread, TRUE) != FALSE;
    ShowWindow(window, SW_RESTORE);
    (void)SetForegroundWindow(window);
    (void)SetFocus(control);
    const bool focused = GetFocus() == control;
    if (attached) AttachThreadInput(current_thread, target_thread, FALSE);
    return focused;
}

[[nodiscard]] bool select_us_layout(const HWND window) noexcept {
    const HKL layout = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    if (layout == nullptr) return false;
    if (!PostMessageW(window, WM_INPUTLANGCHANGEREQUEST,
            INPUTLANGCHANGE_SYSCHARSET, reinterpret_cast<LPARAM>(layout))) {
        return false;
    }
    DWORD ignored = 0U;
    const DWORD thread = GetWindowThreadProcessId(window, &ignored);
    for (unsigned attempt = 0U; attempt < 40U; ++attempt) {
        if (LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(thread))) == 0x0409U) {
            return true;
        }
        Sleep(25U);
    }
    return false;
}

struct PhysicalKey final {
    WORD virtual_key{};
    bool shift{};
};

[[nodiscard]] bool send_physical_key(
    const WORD virtual_key,
    const bool shift = false) noexcept {
    const UINT scan = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    if (scan == 0U) return false;
    std::array<INPUT, 4U> inputs{};
    UINT count = 0U;
    if (shift) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC));
        inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;
        ++count;
    }
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wScan = static_cast<WORD>(scan);
    inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;
    ++count;
    inputs[count] = inputs[count - 1U];
    inputs[count].ki.dwFlags |= KEYEVENTF_KEYUP;
    ++count;
    if (shift) {
        inputs[count] = inputs[0];
        inputs[count].ki.dwFlags |= KEYEVENTF_KEYUP;
        ++count;
    }
    return SendInput(count, inputs.data(), sizeof(INPUT)) == count;
}

template <std::size_t Size>
[[nodiscard]] bool send_sequence(const std::array<WORD, Size>& keys) noexcept {
    for (const WORD key : keys) {
        if (!send_physical_key(key)) return false;
    }
    return true;
}

[[nodiscard]] bool send_sequence(const std::vector<PhysicalKey>& keys) noexcept {
    for (const PhysicalKey key : keys) {
        if (!send_physical_key(key.virtual_key, key.shift)) return false;
    }
    return true;
}

[[nodiscard]] std::wstring control_text(const HWND control) {
    const LRESULT length = SendMessageW(control, WM_GETTEXTLENGTH, 0U, 0L);
    if (length < 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    const LRESULT copied = SendMessageW(control, WM_GETTEXT,
        static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0U);
    return text;
}

[[nodiscard]] bool wait_for_text(
    const HWND control,
    const std::wstring_view expected,
    const DWORD timeout_ms = 3000U) {
    const DWORD started = GetTickCount();
    do {
        if (control_text(control) == expected) return true;
        Sleep(20U);
    } while (GetTickCount() - started < timeout_ms);
    return control_text(control) == expected;
}

struct CandidateSearch final {
    HWND owner{};
    HWND found{};
};

BOOL CALLBACK find_owned_candidate(const HWND candidate, const LPARAM parameter) {
    auto* search = reinterpret_cast<CandidateSearch*>(parameter);
    if (search == nullptr || GetWindow(candidate, GW_OWNER) != search->owner ||
        IsWindowVisible(candidate) == FALSE) {
        return TRUE;
    }
    std::array<wchar_t, 64U> class_name{};
    if (GetClassNameW(candidate, class_name.data(), static_cast<int>(class_name.size())) > 0 &&
        std::wstring_view(class_name.data()) == L"PiInputTsfCandidateWindow") {
        search->found = candidate;
        return FALSE;
    }
    return TRUE;
}

[[nodiscard]] HWND wait_for_owned_candidate(
    const HWND owner,
    const DWORD timeout_ms = 3000U) noexcept {
    const DWORD started = GetTickCount();
    do {
        CandidateSearch search{.owner = owner};
        EnumWindows(find_owned_candidate, reinterpret_cast<LPARAM>(&search));
        if (search.found != nullptr) return search.found;
        Sleep(20U);
    } while (GetTickCount() - started < timeout_ms);
    return nullptr;
}

[[nodiscard]] bool prepare_case(
    const HWND window,
    const HWND control,
    const std::wstring_view text,
    const DWORD caret) {
    if (!focus_control(window, control)) return false;
    if (!SendMessageW(window, clear_all_message, 0U, 0L)) return false;
    if (!SetWindowTextW(control, std::wstring(text).c_str())) return false;
    SendMessageW(control, EM_SETSEL, caret, caret);
    return true;
}

[[nodiscard]] bool run_text_case(
    const HWND window,
    const HWND control,
    const std::wstring_view initial,
    const DWORD caret,
    const std::vector<PhysicalKey>& keys,
    const std::wstring_view expected) {
    if (!prepare_case(window, control, initial, caret) || !send_sequence(keys)) return false;
    return wait_for_text(control, expected);
}

[[nodiscard]] bool ensure_chinese_mode(
    const HWND window,
    const HWND control) {
    const auto probe = [&]() {
        if (!prepare_case(window, control, L"你去吗", 3U) ||
            !send_physical_key(VK_OEM_2, true)) {
            return false;
        }
        return wait_for_text(control, L"你去吗？", 500U);
    };
    if (probe()) return true;
    if (!send_physical_key(VK_SHIFT)) return false;
    Sleep(100U);
    return probe();
}

[[nodiscard]] std::filesystem::path loaded_tsf_module(const DWORD process_id) {
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
        const HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Module32FirstW(snapshot, &entry)) {
                do {
                    if (_wcsicmp(entry.szModule, L"PiInputTSF.dll") == 0) {
                        const std::filesystem::path result(entry.szExePath);
                        CloseHandle(snapshot);
                        return result;
                    }
                } while (Module32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
        Sleep(30U);
    }
    return {};
}

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    if (left.empty() || right.empty()) return false;
    const std::wstring left_text = std::filesystem::weakly_canonical(left).native();
    const std::wstring right_text = std::filesystem::weakly_canonical(right).native();
    return _wcsicmp(left_text.c_str(), right_text.c_str()) == 0;
}

[[nodiscard]] std::filesystem::path argument_path(
    const int argc,
    wchar_t** const argv,
    const std::wstring_view name) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (name == argv[index]) return argv[index + 1];
    }
    return {};
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

[[nodiscard]] unsigned argument_unsigned(
    const int argc,
    wchar_t** const argv,
    const std::wstring_view name) noexcept {
    for (int index = 1; index + 1 < argc; ++index) {
        if (name != argv[index] || argv[index + 1] == nullptr) continue;
        wchar_t* end = nullptr;
        const unsigned long value = std::wcstoul(argv[index + 1], &end, 10);
        if (end != argv[index + 1] && end != nullptr && *end == L'\0') {
            return static_cast<unsigned>(value);
        }
    }
    return 0U;
}

[[nodiscard]] bool write_soak_status(
    const std::filesystem::path& path,
    const DWORD app_process_id,
    const unsigned iterations,
    const unsigned context_recreates,
    const bool passed,
    const bool completed) {
    if (path.empty()) return true;
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << "{\"app_pid\":" << app_process_id
               << ",\"controller_pid\":" << GetCurrentProcessId()
               << ",\"iterations\":" << iterations
               << ",\"context_recreates\":" << context_recreates
               << ",\"pass\":" << (passed ? "true" : "false")
               << ",\"completed\":" << (completed ? "true" : "false")
               << "}\n";
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void close_child(const HWND window, ChildProcess& child) noexcept {
    if (window != nullptr) PostMessageW(window, WM_CLOSE, 0U, 0L);
    if (child.information.hProcess == nullptr) return;
    if (WaitForSingleObject(child.information.hProcess, 3000U) == WAIT_TIMEOUT) {
        TerminateProcess(child.information.hProcess, 4U);
        WaitForSingleObject(child.information.hProcess, 1000U);
    }
}

}  // namespace

int wmain(const int argc, wchar_t** const argv) {
    const bool piinput_smoke = has_argument(argc, argv, L"--piinput-smoke");
    const std::filesystem::path expected_tsf = argument_path(argc, argv, L"--expected-tsf");
    const unsigned soak_seconds = argument_unsigned(argc, argv, L"--soak-seconds");
    const std::filesystem::path status_file = argument_path(argc, argv, L"--status-file");
    ComApartment com;
    ProfileActivationGuard profile;
    if (piinput_smoke) {
        if (expected_tsf.empty()) {
            std::cout << "{\"pass\":false,\"failure\":\"expected_tsf_required\"}\n";
            return 2;
        }
        if (FAILED(com.result) || FAILED(profile.activate_piinput())) {
            std::cout << "{\"pass\":false,\"failure\":\"activate_piinput\"}\n";
            return 3;
        }
    }

    const std::filesystem::path host = sibling_host_path();
    ChildProcess child;
    if (host.empty() || !std::filesystem::is_regular_file(host) || !launch_host(host, child)) {
        std::cout << "{\"pass\":false,\"failure\":\"launch_host\"}\n";
        return 1;
    }
    WaitForInputIdle(child.information.hProcess, 5000U);
    const HWND window = wait_for_host_window(child.information.dwProcessId);
    const HWND edit_a = window == nullptr ? nullptr : GetDlgItem(window, edit_a_id);
    HWND edit_b = window == nullptr ? nullptr : GetDlgItem(window, edit_b_id);
    const HWND password = window == nullptr ? nullptr : GetDlgItem(window, password_id);
    const HWND pin = window == nullptr ? nullptr : GetDlgItem(window, pin_id);

    const bool controls = edit_a != nullptr && edit_b != nullptr &&
        password != nullptr && pin != nullptr;
    unsigned context_recreates = 0U;
    if (!write_soak_status(
            status_file, child.information.dwProcessId, 0U, context_recreates,
            controls, false)) {
        close_child(window, child);
        std::cout << "{\"pass\":false,\"failure\":\"status_file\"}\n";
        return 4;
    }
    if (piinput_smoke) {
        const std::wstring list_initial = L"1";
        const bool list_dot = controls && run_text_case(
            window, edit_a, list_initial, static_cast<DWORD>(list_initial.size()),
            {{VK_OEM_PERIOD, false}}, L"1.");

        const std::filesystem::path loaded = loaded_tsf_module(child.information.dwProcessId);
        const bool module_identity = same_path(loaded, expected_tsf);
        const bool chinese_mode = module_identity && ensure_chinese_mode(window, edit_a);
        const bool candidate_owned = chinese_mode &&
            prepare_case(window, edit_a, {}, 0U) &&
            send_sequence(std::array<WORD, 2U>{'N', 'I'}) &&
            wait_for_owned_candidate(window) != nullptr;
        if (candidate_owned) {
            (void)send_physical_key(VK_ESCAPE);
            (void)wait_for_text(edit_a, {}, 500U);
        }

        const std::wstring decimal_initial = L"314";
        const bool decimal = candidate_owned && run_text_case(
            window, edit_a, decimal_initial, 1U,
            {{VK_OEM_PERIOD, false}}, L"3.14");

        const std::wstring time_initial = L"12";
        const bool time = decimal && run_text_case(
            window, edit_a, time_initial, static_cast<DWORD>(time_initial.size()),
            {{VK_OEM_1, true}, {'2', false}, {'3', false}}, L"12:23");

        const std::wstring version_initial = L"版本是 v1.0.1";
        const bool version_sentence = time && run_text_case(
            window, edit_a, version_initial, static_cast<DWORD>(version_initial.size()),
            {{VK_OEM_PERIOD, false}, {VK_SPACE, false}}, L"版本是 v1.0.1。 ");

        const std::wstring grouped_initial = L"1";
        const bool grouped = version_sentence && run_text_case(
            window, edit_a, grouped_initial, static_cast<DWORD>(grouped_initial.size()),
            {{VK_OEM_COMMA, false}, {'2', false}, {'9', false}, {'9', false},
             {VK_OEM_PERIOD, false}, {'5', false}, {'0', false}},
            L"1,299.50");

        const std::wstring incomplete_group_initial = L"1";
        const bool incomplete_group = grouped && run_text_case(
            window, edit_a, incomplete_group_initial,
            static_cast<DWORD>(incomplete_group_initial.size()),
            {{VK_OEM_COMMA, false}, {'2', false}, {VK_SPACE, false}}, L"1，2 ");

        const bool grouped_backspace = incomplete_group && run_text_case(
            window, edit_a, L"1", 1U,
            {{VK_OEM_COMMA, false}, {'2', false}, {'9', false}, {VK_BACK, false},
             {'9', false}, {'9', false}}, L"1,299");
        const bool grouped_escape = grouped_backspace && run_text_case(
            window, edit_a, L"1", 1U,
            {{VK_OEM_COMMA, false}, {'2', false}, {'9', false}, {VK_ESCAPE, false}},
            L"129");

        const std::wstring invalid_group_initial = L"12";
        const bool invalid_group = grouped_escape && run_text_case(
            window, edit_a, invalid_group_initial, 1U,
            {{VK_OEM_COMMA, false}}, L"1，2");

        const std::wstring filename_initial = L"READMEmd";
        const bool filename = invalid_group && run_text_case(
            window, edit_a, filename_initial, 6U,
            {{VK_OEM_PERIOD, false}}, L"README.md");

        const std::wstring url_initial = L"https://example.com";
        const bool url_question = filename && run_text_case(
            window, edit_a, url_initial, static_cast<DWORD>(url_initial.size()),
            {{VK_OEM_2, true}}, L"https://example.com?");

        const std::wstring chinese_initial = L"你去吗";
        const bool chinese_question = url_question && run_text_case(
            window, edit_a, chinese_initial, static_cast<DWORD>(chinese_initial.size()),
            {{VK_OEM_2, true}}, L"你去吗？");

        const bool chinese_after_digit = chinese_question && run_text_case(
            window, edit_a, L"版本1", 3U,
            {{VK_OEM_2, true}}, L"版本1？");

        const std::wstring url_exit_initial = L"https://example.com 你去吗";
        const bool token_exit_url = chinese_after_digit && run_text_case(
            window, edit_a, url_exit_initial,
            static_cast<DWORD>(url_exit_initial.size()),
            {{VK_OEM_2, true}}, L"https://example.com 你去吗？");
        const std::wstring email_exit_initial = L"联系abc@example.com 已完成";
        const bool token_exit_email = token_exit_url && run_text_case(
            window, edit_a, email_exit_initial,
            static_cast<DWORD>(email_exit_initial.size()),
            {{VK_OEM_PERIOD, false}}, L"联系abc@example.com 已完成。");
        const std::wstring path_exit_initial = L"路径C:/folder/file.txt 已完成";
        const bool token_exit_path = token_exit_email && run_text_case(
            window, edit_a, path_exit_initial,
            static_cast<DWORD>(path_exit_initial.size()),
            {{VK_OEM_PERIOD, false}}, L"路径C:/folder/file.txt 已完成。");
        const std::wstring code_exit_initial = L"func(x) 已完成";
        const bool token_exit_code = token_exit_path && run_text_case(
            window, edit_a, code_exit_initial,
            static_cast<DWORD>(code_exit_initial.size()),
            {{'1', true}}, L"func(x) 已完成！");
        const std::wstring percent_initial = L"折扣80";
        const bool percent_then_chinese = token_exit_code && run_text_case(
            window, edit_a, percent_initial,
            static_cast<DWORD>(percent_initial.size()),
            {{'5', true}, {VK_OEM_COMMA, false}}, L"折扣80%，");

        const bool technical_symbols = percent_then_chinese && run_text_case(
            window, edit_a, L"BIT", 3U,
            {{VK_OEM_4, false}, {'3', false}, {'1', false},
             {VK_OEM_1, true}, {'1', false}, {'6', false}, {VK_OEM_6, false}},
            L"BIT[31:16]");

        const bool technical_bang = technical_symbols && run_text_case(
            window, edit_a, L"flag", 0U,
            {{'1', true}}, L"!flag");
        const bool technical_open_quote = technical_bang && run_text_case(
            window, edit_a, L"key=value", 4U,
            {{VK_OEM_7, true}}, L"key=\"value");
        const bool technical_close_quote = technical_open_quote && run_text_case(
            window, edit_a, L"key=\"value", 10U,
            {{VK_OEM_7, true}}, L"key=\"value\"");

        const bool cross_context = technical_close_quote &&
            prepare_case(window, edit_a, L"1", 1U) &&
            send_sequence(std::vector<PhysicalKey>{{VK_OEM_COMMA, false}, {'2', false}}) &&
            focus_control(window, edit_b) && send_physical_key('3') &&
            wait_for_text(edit_a, L"1，2") && wait_for_text(edit_b, L"3");

        const bool prepared_recreate = cross_context &&
            prepare_case(window, edit_b, L"1", 1U) &&
            send_sequence(std::vector<PhysicalKey>{{VK_OEM_COMMA, false}, {'2', false}});
        const bool recreated_during_provisional = prepared_recreate &&
            SendMessageW(window, recreate_edit_b_message, 0U, 0L) == TRUE;
        if (recreated_during_provisional) ++context_recreates;
        edit_b = window == nullptr ? nullptr : GetDlgItem(window, edit_b_id);
        const bool context_destroy = recreated_during_provisional && edit_b != nullptr &&
            focus_control(window, edit_b) && send_physical_key('3') &&
            wait_for_text(edit_b, L"3") && control_text(edit_a).empty();

        const bool password_bypass = context_destroy && run_text_case(
            window, password, {}, 0U,
            {{'A', false}, {'B', false}, {'C', false},
             {VK_OEM_PERIOD, false}, {'1', false}, {'2', false}},
            L"abc.12");
        const bool pin_bypass = password_bypass && run_text_case(
            window, pin, {}, 0U,
            {{'1', false}, {'2', false}, {VK_OEM_1, true}, {'2', false}, {'3', false}},
            L"12:23");

        bool passed = controls && list_dot && module_identity && chinese_mode && candidate_owned &&
            decimal && time &&
            version_sentence && grouped && incomplete_group && grouped_backspace &&
            grouped_escape && invalid_group && filename &&
            url_question && chinese_question && technical_symbols && technical_bang &&
            technical_open_quote && technical_close_quote && cross_context && context_destroy &&
            password_bypass && pin_bypass;
        unsigned soak_iterations = 0U;
        if (passed && soak_seconds != 0U) {
            const ULONGLONG deadline = GetTickCount64() +
                static_cast<ULONGLONG>(soak_seconds) * 1000ULL;
            while (GetTickCount64() < deadline) {
                ++soak_iterations;
                const bool numeric_cycle = run_text_case(
                    window, edit_a, L"1", 1U,
                    {{VK_OEM_COMMA, false}, {'2', false}, {'9', false}, {'9', false},
                     {VK_OEM_PERIOD, false}, {'5', false}, {'0', false}},
                    L"1,299.50");
                const bool technical_cycle = numeric_cycle && run_text_case(
                    window, edit_a, L"BIT", 3U,
                    {{VK_OEM_4, false}, {'3', false}, {'1', false},
                     {VK_OEM_1, true}, {'1', false}, {'6', false}, {VK_OEM_6, false}},
                    L"BIT[31:16]");
                const bool scope_cycle = technical_cycle && run_text_case(
                    window, password, {}, 0U,
                    {{'A', false}, {'B', false}, {'C', false},
                     {VK_OEM_PERIOD, false}, {'1', false}, {'2', false}},
                    L"abc.12");
                bool lifecycle_cycle = scope_cycle;
                if (lifecycle_cycle && (soak_iterations % 20U) == 0U) {
                    const bool recreated_in_cycle = prepare_case(window, edit_b, L"1", 1U) &&
                        send_sequence(std::vector<PhysicalKey>{
                            {VK_OEM_COMMA, false}, {'2', false}}) &&
                        SendMessageW(window, recreate_edit_b_message, 0U, 0L) == TRUE;
                    if (recreated_in_cycle) ++context_recreates;
                    lifecycle_cycle = recreated_in_cycle;
                    edit_b = window == nullptr ? nullptr : GetDlgItem(window, edit_b_id);
                    lifecycle_cycle = lifecycle_cycle && edit_b != nullptr &&
                        focus_control(window, edit_b) && send_physical_key('3') &&
                        wait_for_text(edit_b, L"3");
                }
                passed = lifecycle_cycle;
                if (!write_soak_status(status_file, child.information.dwProcessId,
                        soak_iterations, context_recreates, passed, false) || !passed) {
                    passed = false;
                    break;
                }
                Sleep(10U);
            }
        }
        (void)write_soak_status(status_file, child.information.dwProcessId,
            soak_iterations, context_recreates, passed, true);
        close_child(window, child);
        std::cout << "{\"pass\":" << (passed ? "true" : "false")
                  << ",\"module_identity\":" << (module_identity ? "true" : "false")
                  << ",\"chinese_mode\":" << (chinese_mode ? "true" : "false")
                  << ",\"candidate_owned\":" << (candidate_owned ? "true" : "false")
                  << ",\"list_dot\":" << (list_dot ? "true" : "false")
                  << ",\"decimal\":" << (decimal ? "true" : "false")
                  << ",\"time\":" << (time ? "true" : "false")
                  << ",\"version_sentence\":" << (version_sentence ? "true" : "false")
                  << ",\"grouped\":" << (grouped ? "true" : "false")
                  << ",\"incomplete_group\":" << (incomplete_group ? "true" : "false")
                  << ",\"grouped_backspace\":" << (grouped_backspace ? "true" : "false")
                  << ",\"grouped_escape\":" << (grouped_escape ? "true" : "false")
                  << ",\"invalid_group\":" << (invalid_group ? "true" : "false")
                  << ",\"filename\":" << (filename ? "true" : "false")
                  << ",\"url_question\":" << (url_question ? "true" : "false")
                  << ",\"chinese_question\":" << (chinese_question ? "true" : "false")
                  << ",\"chinese_after_digit\":" << (chinese_after_digit ? "true" : "false")
                  << ",\"token_exit_url\":" << (token_exit_url ? "true" : "false")
                  << ",\"token_exit_email\":" << (token_exit_email ? "true" : "false")
                  << ",\"token_exit_path\":" << (token_exit_path ? "true" : "false")
                  << ",\"token_exit_code\":" << (token_exit_code ? "true" : "false")
                  << ",\"percent_then_chinese\":" << (percent_then_chinese ? "true" : "false")
                  << ",\"technical_symbols\":" << (technical_symbols ? "true" : "false")
                  << ",\"technical_bang\":" << (technical_bang ? "true" : "false")
                  << ",\"technical_open_quote\":" << (technical_open_quote ? "true" : "false")
                  << ",\"technical_close_quote\":" << (technical_close_quote ? "true" : "false")
                  << ",\"cross_context\":" << (cross_context ? "true" : "false")
                  << ",\"context_destroy\":" << (context_destroy ? "true" : "false")
                  << ",\"password_bypass\":" << (password_bypass ? "true" : "false")
                  << ",\"pin_bypass\":" << (pin_bypass ? "true" : "false")
                  << ",\"soak_iterations\":" << soak_iterations
                  << ",\"context_recreates\":" << context_recreates
                  << "}\n";
        return passed ? 0 : 1;
    }

    const bool layout = controls && select_us_layout(window);
    const bool focus_a = layout && focus_control(window, edit_a);
    const bool sent_a = focus_a && send_sequence(std::array<WORD, 3U>{'A', 'B', 'C'});
    Sleep(100U);
    const bool text_a = sent_a && control_text(edit_a) == L"abc";

    const bool focus_b = text_a && focus_control(window, edit_b);
    const bool sent_b = focus_b && send_sequence(std::array<WORD, 3U>{'1', '2', '3'});
    Sleep(100U);
    const bool isolated = sent_b && control_text(edit_a) == L"abc" &&
        control_text(edit_b) == L"123";

    const bool recreated = isolated &&
        SendMessageW(window, recreate_edit_b_message, 0U, 0L) == TRUE;
    if (recreated) ++context_recreates;
    edit_b = window == nullptr ? nullptr : GetDlgItem(window, edit_b_id);
    const bool recreate_empty = recreated && edit_b != nullptr && control_text(edit_b).empty();
    const bool cleared = recreate_empty &&
        SendMessageW(window, clear_all_message, 0U, 0L) == TRUE &&
        control_text(edit_a).empty() && control_text(edit_b).empty();

    bool passed = controls && layout && focus_a && text_a && focus_b &&
        isolated && recreate_empty && cleared;
    unsigned soak_iterations = 0U;
    if (passed && soak_seconds != 0U) {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(soak_seconds) * 1000ULL;
        while (GetTickCount64() < deadline) {
            ++soak_iterations;
            const bool cycle_a = prepare_case(window, edit_a, {}, 0U) &&
                send_sequence(std::array<WORD, 3U>{'A', 'B', 'C'}) &&
                wait_for_text(edit_a, L"abc");
            const bool cycle_b = cycle_a && focus_control(window, edit_b) &&
                send_sequence(std::array<WORD, 3U>{'1', '2', '3'}) &&
                wait_for_text(edit_b, L"123");
            bool lifecycle_cycle = cycle_b;
            if (lifecycle_cycle && (soak_iterations % 20U) == 0U) {
                lifecycle_cycle = SendMessageW(
                    window, recreate_edit_b_message, 0U, 0L) == TRUE;
                if (lifecycle_cycle) ++context_recreates;
                edit_b = window == nullptr ? nullptr : GetDlgItem(window, edit_b_id);
                lifecycle_cycle = lifecycle_cycle && edit_b != nullptr;
            }
            passed = lifecycle_cycle;
            if (!write_soak_status(status_file, child.information.dwProcessId,
                    soak_iterations, context_recreates, passed, false) || !passed) {
                passed = false;
                break;
            }
            Sleep(10U);
        }
    }
    (void)write_soak_status(status_file, child.information.dwProcessId,
        soak_iterations, context_recreates, passed, true);
    close_child(window, child);
    std::cout << "{\"pass\":" << (passed ? "true" : "false")
              << ",\"controls\":" << (controls ? "true" : "false")
              << ",\"layout\":" << (layout ? "true" : "false")
              << ",\"physical_keys\":" << (text_a ? "true" : "false")
              << ",\"focus_isolation\":" << (isolated ? "true" : "false")
              << ",\"context_recreate\":" << (recreate_empty ? "true" : "false")
              << ",\"clear\":" << (cleared ? "true" : "false")
              << ",\"soak_iterations\":" << soak_iterations
              << ",\"context_recreates\":" << context_recreates << "}\n";
    return passed ? 0 : 1;
}
