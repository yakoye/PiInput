#include "install_layout.h"
#include "migration.h"
#include "stable_runtime.h"
#include "uninstall_layout.h"
#include "piinput_tsf_guids.h"
#include "machine_registration.h"
#include "profile_registration.h"
#include "user_keyboard_registration.h"
#include "piinput/host_protocol.h"

#include "piinput/windows_compat.h"

#include <shlobj.h>
#include <shellapi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using piinput::windows::installer::current_marker_value;
using piinput::windows::installer::can_reuse_registered_stable_shim;
using piinput::windows::installer::discover_legacy_runtime;
using piinput::windows::installer::files_are_identical;
using piinput::windows::installer::is_safe_migration_source;
using piinput::windows::installer::locate_installer_payload;
using piinput::windows::installer::make_post_install_launch_targets;
using piinput::windows::installer::migrate_legacy_user_data;
using piinput::windows::installer::quote_windows_argument;
using piinput::windows::installer::remove_or_schedule_legacy_runtime;
using piinput::windows::installer::make_stable_runtime_layout;
using piinput::windows::installer::make_uninstall_layout;
using piinput::windows::installer::make_uninstall_registry_values;
using piinput::windows::installer::RuntimeMarker;
using piinput::windows::installer::StableShimRefreshResult;
using piinput::windows::installer::refresh_stable_shim;
using piinput::windows::installer::write_runtime_marker_atomic;
using piinput::windows::installer::sanitize_component;
using piinput::windows::installer::stable_shim_registration_fallback;
using piinput::windows::installer::version_directory;
using piinput::windows::tsf::disable_user_keyboard;
using piinput::windows::tsf::enable_user_keyboard;
using piinput::windows::tsf::get_profile;
using piinput::windows::tsf::register_machine_tsf;
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

// Retired product identity. It is intentionally kept as numeric GUID fields so
// new packages can remove the obsolete TSF registration without publishing the
// retired identifier as a current product string.
inline constexpr CLSID kRetiredTextService =
    {0x84e21a77, 0x3a42, 0x4d7b, {0x93, 0xb8, 0xbc, 0xdf, 0x81, 0x8f, 0xc4, 0x14}};

inline constexpr CLSID kLegacyPiInputTextService =
    {0xd73aaba7, 0xbe3e, 0x4e53, {0x8d, 0xe2, 0x65, 0x2d, 0x35, 0x27, 0x43, 0xf3}};

[[nodiscard]] std::filesystem::path executable_path() {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        throw std::runtime_error("Cannot locate PiInput-Install.exe");
    }
    buffer.resize(length);
    return buffer;
}

[[nodiscard]] std::filesystem::path local_app_data() {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        throw std::runtime_error("Cannot locate LocalAppData");
    }
    const std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

[[nodiscard]] std::filesystem::path roaming_app_data() {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        throw std::runtime_error("Cannot locate RoamingAppData");
    }
    const std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

[[nodiscard]] std::wstring build_id() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::wostringstream output;
    output << std::setfill(L'0')
           << std::setw(4) << time.wYear
           << std::setw(2) << time.wMonth
           << std::setw(2) << time.wDay << L'-'
           << std::setw(2) << time.wHour
           << std::setw(2) << time.wMinute
           << std::setw(2) << time.wSecond << L'-'
           << GetCurrentProcessId();
    return output.str();
}

[[nodiscard]] std::wstring guid_string(const GUID& guid) {
    std::array<wchar_t, 64> buffer{};
    StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size()));
    return buffer.data();
}

[[nodiscard]] std::string hresult_error(
    const std::string_view operation,
    const HRESULT result) {
    std::ostringstream message;
    message << operation << ": HRESULT 0x"
            << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
            << static_cast<std::uint32_t>(result);
    return message.str();
}

void enable_and_verify_current_user_profile(bool& keyboard_enabled) {
    const HRESULT enabled = enable_user_keyboard();
    if (FAILED(enabled)) {
        throw std::runtime_error(hresult_error(
            "Cannot add PiInput to the current user's keyboard list", enabled));
    }
    // Record the side effect before the visibility poll so a later timeout
    // still removes the keyboard entry during first-install rollback.
    keyboard_enabled = true;

    HRESULT last_result = E_FAIL;
    DWORD last_flags = 0U;
    for (unsigned int attempt = 0U; attempt < 20U; ++attempt) {
        TF_INPUTPROCESSORPROFILE profile{};
        last_result = get_profile(&profile);
        if (SUCCEEDED(last_result)) {
            last_flags = profile.dwFlags;
            if ((profile.dwFlags & TF_IPP_FLAG_ENABLED) != 0U) return;
        }
        Sleep(250U);
    }

    std::ostringstream message;
    message << hresult_error("PiInput profile did not become enabled", last_result)
            << ", flags=0x" << std::hex << std::uppercase << last_flags;
    throw std::runtime_error(message.str());
}

void require_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Required installer payload is missing");
    }
}

// Installing in place means overwriting files a running process may still hold
// open -- the Host until it drains, and the Shim for as long as any application
// that loaded it is running. A held file is renamed out of the way and deleted
// at the next restart, so the new one can take its place immediately.
void copy_file_replacing_locked(
    const std::filesystem::path& source,
    const std::filesystem::path& target) {
    std::error_code error;
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing, error);
    if (!error) return;

    const auto retired = target.wstring() + L".retired." +
        std::to_wstring(GetTickCount64());
    if (MoveFileExW(target.c_str(), retired.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        throw std::runtime_error(
            "copy_file: " + error.message() + ": \"" + source.string() + "\", \"" +
            target.string() + "\"");
    }
    (void)MoveFileExW(retired.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing);
}

void copy_tree(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!std::filesystem::is_directory(source)) {
        throw std::runtime_error("Installer payload directory is missing");
    }
    std::filesystem::create_directories(destination);
    for (const auto& item : std::filesystem::recursive_directory_iterator(source)) {
        const auto relative = std::filesystem::relative(item.path(), source);
        const auto target = destination / relative;
        if (item.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (item.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path());
            copy_file_replacing_locked(item.path(), target);
        }
    }
}

std::filesystem::path install_or_refresh_stable_shim(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::wstring_view refresh_id) {
    const StableShimRefreshResult refreshed =
        refresh_stable_shim(source, destination, refresh_id);
    if (refreshed.exact_bytes) return refreshed.path;
    std::ostringstream message;
    message << "The stable PiInput input entry could not be refreshed (Windows error "
            << refreshed.error << "). Close the listed applications or restart Windows, "
            << "then run this installer again.";
    throw std::runtime_error(message.str());
}

[[nodiscard]] std::wstring com_registry_key(const CLSID& class_id) {
    return L"Software\\Classes\\CLSID\\" + guid_string(class_id) + L"\\InprocServer32";
}

[[nodiscard]] std::wstring read_registered_dll(const CLSID& class_id) {
    HKEY key = nullptr;
    const std::wstring registry_key = com_registry_key(class_id);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, registry_key.c_str(), 0U, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0U;
    DWORD bytes = 0U;
    LONG result = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes);
    std::wstring value;
    if (result == ERROR_SUCCESS && type == REG_SZ && bytes >= sizeof(wchar_t)) {
        value.resize(bytes / sizeof(wchar_t));
        result = RegQueryValueExW(key, nullptr, nullptr, &type,
            reinterpret_cast<BYTE*>(value.data()), &bytes);
        if (result == ERROR_SUCCESS && !value.empty() && value.back() == L'\0') {
            value.pop_back();
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? value : std::wstring{};
}

void write_registered_dll(const std::filesystem::path& dll) {
    HKEY key = nullptr;
    const std::wstring registry_key = com_registry_key(CLSID_PiInputTextService);
    const LONG create = RegCreateKeyExW(HKEY_CURRENT_USER, registry_key.c_str(), 0U, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot update the PiInput COM registration");
    }
    const std::wstring value = dll.wstring();
    const DWORD bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    LONG result = RegSetValueExW(key, nullptr, 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    constexpr wchar_t threading[] = L"Apartment";
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, L"ThreadingModel", 0U, REG_SZ,
            reinterpret_cast<const BYTE*>(threading), sizeof(threading));
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot write the PiInput COM registration");
    }
}

[[nodiscard]] DWORD run_hidden(const std::filesystem::path& program, const std::wstring& arguments) {
    std::wstring command = L"\"" + program.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, program.parent_path().c_str(), &startup, &process) == FALSE) {
        return GetLastError();
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1U;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

[[nodiscard]] std::wstring read_runtime_registry_string(const wchar_t* const name) {
    constexpr wchar_t key_path[] = L"Software\\PiInput\\Runtime";
    DWORD type = 0U;
    DWORD bytes = 0U;
    if (RegGetValueW(HKEY_CURRENT_USER, key_path, name, RRF_RT_REG_SZ,
            &type, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key_path, name, RRF_RT_REG_SZ,
            &type, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

void write_runtime_registry_string(const wchar_t* const name, const std::wstring& value) {
    constexpr wchar_t key_path[] = L"Software\\PiInput\\Runtime";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key_path, 0U, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot create the PiInput runtime registry key");
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    const LONG result = RegSetValueExW(key, name, 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot update the PiInput runtime path");
    }
}

void remove_runtime_registry_value(const wchar_t* const name) noexcept {
    constexpr wchar_t key_path[] = L"Software\\PiInput\\Runtime";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0U, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        (void)RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
}

void remove_host_autostart() noexcept {
    constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0U, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        (void)RegDeleteValueW(key, L"PiInputHost");
        RegCloseKey(key);
    }
}

[[nodiscard]] bool start_detached(const std::filesystem::path& program) noexcept {
    std::wstring command = L"\"" + program.wstring() + L"\" --serve";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, program.parent_path().c_str(),
            &startup, &process) == FALSE) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void drain_installed_host(const std::filesystem::path& host) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(host, error) || error) return;
    (void)run_hidden(host, L"--drain");
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        if (run_hidden(host, L"--health") != 0U) return;
        Sleep(50U);
    }
}

void activate_versioned_host(
    const std::filesystem::path& host,
    const std::wstring& previous_host) {
    if (!previous_host.empty() && std::filesystem::is_regular_file(previous_host)) {
        (void)run_hidden(previous_host, L"--drain");
    }
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        if (run_hidden(host, L"--health") != 0U) break;
        if (attempt + 1U == 100U) {
            throw std::runtime_error("The previous PiInput Host did not finish draining");
        }
        Sleep(50U);
    }
    write_runtime_registry_string(L"CurrentHostPath", host.wstring());
    if (!start_detached(host)) {
        throw std::runtime_error("Cannot start the versioned PiInput Host");
    }
    const std::wstring expected_health =
        L"--health " + std::wstring(PIINPUT_INSTALLER_BUILD_ID);
    for (unsigned int attempt = 0U; attempt < 40U; ++attempt) {
        if (run_hidden(host, expected_health) == 0U) return;
        Sleep(50U);
    }
    (void)run_hidden(host, L"--drain");
    throw std::runtime_error("The versioned PiInput Host did not match the requested build ID");
}

void retire_tsf_identity(const CLSID& identity) noexcept {
    const std::wstring registered = read_registered_dll(identity);
    if (registered.empty()) {
        return;
    }

    const std::filesystem::path retired_dll(registered);
    const std::filesystem::path retired_profile = retired_dll.parent_path() / L"piinput-profile.exe";
    if (std::filesystem::is_regular_file(retired_profile)) {
        (void)run_hidden(retired_profile, L"--deactivate");
        (void)run_hidden(retired_profile, L"--disable-user");
    }

    const HMODULE module = LoadLibraryExW(retired_dll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr) {
        return;
    }
    using UnregisterServer = HRESULT(__stdcall*)();
    const auto unregister_server = reinterpret_cast<UnregisterServer>(
        GetProcAddress(module, "DllUnregisterServer"));
    if (unregister_server != nullptr) {
        (void)unregister_server();
    }
    FreeLibrary(module);
}

void retire_previous_tsf_identities() noexcept {
    retire_tsf_identity(kRetiredTextService);
    retire_tsf_identity(kLegacyPiInputTextService);
}

[[nodiscard]] bool process_is_elevated() noexcept {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (AllocateAndInitializeSid(&authority, 2U,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0U, 0U, 0U, 0U, 0U, 0U, &administrators) == FALSE) {
        return false;
    }
    BOOL member = FALSE;
    const BOOL checked = CheckTokenMembership(nullptr, administrators, &member);
    FreeSid(administrators);
    return checked != FALSE && member != FALSE;
}

[[nodiscard]] HRESULT run_elevated_machine_action(
    const std::wstring& arguments) {
    const auto program = executable_path();
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.hwnd = GetForegroundWindow();
    execute.lpVerb = L"runas";
    execute.lpFile = program.c_str();
    execute.lpParameters = arguments.c_str();
    execute.lpDirectory = program.parent_path().c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&execute) == FALSE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (execute.hProcess == nullptr) return E_FAIL;
    (void)WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exit_code = static_cast<DWORD>(E_FAIL);
    (void)GetExitCodeProcess(execute.hProcess, &exit_code);
    CloseHandle(execute.hProcess);
    return static_cast<HRESULT>(exit_code);
}

[[nodiscard]] HRESULT register_machine_profile_elevated(
    const std::filesystem::path& dll) {
    return run_elevated_machine_action(
        L"--machine-register " + quote_windows_argument(dll.wstring()) + L" --silent");
}

void unregister_machine_profile_elevated() noexcept {
    try {
        (void)run_elevated_machine_action(L"--machine-unregister --silent");
    } catch (...) {
    }
}

void initialize_user_settings(const std::filesystem::path& user_data) {
    std::filesystem::create_directories(user_data);
    const auto settings = user_data / L"settings.ini";
    if (!std::filesystem::exists(settings)) {
        std::ofstream output(settings, std::ios::binary | std::ios::trunc);
        output << "[general]\n"
               << "schema=flypy\n"
               << "default_language=chinese\n"
               << "hot_reload=true\n"
               << "\n"
               << "[candidates]\n"
               << "items_per_row=6\n"
               << "visible_rows=5\n"
               << "max_items=90\n"
               << "font_size=16\n"
               << "window_height=40\n"
               << "\n"
               << "[punctuation]\n"
               << "mode=chinese\n"
               << "bracket_style=sogou\n";
        return;
    }

    std::ifstream input(settings, std::ios::binary);
    const std::string existing{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (existing.find("bracket_style=") == std::string::npos) {
        std::ofstream output(settings, std::ios::binary | std::ios::app);
        if (!existing.empty() && existing.back() != '\n') output << '\n';
        output << "\n[punctuation]\n"
               << "bracket_style=sogou\n";
    }
}

void write_current_marker(const std::filesystem::path& developer_root, const std::filesystem::path& version_root) {
    const auto temporary = developer_root / L"current.tmp";
    const auto current = developer_root / L"current.txt";
    {
        std::wofstream output(temporary, std::ios::trunc);
        output << current_marker_value(version_root);
    }
    if (MoveFileExW(temporary.c_str(), current.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        throw std::runtime_error("Cannot update the current-version marker");
    }
}

void clean_unlocked_versions(const std::filesystem::path& versions, const std::filesystem::path& current) {
    std::error_code error;
    if (!std::filesystem::is_directory(versions, error)) {
        return;
    }
    for (const auto& item : std::filesystem::directory_iterator(versions, error)) {
        if (error || !item.is_directory() || std::filesystem::equivalent(item.path(), current, error)) {
            error.clear();
            continue;
        }
        try {
            remove_or_schedule_legacy_runtime(item.path());
        } catch (...) {
            // Cleanup is best effort after the new version has been registered.
            // A locked old runtime must not roll back a working installation.
        }
    }
}

void set_registry_string(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    if (RegSetValueExW(key, name, 0U, REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()), bytes) != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot write the PiInput uninstall registry entry");
    }
}

void install_uninstaller(
    const std::filesystem::path& source,
    const std::filesystem::path& local_root,
    const std::filesystem::path& active_dll) {
    const auto layout = make_uninstall_layout(local_root, roaming_app_data());
    std::filesystem::create_directories(layout.uninstall_root);
    const auto temporary = layout.uninstall_root / L"PiInput-Uninstall.tmp";
    std::filesystem::copy_file(source, temporary,
        std::filesystem::copy_options::overwrite_existing);
    if (MoveFileExW(temporary.c_str(), layout.stable_uninstaller.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        throw std::runtime_error("Cannot install PiInput-Uninstall.exe");
    }

    const auto values = make_uninstall_registry_values(
        layout, PIINPUT_INSTALLER_VERSION, active_dll);
    constexpr wchar_t path[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PiInput";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0U, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot create the PiInput uninstall registry entry");
    }
    try {
        set_registry_string(key, L"DisplayName", values.display_name);
        set_registry_string(key, L"DisplayVersion", values.display_version);
        set_registry_string(key, L"Publisher", values.publisher);
        set_registry_string(key, L"InstallLocation", values.install_location);
        set_registry_string(key, L"DisplayIcon", values.display_icon);
        set_registry_string(key, L"UninstallString", values.uninstall_string);
        set_registry_string(key, L"QuietUninstallString", values.quiet_uninstall_string);
        constexpr DWORD one = 1U;
        if (RegSetValueExW(key, L"NoModify", 0U, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one)) != ERROR_SUCCESS ||
            RegSetValueExW(key, L"NoRepair", 0U, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one)) != ERROR_SUCCESS) {
            throw std::runtime_error("Cannot finalize the PiInput uninstall registry entry");
        }
    } catch (...) {
        RegCloseKey(key);
        throw;
    }
    RegCloseKey(key);
}

struct InstallResult {
    std::filesystem::path program_root;
    std::filesystem::path user_data;
};

[[nodiscard]] InstallResult install(
    const std::optional<std::filesystem::path>& migration_source) {
    const auto installer = executable_path();
    const auto payload = locate_installer_payload(installer);
    const auto& source_bin = payload.bin;
    const auto& source_data = payload.data;
    require_file(source_bin / L"PiInputTSF.dll");
    require_file(source_bin / L"PiInputHost.exe");
    require_file(source_bin / L"PiInput-Settings.exe");
    require_file(source_bin / L"piinput-profile.exe");
    require_file(source_bin / L"PiInput-Uninstall.exe");
    require_file(source_bin / L"piinput_icon.ico");
    // yesymbol.exe is optional: an older package may not carry it, and the
    // language bar falls back to a message telling the user where to set one.
    require_file(source_data / L"piinput-base.lex");
    require_file(source_data / L"symbols.tsv");

    const auto local_root = local_app_data();
    const auto piinput_root = local_root / L"PiInput";
    const auto effective_migration = migration_source.has_value()
        ? migration_source
        : discover_legacy_runtime(local_root, piinput_root);
    if (effective_migration.has_value() && !is_safe_migration_source(
            *effective_migration, local_root, piinput_root)) {
        throw std::runtime_error("Unsafe --migrate-from path");
    }
    const std::wstring version_id =
        sanitize_component(PIINPUT_INSTALLER_VERSION) + L"-" + sanitize_component(build_id());
    const auto runtime = make_stable_runtime_layout(piinput_root, version_id);
    if (!runtime.has_value()) {
        throw std::runtime_error("Cannot create a safe PiInput stable-runtime layout");
    }
    // One fixed location: bin beside data, both overwritten in place. There is
    // no per-version directory to accumulate and no versioned path that a
    // registration can be captured against and then outlive.
    const auto target = runtime->root;
    const auto target_bin = runtime->shim_directory;
    // The Host lives at a fixed path now and is overwritten in place, so it has
    // to be shut down before the copy rather than after it.
    drain_installed_host(target_bin / L"PiInputHost.exe");
    copy_tree(source_bin, target_bin);
    copy_tree(source_data, target / L"data");
    initialize_user_settings(piinput_root / L"UserData");

    const std::wstring previous_dll = read_registered_dll(CLSID_PiInputTextService);
    const auto versioned_shim = target_bin / L"PiInputTSF.dll";
    const auto new_dll = can_reuse_registered_stable_shim(
            previous_dll, runtime->shim_dll, versioned_shim)
        ? runtime->shim_dll
        : install_or_refresh_stable_shim(
            versioned_shim, runtime->shim_dll, version_id);
    const auto new_host = target_bin / L"PiInputHost.exe";
    const auto stable_icon = runtime->shim_dll.parent_path() / L"piinput_icon.ico";
    std::filesystem::copy_file(
        source_bin / L"piinput_icon.ico", stable_icon,
        std::filesystem::copy_options::overwrite_existing);
    const std::wstring previous_host = read_runtime_registry_string(L"CurrentHostPath");
    bool first_registration_attempted = false;
    bool user_keyboard_enabled = false;
    bool machine_profile_preexisted = false;
    try {
        TF_INPUTPROCESSORPROFILE existing_profile{};
        machine_profile_preexisted = SUCCEEDED(get_profile(&existing_profile));
        const bool stable_shim_already_registered = !previous_dll.empty() &&
            _wcsicmp(previous_dll.c_str(), new_dll.c_str()) == 0 &&
            machine_profile_preexisted;
        if (!stable_shim_already_registered) {
            first_registration_attempted = true;
            // TSF profile/category registration is machine-wide on Windows and
            // therefore requires elevation. File deployment, HKCU COM wiring,
            // settings and keyboard-list changes deliberately stay in this
            // original user's unelevated process. This also works when a
            // standard user supplies different administrator credentials.
            const HRESULT registration = register_machine_profile_elevated(new_dll);
            if (FAILED(registration)) {
                std::ostringstream message;
                message << "TSF system registration failed: HRESULT 0x"
                        << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
                        << static_cast<std::uint32_t>(registration);
                throw std::runtime_error(message.str());
            }
            write_registered_dll(new_dll);
        }
        // The elevated action above only owns machine-wide TSF state. Add the
        // profile to this original user's keyboard list here, never in the
        // administrator account used to approve UAC.
        enable_and_verify_current_user_profile(user_keyboard_enabled);
        retire_previous_tsf_identities();
        if (effective_migration.has_value()) {
            migrate_legacy_user_data(*effective_migration, piinput_root);
        }
        activate_versioned_host(new_host, previous_host);
        if (!write_runtime_marker_atomic(
                runtime->current_marker, RuntimeMarker{version_id, piinput::host_protocol_current})) {
            throw std::runtime_error("Cannot atomically publish the PiInput runtime marker");
        }
        // The Host is a console executable. Launching it from HKCU\Run creates
        // a visible terminal at every sign-in. It is already started detached
        // above and the TSF Shim can restart it invisibly on first use, so the
        // login entry is both unnecessary and user-visible. Upgrades also
        // remove the entry left by older releases.
        remove_host_autostart();
        install_uninstaller(source_bin / L"PiInput-Uninstall.exe", local_root, new_dll);
    } catch (...) {
        if (user_keyboard_enabled && first_registration_attempted) {
            (void)disable_user_keyboard();
        }
        if (!previous_dll.empty()) {
            write_registered_dll(previous_dll);
        } else if (first_registration_attempted) {
            const std::wstring base = L"Software\\Classes\\CLSID\\" +
                guid_string(CLSID_PiInputTextService);
            (void)RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
            if (!machine_profile_preexisted) {
                unregister_machine_profile_elevated();
            }
        }
        if (!previous_host.empty()) {
            write_runtime_registry_string(L"CurrentHostPath", previous_host);
            remove_host_autostart();
            if (std::filesystem::is_regular_file(previous_host)) {
                (void)start_detached(previous_host);
            }
        } else {
            remove_runtime_registry_value(L"CurrentHostPath");
            remove_host_autostart();
        }
        throw;
    }

    // Upgrading also cleans up: the old layout left a Runtime tree (a permanent
    // Shim plus one directory per version, none of which anything removed) and
    // an older Dev tree beside it. Both are gone now, and a locked file is
    // scheduled rather than left behind.
    for (const std::wstring_view legacy : {L"Runtime", L"Dev"}) {
        const auto path = piinput_root / legacy;
        std::error_code legacy_error;
        if (!std::filesystem::is_directory(path, legacy_error) || legacy_error) continue;
        try {
            remove_or_schedule_legacy_runtime(path);
        } catch (...) {
            // Best effort once the new version is registered and working.
        }
    }
    if (effective_migration.has_value()) {
        remove_or_schedule_legacy_runtime(*effective_migration);
    }
    return {target, piinput_root / L"UserData"};
}

[[nodiscard]] std::wstring widen_error(const std::exception& error) {
    const std::string text(error.what());
    return std::wstring(text.begin(), text.end());
}

[[nodiscard]] bool has_argument(const std::wstring_view expected) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return false;
    }
    bool found = false;
    for (int index = 1; index < count; ++index) {
        if (std::wstring_view(arguments[index]) == expected) {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

[[nodiscard]] std::optional<std::wstring> argument_value(const std::wstring_view expected) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return std::nullopt;
    }
    std::optional<std::wstring> value;
    for (int index = 1; index + 1 < count; ++index) {
        if (std::wstring_view(arguments[index]) == expected) {
            value = arguments[index + 1];
            break;
        }
    }
    LocalFree(arguments);
    return value;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool silent = has_argument(L"--silent");
    try {
        ScopedComApartment com;
        if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
            throw std::runtime_error(hresult_error("Cannot initialize COM", com.result()));
        }
        if (const auto dll = argument_value(L"--machine-register"); dll.has_value()) {
            if (!process_is_elevated()) return static_cast<int>(E_ACCESSDENIED);
            std::error_code error;
            const std::filesystem::path path(*dll);
            if (!std::filesystem::is_regular_file(path, error) || error ||
                _wcsicmp(path.filename().c_str(), L"PiInputTSF.dll") != 0) {
                return static_cast<int>(HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
            }
            return static_cast<int>(register_machine_tsf(path.wstring()).result);
        }
        if (has_argument(L"--machine-unregister")) {
            if (!process_is_elevated()) return static_cast<int>(E_ACCESSDENIED);
            return static_cast<int>(unregister_machine_tsf().result);
        }
        const auto migration = argument_value(L"--migrate-from");
        const auto result = install(migration.has_value()
            ? std::optional<std::filesystem::path>(*migration)
            : std::nullopt);
        const std::wstring message =
            L"PiInput 已安装完成。\n\n"
            L"安装器没有自动激活输入法，也没有关闭任何程序。请重新打开要测试的程序，"
            L"再通过 Win+Space 主动选择 PiInput。\n\n安装目录：\n" + result.program_root.wstring() +
            L"\n\n用户设置和词库：\n" + result.user_data.wstring() +
            L"\n\n点击「确定」后会打开配置目录和 PiInput 设置软件。";
        if (!silent) {
            MessageBoxW(nullptr, message.c_str(), L"PiInput 安装完成",
                MB_OK | MB_ICONINFORMATION | kForegroundMessageBox);
            const auto launch = make_post_install_launch_targets(
                result.program_root, result.user_data);
            // Keep the configuration directory visible for dictionaries and
            // advanced editing, then bring the normal Settings UI to the front.
            (void)ShellExecuteW(nullptr, L"open", launch.user_data_directory.c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
            const std::wstring settings_arguments =
                L"--settings " + quote_windows_argument(launch.settings_file.wstring());
            (void)ShellExecuteW(nullptr, L"open", launch.settings_executable.c_str(),
                settings_arguments.c_str(), launch.settings_executable.parent_path().c_str(),
                SW_SHOWNORMAL);
        }
        return 0;
    } catch (const std::filesystem::filesystem_error& error) {
        const std::wstring detail = widen_error(error);
        if (!silent) {
            MessageBoxW(nullptr, (L"PiInput 安装失败：\n" + detail).c_str(),
                L"PiInput 安装失败", MB_OK | MB_ICONERROR | kForegroundMessageBox);
        }
        return 1;
    } catch (const std::exception& error) {
        const std::wstring detail = widen_error(error);
        if (!silent) {
            MessageBoxW(nullptr, (L"PiInput 安装失败：\n" + detail).c_str(),
                L"PiInput 安装失败", MB_OK | MB_ICONERROR | kForegroundMessageBox);
        }
        return 1;
    }
}
