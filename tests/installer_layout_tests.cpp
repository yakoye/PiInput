#include "install_layout.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
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
    std::filesystem::remove_all(package);
    std::cout << "PiInput installer layout tests passed.\n";
    return 0;
}
