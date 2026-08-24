#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace piinput::windows::installer {

struct InstallerPayloadLayout {
    std::filesystem::path bin;
    std::filesystem::path data;
};

struct PostInstallLaunchTargets {
    std::filesystem::path settings_executable;
    std::filesystem::path settings_file;
    std::filesystem::path user_data_directory;
};

struct ProfileInstallCommand {
    std::wstring_view arguments;
    std::string_view failure_message;
    bool enables_user_keyboard;
    unsigned int max_attempts;
    unsigned int retry_delay_ms;
};

[[nodiscard]] std::array<ProfileInstallCommand, 3> profile_install_commands();

[[nodiscard]] std::wstring sanitize_component(std::wstring_view value);

[[nodiscard]] std::filesystem::path version_directory(
    const std::filesystem::path& developer_root,
    std::wstring_view version,
    std::wstring_view build_id);

[[nodiscard]] std::wstring current_marker_value(
    const std::filesystem::path& version_root);

[[nodiscard]] InstallerPayloadLayout locate_installer_payload(
    const std::filesystem::path& installer_path);

[[nodiscard]] PostInstallLaunchTargets make_post_install_launch_targets(
    const std::filesystem::path& program_root,
    const std::filesystem::path& user_data_directory);

}  // namespace piinput::windows::installer
