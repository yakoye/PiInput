#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace piinput::windows::installer {

struct InstallerPayloadLayout {
    std::filesystem::path bin;
    std::filesystem::path data;
};

[[nodiscard]] std::wstring sanitize_component(std::wstring_view value);

[[nodiscard]] std::filesystem::path version_directory(
    const std::filesystem::path& developer_root,
    std::wstring_view version,
    std::wstring_view build_id);

[[nodiscard]] std::wstring current_marker_value(
    const std::filesystem::path& version_root);

[[nodiscard]] InstallerPayloadLayout locate_installer_payload(
    const std::filesystem::path& installer_path);

}  // namespace piinput::windows::installer
