#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace piinput::windows::installer {

[[nodiscard]] std::wstring sanitize_component(std::wstring_view value);

[[nodiscard]] std::filesystem::path version_directory(
    const std::filesystem::path& developer_root,
    std::wstring_view version,
    std::wstring_view build_id);

[[nodiscard]] std::wstring current_marker_value(
    const std::filesystem::path& version_root);

}  // namespace piinput::windows::installer
