#include "migration.h"
#include "machine_registration.h"
#include "uninstall_layout.h"
#include "stable_runtime.h"
#include "pipe_endpoint.h"
#include "profile_registration.h"
#include "user_keyboard_registration.h"

#include "piinput/host_protocol.h"
#include "piinput/windows_compat.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using piinput::windows::installer::UninstallLayout;
using piinput::windows::installer::make_uninstall_layout;
using piinput::windows::installer::remove_or_schedule_legacy_runtime;
using piinput::windows::installer::machine_runtime_root;
using piinput::windows::installer::is_safe_machine_runtime_root;
using piinput::windows::installer::uninstall_roots;
using piinput::windows::installer::validate_uninstall_layout;
using piinput::windows::tsf::deactivate_profile;
using piinput::windows::tsf::disable_user_keyboard;
using piinput::windows::tsf::unregister_machine_tsf;

constexpr UINT kForegroundMessageBox = MB_TOPMOST | MB_SETFOREGROUND;

class ScopedComApartment final {
public:
    ScopedComApartment() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ScopedComApartment() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }

    [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_;
};

struct Arguments {
    bool silent{};
    bool remove_user_data{};
    bool worker{};
    bool machine_unregister{};
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
        } else if (current == L"--machine-unregister") {
            result.machine_unregister = true;
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

// Ask the already-running Host to drain over its control pipe. Launching
// PiInputHost.exe from a temporary unsigned uninstall worker is exactly the
// child-process pattern Windows application control blocks. Speaking the
// existing protocol directly is both quieter and allows its mapped files to be
// removed without administrator-only delayed-delete registration.
void request_host_drain() noexcept {
    const auto endpoint = piinput::windows::current_host_endpoint_names();
    if (!endpoint.has_value()) return;

    const HANDLE host_mutex = OpenMutexW(SYNCHRONIZE, FALSE, endpoint->mutex.c_str());
    HANDLE pipe = CreateFileW(endpoint->pipe.c_str(), GENERIC_WRITE, 0U, nullptr,
        OPEN_EXISTING, 0U, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY &&
        WaitNamedPipeW(endpoint->pipe.c_str(), 500U) != FALSE) {
        pipe = CreateFileW(endpoint->pipe.c_str(), GENERIC_WRITE, 0U, nullptr,
            OPEN_EXISTING, 0U, nullptr);
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        try {
            const piinput::HostEnvelope request{
                .version = piinput::host_protocol_v1,
                .client_id = static_cast<std::uint64_t>(GetCurrentProcessId()),
                .session_id = 1U,
                .sequence = (std::max)(GetTickCount64(), 1ULL),
                .generation = 1U,
                .type = piinput::HostMessageType::drain,
            };
            const auto encoded = piinput::encode_host_envelope(request);
            DWORD written = 0U;
            (void)WriteFile(pipe, encoded.data(), static_cast<DWORD>(encoded.size()),
                &written, nullptr);
        } catch (...) {
        }
        CloseHandle(pipe);
    }
    if (host_mutex != nullptr) {
        (void)WaitForSingleObject(host_mutex, 3000U);
        CloseHandle(host_mutex);
    }
}

// Unregistering still happens before anything is deleted, so a live TSF
// profile is never left pointing at files that are already gone. What changed
// is the response to failure: every step reports and continues. Refusing to
// clean up because a program is missing left users with an installation they
// could not remove by any means -- and a missing program is usually the reason
// there is nothing left to unregister.
[[nodiscard]] std::vector<std::wstring> unregister_user_tsf() {
    std::vector<std::wstring> problems;
    const HRESULT deactivated = deactivate_profile();
    if (FAILED(deactivated)) {
        problems.emplace_back(L"未能停用当前 PiInput profile");
    }
    if (FAILED(disable_user_keyboard())) {
        problems.emplace_back(L"未能从当前用户的键盘列表中移除 PiInput");
    }
    return problems;
}

// Remove the per-user COM registration directly. The uninstaller deliberately
// does not load the product DLL: application-control policies commonly reject
// an unsigned DLL loaded by a temporary uninstall worker, and cleanup must
// still work when the DLL is damaged or already gone.
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
    request_host_drain();
    auto problems = unregister_user_tsf();

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

    return problems;
}

HRESULT CALLBACK topmost_task_dialog_callback(
    const HWND dialog,
    const UINT notification,
    WPARAM,
    LPARAM,
    LONG_PTR) {
    if (notification == TDN_CREATED) {
        (void)SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        (void)SetForegroundWindow(dialog);
    }
    return S_OK;
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
    config.pfCallback = topmost_task_dialog_callback;
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
    // Remove the keyboard entry while this process still has the original
    // user's token and while the machine profile still exists. The later
    // worker repeats this idempotently to cover races with the text service.
    request_host_drain();
    (void)unregister_user_tsf();
    // Only the machine-wide TSF profile/categories need elevation. Run that
    // narrow action from the installed executable, wait for it, then let the
    // original user's normal-token worker remove HKCU state and LocalAppData.
    // This remains correct when UAC credentials belong to a different account.
    SHELLEXECUTEINFOW privileged{};
    privileged.cbSize = sizeof(privileged);
    privileged.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    privileged.hwnd = GetForegroundWindow();
    privileged.lpVerb = L"runas";
    privileged.lpFile = source.c_str();
    privileged.lpParameters = L"--machine-unregister --silent";
    privileged.lpDirectory = source.parent_path().c_str();
    privileged.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&privileged) == FALSE || privileged.hProcess == nullptr) {
        throw std::runtime_error("Administrator permission is required to remove the TSF profile");
    }
    (void)WaitForSingleObject(privileged.hProcess, INFINITE);
    DWORD privileged_exit = static_cast<DWORD>(E_FAIL);
    (void)GetExitCodeProcess(privileged.hProcess, &privileged_exit);
    CloseHandle(privileged.hProcess);
    if (FAILED(static_cast<HRESULT>(privileged_exit))) {
        throw std::runtime_error("Machine-wide TSF profile cleanup failed");
    }

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
    execute.lpVerb = L"open";
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
        ScopedComApartment com;
        if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
            throw std::runtime_error("Cannot initialize COM for PiInput uninstall");
        }
        auto arguments = parse_arguments();
        if (arguments.machine_unregister) {
            SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
            PSID administrators = nullptr;
            BOOL member = FALSE;
            const BOOL allocated = AllocateAndInitializeSid(&authority, 2U,
                SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                0U, 0U, 0U, 0U, 0U, 0U, &administrators);
            const BOOL checked = allocated != FALSE
                ? CheckTokenMembership(nullptr, administrators, &member)
                : FALSE;
            if (administrators != nullptr) FreeSid(administrators);
            if (checked == FALSE || member == FALSE) {
                return static_cast<int>(E_ACCESSDENIED);
            }
            const HRESULT result = unregister_machine_tsf().result;
            if (FAILED(result)) return static_cast<int>(result);
            const auto program_files = known_folder(FOLDERID_ProgramFiles);
            const auto machine_root = machine_runtime_root(program_files);
            if (!is_safe_machine_runtime_root(machine_root, program_files)) {
                return static_cast<int>(HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
            }
            try {
                remove_or_schedule_legacy_runtime(machine_root);
            } catch (...) {
                return static_cast<int>(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
            }
            return 0;
        }
        if (arguments.worker) {
            const auto problems = run_worker(arguments);
            if (!arguments.silent) {
                if (problems.empty()) {
                    MessageBoxW(nullptr, L"PiInput 已卸载，相关文件和注册表项已清理。",
                        L"PiInput 卸载完成",
                        MB_OK | MB_ICONINFORMATION | kForegroundMessageBox);
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
                        MB_OK | MB_ICONWARNING | kForegroundMessageBox);
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
            L"PiInput 卸载失败", MB_OK | MB_ICONERROR | kForegroundMessageBox);
        return 1;
    }
}
