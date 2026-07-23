#include "profile_registration.h"

#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <msctf.h>
#include <objbase.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using piinput::windows::tsf::activate_profile;
using piinput::windows::tsf::deactivate_profile;
using piinput::windows::tsf::get_profile;
using piinput::windows::tsf::register_profile;
using piinput::windows::tsf::unregister_profile;

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

[[nodiscard]] bool valid_schema(const std::string_view schema) {
    return schema == "full" || schema == "flypy" || schema == "natural" ||
           schema == "mspy" || schema == "abc";
}

void write_schema(const std::string& schema) {
    if (!valid_schema(schema)) {
        throw std::runtime_error("Unknown schema. Use full, flypy, natural, mspy, or abc.");
    }
    const auto path = local_app_data() / L"PiInput" / L"UserData" / L"settings.ini";
    std::filesystem::create_directories(path.parent_path());
    std::vector<std::string> lines;
    bool in_general = false;
    bool in_named_section = false;
    bool found_general = false;
    {
        std::ifstream input(path, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.size() >= 2U && line.front() == '[' && line.back() == ']') {
                in_named_section = true;
                in_general = line == "[general]";
                found_general = found_general || in_general;
                lines.push_back(line);
                if (in_general) {
                    lines.push_back("schema=" + schema);
                }
                continue;
            }
            if ((in_general || !in_named_section) && line.rfind("schema=", 0U) == 0U) {
                continue;
            }
            lines.push_back(std::move(line));
        }
    }
    if (!found_general) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.emplace_back();
        }
        lines.push_back("[general]");
        lines.push_back("schema=" + schema);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot write settings.ini");
    }
    for (const auto& line : lines) {
        output << line << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Failed while writing settings.ini");
    }
    std::cout << "PiInput schema set to: " << schema << '\n';
}

[[nodiscard]] std::string read_schema() {
    const auto path = local_app_data() / L"PiInput" / L"UserData" / L"settings.ini";
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::string legacy;
    bool in_general = false;
    bool in_named_section = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 2U && line.front() == '[' && line.back() == ']') {
            in_named_section = true;
            in_general = line == "[general]";
            continue;
        }
        constexpr std::string_view prefix = "schema=";
        if (line.rfind(prefix, 0U) == 0U) {
            if (in_general) {
                return line.substr(prefix.size());
            }
            if (!in_named_section) {
                legacy = line.substr(prefix.size());
            }
        }
    }
    return legacy.empty() ? "full" : legacy;
}

[[nodiscard]] int print_hresult_failure(const char* operation, const HRESULT result, const int exit_code) {
    std::cerr << operation << " failed: 0x"
              << std::hex << std::uppercase
              << static_cast<unsigned long>(result) << '\n';
    return exit_code;
}

int show_status() {
    TF_INPUTPROCESSORPROFILE profile{};
    const HRESULT result = get_profile(&profile);
    if (FAILED(result)) {
        std::cout << "registered=no\n"
                  << "enabled=no\n"
                  << "active=no\n"
                  << "hresult=0x" << std::hex << std::uppercase
                  << static_cast<unsigned long>(result) << '\n';
        return 4;
    }

    std::cout << "registered=yes\n"
              << "enabled=" << (((profile.dwFlags & TF_IPP_FLAG_ENABLED) != 0U) ? "yes" : "no") << '\n'
              << "active=" << (((profile.dwFlags & TF_IPP_FLAG_ACTIVE) != 0U) ? "yes" : "no") << '\n'
              << "flags=0x" << std::hex << std::uppercase << profile.dwFlags << '\n';
    return 0;
}

void print_help() {
    std::cout
        << "PiInput profile tool\n"
        << "  --register               Register and enable the PiInput TSF profile\n"
        << "  --unregister             Unregister the PiInput TSF profile\n"
        << "  --activate               Enable and activate the PiInput TSF profile\n"
        << "  --deactivate             Deactivate the PiInput TSF profile\n"
        << "  --status                 Show registered/enabled/active state\n"
        << "  --schema <name>          Set full/flypy/natural/mspy/abc\n"
        << "  --show-schema            Print the configured input schema\n";
}

int run(const int argc, wchar_t* argv[]) {
    if (argc <= 1) {
        print_help();
        return 0;
    }

    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--schema") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--schema requires a value");
            }
            write_schema(piinput::wide_to_utf8(argv[++index]));
        } else if (argument == L"--show-schema") {
            std::cout << read_schema() << '\n';
        } else if (argument == L"--register") {
            const HRESULT result = register_profile();
            if (FAILED(result)) {
                return print_hresult_failure("RegisterProfile", result, 5);
            }
            std::cout << "PiInput profile registered and enabled.\n";
        } else if (argument == L"--unregister") {
            const HRESULT result = unregister_profile();
            if (result == S_FALSE) {
                std::cout << "PiInput profile was not registered; nothing to remove.\n";
            } else if (FAILED(result)) {
                return print_hresult_failure("UnregisterProfile", result, 6);
            } else {
                std::cout << "PiInput profile unregistered.\n";
            }
        } else if (argument == L"--activate") {
            const HRESULT result = activate_profile();
            if (result != S_OK) {
                return print_hresult_failure("ActivateProfile", result, 2);
            }
            std::cout << "PiInput profile enabled. Use Win+Space to select it if Windows did not switch immediately.\n";
        } else if (argument == L"--deactivate") {
            const HRESULT result = deactivate_profile();
            if (result == S_FALSE) {
                std::cout << "PiInput profile was not active or not registered; nothing to deactivate.\n";
            } else if (FAILED(result)) {
                return print_hresult_failure("DeactivateProfile", result, 3);
            } else {
                std::cout << "PiInput profile deactivated.\n";
            }
        } else if (argument == L"--status") {
            const int status = show_status();
            if (status != 0) {
                return status;
            }
        } else if (argument == L"--help" || argument == L"-h") {
            print_help();
        } else {
            throw std::runtime_error("Unknown argument: " + piinput::wide_to_utf8(argv[index]));
        }
    }
    return 0;
}

}  // namespace

int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
        std::cerr << "CoInitializeEx failed: 0x" << std::hex << std::uppercase
                  << static_cast<unsigned long>(com_result) << '\n';
        return 7;
    }

    try {
        const int result = run(argc, argv);
        if (SUCCEEDED(com_result)) {
            CoUninitialize();
        }
        return result;
    } catch (const std::exception& error) {
        if (SUCCEEDED(com_result)) {
            CoUninitialize();
        }
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
