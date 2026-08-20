#include "uninstall_layout.h"
#include "stable_runtime.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace piinput::windows::installer {
namespace {

[[nodiscard]] std::filesystem::path normalized(const std::filesystem::path& value) {
    std::error_code error;
    auto result = std::filesystem::absolute(value, error).lexically_normal();
    if (error) {
        return value.lexically_normal();
    }
#ifdef _WIN32
    std::wstring text = result.wstring();
    std::transform(text.begin(), text.end(), text.begin(), [](const wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return std::filesystem::path(std::move(text));
#else
    return result;
#endif
}

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return normalized(left) == normalized(right);
}

[[nodiscard]] bool valid_marker_component(const std::wstring& value) {
    if (value.empty() || value == L"." || value == L".." ||
        value.find(L'/') != std::wstring::npos || value.find(L'\\') != std::wstring::npos) {
        return false;
    }
    const std::filesystem::path component(value);
    return !component.is_absolute() && !component.has_root_name() &&
           !component.has_root_directory() && component.filename() == component;
}

}  // namespace

UninstallLayout make_uninstall_layout(
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& roaming_app_data) {
    UninstallLayout layout;
    layout.local_app_data = local_app_data;
    layout.roaming_app_data = roaming_app_data;
    layout.product_root = local_app_data / L"PiInput";
    // Matches the unified install layout: bin and data under the product root,
    // with no Runtime or Dev tree beside them. developer_root names the program
    // directory only -- pointing it at the product root would make an ordinary
    // uninstall delete UserData along with the program.
    layout.developer_root = layout.product_root / L"bin";
    layout.versions_root = layout.product_root / L"bin";
    layout.current_marker = layout.product_root / L"current.json";
    layout.user_data = layout.product_root / L"UserData";
    layout.uninstall_root = layout.product_root / L"Uninstall";
    layout.stable_uninstaller = layout.uninstall_root / L"PiInput-Uninstall.exe";
    layout.start_menu_root = roaming_app_data / L"Microsoft" / L"Windows" /
        L"Start Menu" / L"Programs" / L"PiInput";
    return layout;
}

bool validate_uninstall_layout(const UninstallLayout& layout) {
    if (!layout.local_app_data.is_absolute() || !layout.roaming_app_data.is_absolute()) {
        return false;
    }
    const auto expected = make_uninstall_layout(layout.local_app_data, layout.roaming_app_data);
    return same_path(layout.product_root, expected.product_root) &&
           same_path(layout.developer_root, expected.developer_root) &&
           same_path(layout.versions_root, expected.versions_root) &&
           same_path(layout.current_marker, expected.current_marker) &&
           same_path(layout.user_data, expected.user_data) &&
           same_path(layout.uninstall_root, expected.uninstall_root) &&
           same_path(layout.stable_uninstaller, expected.stable_uninstaller) &&
           same_path(layout.start_menu_root, expected.start_menu_root);
}

std::optional<std::filesystem::path> resolve_active_version(const UninstallLayout& layout) {
    if (!validate_uninstall_layout(layout)) {
        throw std::runtime_error("Unsafe PiInput uninstall layout");
    }
    // An installation made before the layout was unified has a version marker
    // and a per-version directory. The unified layout keeps the marker but has
    // no such directory -- everything sits in bin -- so a marker that does not
    // resolve is the normal case, not a failure. An unreadable or hostile
    // marker takes the same path: it never becomes a directory name, and it
    // never stops the uninstall.
    const auto unified = [&]() -> std::optional<std::filesystem::path> {
        std::error_code error;
        if (std::filesystem::is_directory(layout.versions_root, error) && !error) {
            return layout.versions_root;
        }
        return std::nullopt;
    };
    if (!std::filesystem::is_regular_file(layout.current_marker)) {
        return unified();
    }
    const auto marker = read_runtime_marker(layout.current_marker);
    if (!marker.has_value() || !valid_marker_component(marker->version_id)) {
        return unified();
    }
    const auto versioned = layout.versions_root / marker->version_id;
    if (!std::filesystem::is_directory(versioned)) {
        return unified();
    }
    return versioned;
}

UninstallTools locate_uninstall_tools(
    const UninstallLayout& layout,
    const std::optional<std::filesystem::path>& active_version) {
    if (!validate_uninstall_layout(layout)) {
        throw std::runtime_error("Unsafe PiInput uninstall layout");
    }
    // Every directory PiInput has ever put its programs in, newest first. The
    // active version's own bin comes first so a versioned install unregisters
    // with its own build rather than whatever else is lying around.
    std::vector<std::filesystem::path> directories;
    if (active_version.has_value()) {
        directories.push_back(*active_version / L"bin");
        directories.push_back(*active_version);
    }
    directories.push_back(layout.developer_root);
    directories.push_back(layout.developer_root / L"Shim");
    const auto runtime = layout.product_root / L"Runtime";
    directories.push_back(runtime / L"Shim");
    directories.push_back(runtime / L"bin");
    directories.push_back(layout.product_root / L"Dev");
    directories.push_back(layout.product_root / L"Dev" / L"bin");

    const auto first_existing = [&directories](const std::wstring& name)
        -> std::optional<std::filesystem::path> {
        for (const auto& directory : directories) {
            const auto candidate = directory / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return candidate;
            }
        }
        return std::nullopt;
    };

    UninstallTools tools;
    tools.profile = first_existing(L"piinput-profile.exe");
    tools.host = first_existing(L"PiInputHost.exe");
    tools.tsf_dll = first_existing(L"PiInputTSF.dll");
    return tools;
}

std::vector<std::filesystem::path> uninstall_roots(
    const UninstallLayout& layout,
    const bool remove_user_data) {
    if (!validate_uninstall_layout(layout)) {
        throw std::runtime_error("Unsafe PiInput uninstall layout");
    }
    std::vector<std::filesystem::path> roots{layout.developer_root};
    roots.push_back(layout.product_root / L"data");
    // Runtime and Dev are where older releases kept the shim and the versioned
    // builds. They survived several layout changes untouched, so an uninstall
    // that only knows the current layout leaves most of the product behind.
    roots.push_back(layout.product_root / L"Runtime");
    roots.push_back(layout.product_root / L"Dev");
    if (remove_user_data) {
        roots.push_back(layout.user_data);
    }
    roots.push_back(layout.uninstall_root);
    // The marker records install state, not user data. Leaving it behind is
    // what made the next uninstall resolve a version directory that no longer
    // existed and refuse to run at all.
    roots.push_back(layout.current_marker);
    if (remove_user_data) {
        // Remove the validated product root last so full uninstall also clears
        // migration backups and any other installer-owned bookkeeping files.
        roots.push_back(layout.product_root);
    }
    return roots;
}

std::wstring quote_windows_argument(const std::wstring_view value) {
    std::wstring result(1U, L'"');
    std::size_t backslashes = 0U;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0U;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

UninstallRegistryValues make_uninstall_registry_values(
    const UninstallLayout& layout,
    const std::wstring_view version,
    const std::filesystem::path& display_icon_module) {
    if (!validate_uninstall_layout(layout)) {
        throw std::runtime_error("Unsafe PiInput uninstall layout");
    }
    UninstallRegistryValues values;
    values.display_name = L"PiInput";
    values.display_version = version;
    values.publisher = L"PiInput Project";
    values.install_location = layout.product_root.wstring();
    values.display_icon = quote_windows_argument(display_icon_module.wstring()) + L",0";
    values.uninstall_string = quote_windows_argument(layout.stable_uninstaller.wstring());
    values.quiet_uninstall_string = values.uninstall_string + L" --silent";
    return values;
}

}  // namespace piinput::windows::installer
