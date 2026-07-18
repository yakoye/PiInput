#include "install_layout.h"

#include <filesystem>
#include <iostream>

int main() {
    const std::filesystem::path root = L"C:\\Users\\tester\\AppData\\Local\\LiteIME\\Dev";
    const auto version = liteime::windows::installer::version_directory(
        root, L"0.2.0", L"20260719-003412-42");
    const auto expected = root / L"versions" / L"0.2.0-20260719-003412-42";
    if (version != expected) {
        std::cerr << "Version directory does not use side-by-side layout\n";
        return 1;
    }
    if (version / L"bin" / L"LiteImeTSF.dll" == root / L"bin" / L"LiteImeTSF.dll") {
        std::cerr << "Versioned DLL path unexpectedly equals the legacy fixed DLL path\n";
        return 2;
    }
    if (liteime::windows::installer::sanitize_component(L"0.2.0 dev/unsafe") != L"0.2.0-dev-unsafe") {
        std::cerr << "Unsafe path component was not sanitized deterministically\n";
        return 3;
    }
    std::cout << "LiteIME installer layout tests passed.\n";
    return 0;
}
