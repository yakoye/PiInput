#include "install_layout.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto registration = piinput::windows::installer::profile_install_commands();
    if (registration.size() != 3U ||
        registration[0].arguments != L"--refresh-profile" ||
        registration[0].max_attempts != 1U ||
        registration[1].arguments != L"--enable-user" ||
        registration[1].max_attempts != 1U ||
        registration[2].arguments != L"--status" ||
        registration[2].max_attempts < 2U ||
        registration[2].retry_delay_ms == 0U) {
        std::cerr << "Installer must refresh cached profile metadata, add PiInput, and verify it\n";
        return 1;
    }
    const std::filesystem::path root = L"C:\\Users\\tester\\AppData\\Local\\PiInput\\Dev";
    const auto version = piinput::windows::installer::version_directory(
        root, L"0.2.0", L"20260719-003412-42");
    const auto expected = root / L"versions" / L"0.2.0-20260719-003412-42";
    if (version != expected) {
        std::cerr << "Version directory does not use side-by-side layout\n";
        return 1;
    }
    if (version / L"bin" / L"PiInputTSF.dll" == root / L"bin" / L"PiInputTSF.dll") {
        std::cerr << "Versioned DLL path unexpectedly equals the legacy fixed DLL path\n";
        return 2;
    }
    if (piinput::windows::installer::sanitize_component(L"0.2.0 dev/unsafe") != L"0.2.0-dev-unsafe") {
        std::cerr << "Unsafe path component was not sanitized deterministically\n";
        return 3;
    }
    if (piinput::windows::installer::current_marker_value(version) != L"0.2.0-20260719-003412-42") {
        std::cerr << "Current marker must contain only the active version directory name\n";
        return 4;
    }
    const auto package = std::filesystem::temp_directory_path() / "piinput-installer-layout";
    std::filesystem::remove_all(package);
    std::filesystem::create_directories(package / "bin");
    std::filesystem::create_directories(package / "data");
    std::ofstream(package / "bin" / "PiInputTSF.dll") << "fixture";
    const auto payload = piinput::windows::installer::locate_installer_payload(
        package / "PiInput-Install.exe");
    if (payload.bin != package / "bin" || payload.data != package / "data") {
        std::cerr << "Root installer did not resolve the packaged bin/data layout\n";
        return 5;
    }
    const auto launch = piinput::windows::installer::make_post_install_launch_targets(
        root / L"Current", root / L"UserData");
    if (launch.settings_executable != root / L"Current" / L"bin" / L"PiInput-Settings.exe" ||
        launch.settings_file != root / L"UserData" / L"settings.ini" ||
        launch.user_data_directory != root / L"UserData") {
        std::cerr << "Post-install launch targets do not open Settings and UserData\n";
        return 6;
    }
    std::filesystem::remove_all(package);
    std::cout << "PiInput installer layout tests passed.\n";
    return 0;
}
