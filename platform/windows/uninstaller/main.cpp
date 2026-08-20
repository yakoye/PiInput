#include "migration.h"
#include "uninstall_layout.h"

#include "piinput/windows_compat.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using piinput::windows::installer::UninstallLayout;
using piinput::windows::installer::make_uninstall_layout;
using piinput::windows::installer::quote_windows_argument;
using piinput::windows::installer::remove_or_schedule_legacy_runtime;
using piinput::windows::installer::resolve_active_version;
using piinput::windows::installer::uninstall_roots;
using piinput::windows::installer::validate_uninstall_layout;

struct Arguments {
    bool silent{};
    bool remove_user_data{};
    bool worker{};
    DWORD wait_pid{};
};

[[nodiscard]] std::filesystem::path known_folder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        throw std::runtime_error("Cannot locate a required Windows user directory");
    }
    const std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

[[nodiscard]] std::filesystem::path executable_path() {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        throw std::runtime_error("Cannot locate PiInput-Uninstall.exe");
    }
    buffer.resize(length);
    return buffer;
}

[[nodiscard]] Arguments parse_arguments() {
    Arguments result;
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (values == nullptr) {
        throw std::runtime_error("Cannot parse the uninstaller command line");
    }
    for (int index = 1; index < count; ++index) {
        const std::wstring_view current(values[index]);
        if (current == L"--silent") {
            result.silent = true;
        } else if (current == L"--remove-user-data") {
            result.remove_user_data = true;
        } else if (current == L"--worker") {
            result.worker = true;
        } else if (current == L"--wait-pid" && index + 1 < count) {
            try {
                result.wait_pid = static_cast<DWORD>(std::stoul(values[++index]));
            } catch (...) {
                LocalFree(values);
                throw std::runtime_error("Invalid --wait-pid value");
            }
        }
    }
    LocalFree(values);
    return result;
}

[[nodiscard]] DWORD run_hidden(
    const std::filesystem::path& program,
    const std::wstring_view arguments) {
    std::wstring command = quote_windows_argument(program.wstring()) + L" " + std::wstring(arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, program.parent_path().c_str(), &startup, &process) == FALSE) {
        return GetLastError();
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

void wait_for_parent(const DWORD process_id) {
    if (process_id == 0U) {
        return;
    }
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (process != nullptr) {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }
}

// Unregistering still happens before anything is deleted, so a live TSF
// profile is never left pointing at files that are already gone. What changed
// is the response to failure: every step reports and continues. Refusing to
// clean up because a program is missing left users with an installation they
// could not remove by any means -- and a missing program is usually the reason
// there is nothing left to unregister.
[[nodiscard]] std::vector<std::wstring> unregister_tsf(
    const UninstallLayout& layout,
    const std::optional<std::filesystem::path>& active_version) {
    std::vector<std::wstring> problems;
    const auto tools = locate_uninstall_tools(layout, active_version);
    if (tools.host.has_value()) {
        (void)run_hidden(*tools.host, L"--drain");
    }
    if (tools.profile.has_value()) {
        (void)run_hidden(*tools.profile, L"--deactivate");
        if (run_hidden(*tools.profile, L"--disable-user") != 0U) {
            problems.emplace_back(L"未能从当前用户的键盘列表中移除 PiInput");
        }
    } else {
        problems.emplace_back(L"未找到 piinput-profile.exe，跳过键盘列表清理");
    }

    if (!tools.tsf_dll.has_value()) {
        problems.emplace_back(L"未找到 PiInputTSF.dll，跳过输入法组件反注册");
        return problems;
    }
    const HMODULE module = LoadLibraryExW(tools.tsf_dll->c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr) {
        problems.emplace_back(L"无法加载 PiInputTSF.dll 进行反注册");
        return problems;
    }
    using UnregisterServer = HRESULT(__stdcall*)();
    const auto function = reinterpret_cast<UnregisterServer>(
        GetProcAddress(module, "DllUnregisterServer"));
    const HRESULT result = function == nullptr
        ? HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)
        : function();  // DllUnregisterServer
    FreeLibrary(module);
    if (FAILED(result)) {
        problems.emplace_back(L"输入法组件反注册失败");
    }
    return problems;
}

// The COM registration the DLL writes about itself. When the DLL is already
// gone there is nobody left to call DllUnregisterServer, and without this the
// class registration would outlive the product forever.
// CLSID_PiInputTextService, the permanent stable Shim identity declared
// in platform/windows/tsf/piinput_tsf_guids.h.
void delete_com_registration() noexcept {
    static constexpr const wchar_t* keys[] = {
        L"Software\\Classes\\CLSID\\{13EB305F-2DA3-4CF7-8C45-16B016B801B5}",
        L"Software\\PiInput",
    };
    for (const auto* key : keys) {
        (void)RegDeleteTreeW(HKEY_CURRENT_USER, key);
    }
}

[[nodiscard]] bool delete_uninstall_registry() noexcept {
    constexpr wchar_t key[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PiInput";
    const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

void delete_runtime_registry() noexcept {
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\PiInput\\Runtime");
    HKEY run = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0U, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        (void)RegDeleteValueW(run, L"PiInputHost");
        RegCloseKey(run);
    }
}

// Everything that could not be cleaned up, in the user's language. An empty
// result means the machine no longer has PiInput on it.
[[nodiscard]] std::vector<std::wstring> run_worker(const Arguments& arguments) {
    wait_for_parent(arguments.wait_pid);
    const auto layout = make_uninstall_layout(
        known_folder(FOLDERID_LocalAppData),
        known_folder(FOLDERID_RoamingAppData));
    // The one thing still worth refusing over: a layout that does not describe
    // PiInput's own directories must never be handed to a recursive delete.
    if (!validate_uninstall_layout(layout)) {
        throw std::runtime_error("Unsafe PiInput uninstall paths");
    }

    // Unregister before deleting, so a live profile is never left pointing at
    // files that are already gone. Whatever fails here is reported, not fatal:
    // the files still have to go, or the user keeps an installation that no
    // uninstaller on the machine can remove.
    auto problems = unregister_tsf(layout, resolve_active_version(layout));

    std::error_code ignored;
    std::filesystem::remove_all(layout.start_menu_root, ignored);
    for (const auto& root : uninstall_roots(layout, arguments.remove_user_data)) {
        std::error_code error;
        if (!std::filesystem::exists(root, error)) {
            continue;
        }
        try {
            remove_or_schedule_legacy_runtime(root);
        } catch (const std::exception&) {
            problems.push_back(L"未能删除：" + root.wstring());
        }
    }
    if (!delete_uninstall_registry()) {
        problems.emplace_back(L"未能删除「应用和功能」中的卸载条目");
    }
    delete_runtime_registry();
    delete_com_registration();

    const auto temporary_worker = executable_path();
    (void)MoveFileExW(temporary_worker.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return problems;
}

[[nodiscard]] bool confirm_uninstall(bool& remove_user_data) {
    BOOL verification_checked = FALSE;
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = nullptr;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
    config.pszWindowTitle = L"卸载 PiInput";
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = L"确定要卸载 PiInput 吗？";
    config.pszContent =
        L"默认保留用户词库、设置和学习记录，重新安装后可以继续使用。";
    config.pszVerificationText = L"同时删除用户词库、设置和学习记录";
    config.nDefaultButton = IDNO;
    int button = IDNO;
    const HRESULT result = TaskDialogIndirect(&config, &button, nullptr, &verification_checked);
    if (FAILED(result)) {
        throw std::runtime_error("Cannot show the PiInput uninstall confirmation");
    }
    remove_user_data = verification_checked != FALSE;
    return button == IDYES;
}

void launch_worker(const Arguments& arguments) {
    const auto source = executable_path();
    const auto temp_root = std::filesystem::temp_directory_path() /
        (L"PiInput-Uninstall-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp_root);
    const auto worker = temp_root / L"PiInput-Uninstall.exe";
    if (CopyFileW(source.c_str(), worker.c_str(), FALSE) == FALSE) {  // CopyFileW
        throw std::runtime_error("Cannot prepare the temporary PiInput uninstall worker");
    }

    std::wstring parameters = L"--worker --wait-pid " + std::to_wstring(GetCurrentProcessId());
    if (arguments.remove_user_data) {
        parameters += L" --remove-user-data";
    }
    if (arguments.silent) {
        parameters += L" --silent";
    }
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = worker.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&execute) == FALSE) {
        throw std::runtime_error("Cannot start the PiInput uninstall worker");
    }
    if (execute.hProcess != nullptr) {
        CloseHandle(execute.hProcess);
    }
}

[[nodiscard]] std::wstring widen_error(const std::exception& error) {
    const std::string text(error.what());
    return std::wstring(text.begin(), text.end());
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        auto arguments = parse_arguments();
        if (arguments.worker) {
            const auto problems = run_worker(arguments);
            if (!arguments.silent) {
                if (problems.empty()) {
                    MessageBoxW(nullptr, L"PiInput 已卸载，相关文件和注册表项已清理。",
                        L"PiInput 卸载完成", MB_OK | MB_ICONINFORMATION);
                } else {
                    // Say what is left rather than calling the whole uninstall a
                    // failure. The product is gone either way; these are the
                    // leftovers, and most of them clear on the next restart.
                    std::wstring message =
                        L"PiInput 已卸载，但以下项目需要重启后才能清理干净：\n";
                    for (const auto& problem : problems) {
                        message += L"\n· " + problem;
                    }
                    MessageBoxW(nullptr, message.c_str(), L"PiInput 卸载完成",
                        MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }
        if (!arguments.silent && !confirm_uninstall(arguments.remove_user_data)) {
            return 0;
        }
        launch_worker(arguments);
        return 0;
    } catch (const std::exception& error) {
        const auto detail = widen_error(error);
        MessageBoxW(nullptr, (L"PiInput 卸载失败：\n" + detail).c_str(),
            L"PiInput 卸载失败", MB_OK | MB_ICONERROR);
        return 1;
    }
}
