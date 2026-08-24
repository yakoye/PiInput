#include "install_layout.h"

namespace piinput::windows::installer {

std::wstring sanitize_component(const std::wstring_view value) {
    std::wstring result;
    result.reserve(value.size());
    bool previous_dash = false;
    for (const wchar_t character : value) {
        const bool safe = (character >= L'a' && character <= L'z') ||
                          (character >= L'A' && character <= L'Z') ||
                          (character >= L'0' && character <= L'9') ||
                          character == L'.' || character == L'_';
        if (safe) {
            result.push_back(character);
            previous_dash = false;
        } else if (!previous_dash && !result.empty()) {
            result.push_back(L'-');
            previous_dash = true;
        }
    }
    while (!result.empty() && result.back() == L'-') {
        result.pop_back();
    }
    return result.empty() ? L"unknown" : result;
}

std::filesystem::path version_directory(
    const std::filesystem::path& developer_root,
    const std::wstring_view version,
    const std::wstring_view build_id) {
    return developer_root / L"versions" /
        (sanitize_component(version) + L"-" + sanitize_component(build_id));
}

std::wstring current_marker_value(const std::filesystem::path& version_root) {
    return version_root.filename().wstring();
}

InstallerPayloadLayout locate_installer_payload(
    const std::filesystem::path& installer_path) {
    const auto root = installer_path.parent_path();
    const auto packaged_bin = root / L"bin";
    if (std::filesystem::is_regular_file(packaged_bin / L"PiInputTSF.dll")) {
        return {packaged_bin, root / L"data"};
    }
    return {root, root.parent_path() / L"data"};
}

PostInstallLaunchTargets make_post_install_launch_targets(
    const std::filesystem::path& program_root,
    const std::filesystem::path& user_data_directory) {
    return {
        program_root / L"bin" / L"PiInput-Settings.exe",
        user_data_directory / L"settings.ini",
        user_data_directory,
    };
}

}  // namespace piinput::windows::installer
