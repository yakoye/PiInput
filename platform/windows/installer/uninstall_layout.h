#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace piinput::windows::installer {

struct UninstallLayout {
    std::filesystem::path local_app_data;
    std::filesystem::path roaming_app_data;
    std::filesystem::path product_root;
    std::filesystem::path developer_root;
    std::filesystem::path versions_root;
    std::filesystem::path current_marker;
    std::filesystem::path user_data;
    std::filesystem::path uninstall_root;
    std::filesystem::path stable_uninstaller;
    std::filesystem::path start_menu_root;
};

struct UninstallRegistryValues {
    std::wstring display_name;
    std::wstring display_version;
    std::wstring publisher;
    std::wstring install_location;
    std::wstring display_icon;
    std::wstring uninstall_string;
    std::wstring quiet_uninstall_string;
};

[[nodiscard]] UninstallLayout make_uninstall_layout(
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& roaming_app_data);

[[nodiscard]] bool validate_uninstall_layout(const UninstallLayout& layout);

// The directory holding the active build's programs, whichever layout this
// installation was made with. Nothing here is an error: an uninstall that
// refuses to run because a leftover file disagrees with the files on disk is an
// uninstall the user cannot escape.
[[nodiscard]] std::optional<std::filesystem::path> resolve_active_version(
    const UninstallLayout& layout);

struct UninstallTools {
    std::optional<std::filesystem::path> profile;
    std::optional<std::filesystem::path> host;
    std::optional<std::filesystem::path> tsf_dll;
};

// The programs an uninstall needs in order to unregister itself, searched
// across every layout PiInput has shipped. Anything not found is reported as
// absent rather than treated as a broken installation.
[[nodiscard]] UninstallTools locate_uninstall_tools(
    const UninstallLayout& layout,
    const std::optional<std::filesystem::path>& active_version);

[[nodiscard]] std::vector<std::filesystem::path> uninstall_roots(
    const UninstallLayout& layout,
    bool remove_user_data);

[[nodiscard]] std::wstring quote_windows_argument(std::wstring_view value);

[[nodiscard]] UninstallRegistryValues make_uninstall_registry_values(
    const UninstallLayout& layout,
    std::wstring_view version,
    const std::filesystem::path& display_icon_module);

}  // namespace piinput::windows::installer
