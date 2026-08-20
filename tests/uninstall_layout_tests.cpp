#include "uninstall_layout.h"
#include "stable_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_stub(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc).put('x');
}

}  // namespace

int main() {
    namespace installer = piinput::windows::installer;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"piinput-uninstall-layout-" + std::to_wstring(nonce));
    const auto local = fixture / L"Local App Data";
    const auto roaming = fixture / L"Roaming App Data";
    std::filesystem::create_directories(local);
    std::filesystem::create_directories(roaming);

    try {
        const auto layout = installer::make_uninstall_layout(local, roaming);
        require(installer::validate_uninstall_layout(layout),
            "generated uninstall layout must pass strict validation");
        require(layout.product_root == local / L"PiInput", "unexpected product root");
        // One fixed program directory; UserData sits beside it and must never
        // be inside anything an ordinary uninstall removes.
        require(layout.developer_root == layout.product_root / L"bin", "unexpected program root");
        require(layout.versions_root == layout.product_root / L"bin", "unexpected versions root");
        require(layout.user_data != layout.developer_root &&
                layout.developer_root != layout.product_root,
            "the program root must not be the product root");
        require(layout.user_data == layout.product_root / L"UserData", "unexpected user-data root");
        require(layout.stable_uninstaller ==
                layout.product_root / L"Uninstall" / L"PiInput-Uninstall.exe",
            "unexpected stable uninstaller path");

        const auto active = layout.versions_root / L"0.3.9-20260811-120000-42";
        std::filesystem::create_directories(active / L"bin");
        std::filesystem::create_directories(layout.developer_root);
        require(installer::write_runtime_marker_atomic(
                layout.current_marker, {active.filename().wstring(), 1U}),
            "unable to write valid current marker");
        require(installer::resolve_active_version(layout) == active,
            "valid current marker did not resolve the active version");

        // An unusable marker must never become a path, but it must not block the
        // uninstall either: refusing to run because a leftover file is wrong is
        // exactly how an installation becomes impossible to remove.
        for (const auto* hostile : {LR"({"version_id":"..\\outside","protocol_version":1})",
                                    LR"({"version_id":"C:\\outside","protocol_version":1})"}) {
            std::wofstream marker(layout.current_marker, std::ios::trunc);
            marker << hostile;
            marker.close();
            const auto resolved = installer::resolve_active_version(layout);
            require(resolved.has_value() && *resolved == layout.versions_root,
                "an unusable marker must fall back to the unified program directory");
        }

        // The marker names a version directory that is no longer there. Every
        // unified-layout install is in exactly this state, and it is what the
        // reported "active-version directory is missing" failure came from.
        {
            std::wofstream marker(layout.current_marker, std::ios::trunc);
            marker << LR"({"version_id":"0.7.3-20260819-151735-25768","protocol_version":3})";
        }
        const auto stale = installer::resolve_active_version(layout);
        require(stale.has_value() && *stale == layout.versions_root,
            "a marker naming a removed version directory must not fail the uninstall");

        // Tools sit directly in bin in the unified layout, and under
        // <version>/bin with the DLL in Shim in the older ones. Both have to be
        // found, or the uninstall cannot unregister what it is about to delete.
        write_stub(layout.developer_root / L"piinput-profile.exe");
        write_stub(layout.developer_root / L"PiInputTSF.dll");
        write_stub(layout.developer_root / L"PiInputHost.exe");
        const auto unified = installer::locate_uninstall_tools(layout, layout.versions_root);
        require(unified.profile == layout.developer_root / L"piinput-profile.exe",
            "unified layout must locate piinput-profile.exe in bin");
        require(unified.tsf_dll == layout.developer_root / L"PiInputTSF.dll",
            "unified layout must locate PiInputTSF.dll in bin");
        require(unified.host == layout.developer_root / L"PiInputHost.exe",
            "unified layout must locate PiInputHost.exe in bin");

        // A pre-unification install on its own: programs under <version>/bin
        // and the shim DLL in Shim, with nothing directly in bin. Its own
        // fixture, so the unified files written above cannot answer for it.
        const auto old_local = fixture / L"Old Local";
        const auto old_layout = installer::make_uninstall_layout(
            old_local, fixture / L"Old Roaming");
        const auto old_active = old_layout.versions_root / L"0.6.1-20260101-000000-1";
        write_stub(old_active / L"bin" / L"piinput-profile.exe");
        write_stub(old_active / L"bin" / L"PiInputHost.exe");
        write_stub(old_layout.developer_root / L"Shim" / L"PiInputTSF.dll");
        const auto versioned = installer::locate_uninstall_tools(old_layout, old_active);
        require(versioned.profile == old_active / L"bin" / L"piinput-profile.exe",
            "a versioned install must prefer the active version's own tools");
        require(versioned.tsf_dll == old_layout.developer_root / L"Shim" / L"PiInputTSF.dll",
            "a versioned install must find the DLL staged in Shim");

        // The oldest layout of all: everything under Runtime.
        const auto runtime_local = fixture / L"Runtime Local";
        const auto runtime_layout = installer::make_uninstall_layout(
            runtime_local, fixture / L"Runtime Roaming");
        write_stub(runtime_layout.product_root / L"Runtime" / L"bin" / L"piinput-profile.exe");
        write_stub(runtime_layout.product_root / L"Runtime" / L"Shim" / L"PiInputTSF.dll");
        const auto runtime_tools =
            installer::locate_uninstall_tools(runtime_layout, std::nullopt);
        require(runtime_tools.profile ==
                runtime_layout.product_root / L"Runtime" / L"bin" / L"piinput-profile.exe" &&
                runtime_tools.tsf_dll ==
                runtime_layout.product_root / L"Runtime" / L"Shim" / L"PiInputTSF.dll",
            "a Runtime-era install must still be locatable");

        const auto empty_layout = installer::make_uninstall_layout(
            fixture / L"Empty Local", fixture / L"Empty Roaming");
        const auto nothing = installer::locate_uninstall_tools(empty_layout, std::nullopt);
        require(!nothing.profile.has_value() && !nothing.tsf_dll.has_value() &&
                !nothing.host.has_value(),
            "an installation with no files must report no tools rather than fail");

        const auto preserved = installer::uninstall_roots(layout, false);
        for (const auto& root : preserved) {
            require(root != layout.user_data && root != layout.product_root,
                "an uninstall that preserves user data must not remove it");
        }
        const auto lists = [](const std::vector<std::filesystem::path>& roots,
                              const std::filesystem::path& wanted) {
            return std::find(roots.begin(), roots.end(), wanted) != roots.end();
        };
        require(lists(preserved, layout.developer_root) &&
                lists(preserved, layout.product_root / L"data") &&
                lists(preserved, layout.uninstall_root),
            "default uninstall roots must remove the program files");
        // The marker records install state, not user data. Leaving it behind is
        // what made the next uninstall resolve a version directory that no
        // longer existed and refuse to run.
        require(lists(preserved, layout.current_marker),
            "the version marker must go even when user data is preserved");
        // Older installs put the runtime under Runtime/ and Dev/. Those trees
        // outlived several layout changes and were never cleaned up.
        require(lists(preserved, layout.product_root / L"Runtime") &&
                lists(preserved, layout.product_root / L"Dev"),
            "historical runtime trees must be removed too");
        const auto removed = installer::uninstall_roots(layout, true);
        require(lists(removed, layout.user_data) && removed.back() == layout.product_root,
            "explicit full removal must remove user data and the product root last");

        const auto quoted = installer::quote_windows_argument(
            LR"(C:\Program Files\PiInput\PiInput-Uninstall.exe)");
        require(quoted == LR"("C:\Program Files\PiInput\PiInput-Uninstall.exe")",
            "Windows command path must be quoted");

        const auto registry = installer::make_uninstall_registry_values(
            layout,
            L"0.3.9-dev",
            active / L"bin" / L"PiInputTSF.dll");
        require(registry.display_name == L"PiInput", "unexpected uninstall display name");
        require(registry.display_version == L"0.3.9-dev", "unexpected uninstall display version");
        require(registry.install_location == layout.product_root.wstring(),
            "unexpected uninstall location");
        require(registry.uninstall_string ==
                installer::quote_windows_argument(layout.stable_uninstaller.wstring()),
            "interactive uninstall command must use the stable uninstaller");
        require(registry.quiet_uninstall_string ==
                registry.uninstall_string + L" --silent",
            "quiet uninstall command must add only --silent");
        require(registry.display_icon ==
                installer::quote_windows_argument((active / L"bin" / L"PiInputTSF.dll").wstring()) + L",0",
            "installed-app icon must use the embedded PiInputTSF icon");

        auto unsafe = layout;
        unsafe.developer_root = local.parent_path();
        require(!installer::validate_uninstall_layout(unsafe),
            "layout escaping the PiInput product root must be rejected");
    } catch (const std::exception& failure) {
        std::error_code ignored;
        std::filesystem::remove_all(fixture, ignored);
        // Rethrowing here terminated the process without naming the check that
        // failed, which is useless in CI output.
        std::cerr << "FAIL: " << failure.what() << std::endl;
        return 1;
    }

    std::error_code ignored;
    std::filesystem::remove_all(fixture, ignored);
    std::cout << "PiInput uninstall layout tests passed.\n";
    return 0;
}
