#include "install_layout.h"
#include "liteime_tsf_guids.h"

#include "liteime/windows_compat.h"

#include <shlobj.h>
#include <shellapi.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using liteime::windows::installer::version_directory;

[[nodiscard]] std::filesystem::path executable_path() {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        throw std::runtime_error("Cannot locate LiteIME-Install.exe");
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

void require_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Required installer payload is missing");
    }
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
            std::filesystem::copy_file(item.path(), target, std::filesystem::copy_options::overwrite_existing);
        }
    }
}

[[nodiscard]] std::wstring com_registry_key() {
    return L"Software\\Classes\\CLSID\\" + guid_string(CLSID_LiteImeTextService) + L"\\InprocServer32";
}

[[nodiscard]] std::wstring read_registered_dll() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, com_registry_key().c_str(), 0U, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
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
    const LONG create = RegCreateKeyExW(HKEY_CURRENT_USER, com_registry_key().c_str(), 0U, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot update the LiteIME COM registration");
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
        throw std::runtime_error("Cannot write the LiteIME COM registration");
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

[[nodiscard]] HRESULT register_first_install(const std::filesystem::path& dll) {
    const HMODULE module = LoadLibraryExW(dll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    using RegisterServer = HRESULT(__stdcall*)();
    const auto function = reinterpret_cast<RegisterServer>(GetProcAddress(module, "DllRegisterServer"));
    const HRESULT result = function == nullptr ? HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND) : function();
    FreeLibrary(module);
    return result;
}

void unregister_failed_first_install(const std::filesystem::path& dll) noexcept {
    const HMODULE module = LoadLibraryExW(dll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr) {
        return;
    }
    using UnregisterServer = HRESULT(__stdcall*)();
    const auto function = reinterpret_cast<UnregisterServer>(GetProcAddress(module, "DllUnregisterServer"));
    if (function != nullptr) {
        (void)function();
    }
    FreeLibrary(module);
}

void initialize_user_settings(const std::filesystem::path& user_data) {
    std::filesystem::create_directories(user_data);
    const auto settings = user_data / L"settings.ini";
    if (!std::filesystem::exists(settings)) {
        std::ofstream output(settings, std::ios::binary | std::ios::trunc);
        output << "schema=flypy\n"
               << "single_syllable_page_size=9\n"
               << "phrase_page_size=6\n";
    }
}

void write_current_marker(const std::filesystem::path& developer_root, const std::filesystem::path& version_root) {
    const auto temporary = developer_root / L"current.tmp";
    const auto current = developer_root / L"current.txt";
    {
        std::wofstream output(temporary, std::ios::trunc);
        output << version_root.wstring();
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
        const auto pending = item.path().wstring() + L".delete";
        if (MoveFileExW(item.path().c_str(), pending.c_str(), 0U) != FALSE) {
            std::filesystem::remove_all(pending, error);
            error.clear();
        }
    }
}

[[nodiscard]] std::filesystem::path install() {
    const auto installer = executable_path();
    const auto source_bin = installer.parent_path();
    const auto source_data = source_bin.parent_path() / L"data";
    require_file(source_bin / L"LiteImeTSF.dll");
    require_file(source_bin / L"liteime-profile.exe");
    require_file(source_data / L"base_lexicon.tsv");
    require_file(source_data / L"symbols.tsv");

    const auto liteime_root = local_app_data() / L"LiteIME";
    const auto developer_root = liteime_root / L"Dev";
    const auto target = version_directory(developer_root, LITEIME_VERSION, build_id());
    const auto target_bin = target / L"bin";
    copy_tree(source_bin, target_bin);
    copy_tree(source_data, target / L"data");
    initialize_user_settings(liteime_root / L"UserData");

    const auto new_dll = target_bin / L"LiteImeTSF.dll";
    const auto profile = target_bin / L"liteime-profile.exe";
    const std::wstring previous_dll = read_registered_dll();
    bool first_registration_attempted = false;
    try {
        const DWORD previous_status = run_hidden(profile, L"--status");
        if (previous_status == 0U) {
            write_registered_dll(new_dll);
        } else {
            first_registration_attempted = true;
            const HRESULT registration = register_first_install(new_dll);
            if (FAILED(registration) && run_hidden(profile, L"--status") != 0U) {
                throw std::runtime_error("TSF registration failed");
            }
            write_registered_dll(new_dll);
        }
        if (run_hidden(profile, L"--activate") != 0U || run_hidden(profile, L"--status") != 0U) {
            throw std::runtime_error("TSF profile activation or verification failed");
        }
        write_current_marker(developer_root, target);
    } catch (...) {
        if (!previous_dll.empty()) {
            write_registered_dll(previous_dll);
        } else if (first_registration_attempted) {
            unregister_failed_first_install(new_dll);
        }
        throw;
    }

    clean_unlocked_versions(developer_root / L"versions", target);
    return target;
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

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool silent = has_argument(L"--silent");
    try {
        const auto target = install();
        const std::wstring message =
            L"LiteIME 已安装完成。\n\n"
            L"不需要关闭当前正在编辑的程序。请重新打开要测试的程序，"
            L"再通过 Win+Space 选择 LiteIME。\n\n安装目录：\n" + target.wstring();
        if (!silent) {
            MessageBoxW(nullptr, message.c_str(), L"LiteIME 安装完成", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    } catch (const std::filesystem::filesystem_error& error) {
        const std::wstring detail = widen_error(error);
        if (!silent) {
            MessageBoxW(nullptr, (L"LiteIME 安装失败：\n" + detail).c_str(),
                L"LiteIME 安装失败", MB_OK | MB_ICONERROR);
        }
        return 1;
    } catch (const std::exception& error) {
        const std::wstring detail = widen_error(error);
        if (!silent) {
            MessageBoxW(nullptr, (L"LiteIME 安装失败：\n" + detail).c_str(),
                L"LiteIME 安装失败", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
