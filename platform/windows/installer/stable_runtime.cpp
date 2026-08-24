#include "stable_runtime.h"

#include "piinput/windows_compat.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <regex>
#include <system_error>

#include <aclapi.h>
#include <sddl.h>

namespace piinput::windows::installer {

namespace {

// Store-packaged applications -- the ChatGPT/Codex desktop app is one -- run
// inside an AppContainer and can only load a file that grants read+execute to
// ALL APPLICATION PACKAGES (S-1-15-2-1). Without this the Shim simply never
// loads there, Windows shows the profile as unavailable, and the input method
// cannot even be switched to. Every shipping IME grants it; SogouTSF.ime does.
bool grant_app_container_read(const std::filesystem::path& target) noexcept {
    PSID app_packages = nullptr;
    if (ConvertStringSidToSidW(L"S-1-15-2-1", &app_packages) == FALSE ||
        app_packages == nullptr) {
        return false;
    }
    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR>(app_packages);

    PACL existing = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring path = target.wstring();
    bool granted = false;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &existing, nullptr,
            &descriptor) == ERROR_SUCCESS) {
        PACL updated = nullptr;
        if (SetEntriesInAclW(1U, &access, existing, &updated) == ERROR_SUCCESS &&
            updated != nullptr) {
            granted = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, updated, nullptr) ==
                ERROR_SUCCESS;
            LocalFree(updated);
        }
        if (descriptor != nullptr) LocalFree(descriptor);
    }
    LocalFree(app_packages);
    return granted;
}

[[nodiscard]] bool safe_version_id(const std::wstring_view value) noexcept {
    if (value.empty() || value == L"." || value == L"..") return false;
    for (const wchar_t character : value) {
        const bool safe = (character >= L'a' && character <= L'z') ||
                          (character >= L'A' && character <= L'Z') ||
                          (character >= L'0' && character <= L'9') ||
                          character == L'.' || character == L'_' || character == L'-';
        if (!safe) return false;
    }
    return true;
}

}  // namespace

std::optional<StableRuntimeLayout> make_stable_runtime_layout(
    const std::filesystem::path& piinput_root,
    const std::wstring_view version_id) noexcept {
    if (piinput_root.empty() || !safe_version_id(version_id)) return std::nullopt;
    try {
        // One fixed directory. The previous layout kept a permanent Shim under
        // Runtime\Shim and a per-version tree under Runtime\versions, plus an
        // older Dev tree beside them. Every upgrade added a directory that
        // nothing removed, and a registration captured while a versioned path
        // was current became a dead link the moment that version was replaced --
        // which is exactly how the ChatGPT desktop app ended up pointing at a
        // v0.5.5 path that no longer existed. Everything now lives in bin, at a
        // path that never changes, and upgrades overwrite in place.
        StableRuntimeLayout result;
        result.root = piinput_root;
        result.shim_directory = result.root / L"bin";
        result.shim_dll = result.shim_directory / L"PiInputTSF.dll";
        result.versions_directory = result.shim_directory;
        result.version_root = result.shim_directory;
        result.current_marker = result.root / L"current.json";
        result.rollback_marker = result.root / L"rollback.json";
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

bool write_runtime_marker_atomic(
    const std::filesystem::path& path,
    const RuntimeMarker& marker) noexcept {
    if (!safe_version_id(marker.version_id) || marker.protocol_version == 0U) return false;
    try {
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.wstring() + L".tmp";
        {
            std::wofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << L"{\n  \"version_id\": \"" << marker.version_id
                   << L"\",\n  \"protocol_version\": " << marker.protocol_version
                   << L"\n}\n";
            output.flush();
            if (!output) return false;
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<RuntimeMarker> read_runtime_marker(
    const std::filesystem::path& path) noexcept {
    try {
        std::wifstream input(path, std::ios::binary);
        if (!input) return std::nullopt;
        const std::wstring content{
            std::istreambuf_iterator<wchar_t>(input),
            std::istreambuf_iterator<wchar_t>()};
        static const std::wregex pattern(
            LR"REGEX(^\s*\{\s*"version_id"\s*:\s*"([A-Za-z0-9._-]+)"\s*,\s*"protocol_version"\s*:\s*([0-9]+)\s*\}\s*$)REGEX");
        std::wsmatch match;
        if (!std::regex_match(content, match, pattern)) return std::nullopt;
        const std::wstring id = match[1].str();
        const unsigned long protocol = std::stoul(match[2].str());
        if (!safe_version_id(id) || protocol == 0UL || protocol > UINT32_MAX) return std::nullopt;
        return RuntimeMarker{id, static_cast<std::uint32_t>(protocol)};
    } catch (...) {
        return std::nullopt;
    }
}

bool files_are_identical(
    const std::filesystem::path& first,
    const std::filesystem::path& second) noexcept {
    try {
        if (!std::filesystem::is_regular_file(first) ||
            !std::filesystem::is_regular_file(second) ||
            std::filesystem::file_size(first) != std::filesystem::file_size(second)) {
            return false;
        }
        std::ifstream left(first, std::ios::binary);
        std::ifstream right(second, std::ios::binary);
        if (!left || !right) return false;
        std::array<char, 64U * 1024U> left_buffer{};
        std::array<char, 64U * 1024U> right_buffer{};
        while (left && right) {
            left.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
            right.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
            const std::streamsize left_count = left.gcount();
            const std::streamsize right_count = right.gcount();
            if (left_count != right_count ||
                !std::equal(left_buffer.begin(), left_buffer.begin() + left_count,
                    right_buffer.begin())) {
                return false;
            }
        }
        return left.eof() && right.eof();
    } catch (...) {
        return false;
    }
}

bool can_reuse_registered_stable_shim(
    const std::filesystem::path& registered_dll,
    const std::filesystem::path& stable_shim,
    const std::filesystem::path& packaged_shim) noexcept {
    if (registered_dll.empty() || stable_shim.empty() || packaged_shim.empty()) return false;
    try {
        const std::wstring registered = registered_dll.lexically_normal().wstring();
        const std::wstring stable = stable_shim.lexically_normal().wstring();
        if (CompareStringOrdinal(
                registered.c_str(), static_cast<int>(registered.size()),
                stable.c_str(), static_cast<int>(stable.size()), TRUE) != CSTR_EQUAL) {
            return false;
        }
        return files_are_identical(packaged_shim, stable_shim);
    } catch (...) {
        return false;
    }
}

std::filesystem::path stable_shim_registration_fallback(
    const std::filesystem::path& stable_shim,
    const std::uint32_t replacement_error) noexcept {
    if (replacement_error != ERROR_ACCESS_DENIED &&
        replacement_error != ERROR_SHARING_VIOLATION) {
        return {};
    }
    try {
        std::error_code error;
        return std::filesystem::is_regular_file(stable_shim, error) && !error
            ? stable_shim
            : std::filesystem::path{};
    } catch (...) {
        return {};
    }
}

// Retiring a locked Shim is unavoidable -- it is mapped into every running
// application -- but nothing ever removed the retired copies, and eleven of
// them had accumulated. Each install clears the ones it can and asks Windows to
// delete the rest at the next restart, so they cannot pile up again.
void purge_retired_shims(const std::filesystem::path& shim_directory) noexcept {
    try {
        std::error_code error;
        if (!std::filesystem::is_directory(shim_directory, error) || error) return;
        for (const auto& entry :
                std::filesystem::directory_iterator(shim_directory, error)) {
            if (error) return;
            if (!entry.is_regular_file(error) || error) continue;
            const auto name = entry.path().filename().wstring();
            if (name.find(L".retired.") == std::wstring::npos) continue;
            std::error_code removed;
            if (std::filesystem::remove(entry.path(), removed) && !removed) continue;
            // Still mapped by a running application; let the loader release it.
            (void)MoveFileExW(entry.path().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    } catch (...) {
    }
}

StableShimRefreshResult refresh_stable_shim(
    const std::filesystem::path& source,
    const std::filesystem::path& stable_shim,
    const std::wstring_view refresh_id) noexcept {
    StableShimRefreshResult result{.path = stable_shim};
    try {
        if (!safe_version_id(refresh_id) || !std::filesystem::is_regular_file(source)) {
            result.error = ERROR_INVALID_PARAMETER;
            return result;
        }
        if (files_are_identical(source, stable_shim)) {
            result.exact_bytes = true;
            // An install that changed nothing can still be repairing a shim
            // whose permissions block packaged applications.
            (void)grant_app_container_read(stable_shim.parent_path());
            (void)grant_app_container_read(stable_shim);
            return result;
        }
        std::filesystem::create_directories(stable_shim.parent_path());
        (void)grant_app_container_read(stable_shim.parent_path());
        purge_retired_shims(stable_shim.parent_path());
        const std::filesystem::path temporary =
            stable_shim.wstring() + L".refresh." + std::wstring(refresh_id) + L".tmp";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::copy_file(
            source, temporary, std::filesystem::copy_options::overwrite_existing);

        if (MoveFileExW(temporary.c_str(), stable_shim.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            result.error = GetLastError();
            if ((result.error == ERROR_ACCESS_DENIED ||
                    result.error == ERROR_SHARING_VIOLATION) &&
                std::filesystem::is_regular_file(stable_shim)) {
                const std::filesystem::path retired =
                    stable_shim.wstring() + L".retired." + std::wstring(refresh_id);
                std::filesystem::remove(retired, ignored);
                if (MoveFileExW(stable_shim.c_str(), retired.c_str(),
                        MOVEFILE_WRITE_THROUGH) != FALSE) {
                    if (MoveFileExW(temporary.c_str(), stable_shim.c_str(),
                            MOVEFILE_WRITE_THROUGH) != FALSE) {
                        result.retired_path = retired;
                        result.error = ERROR_SUCCESS;
                        result.exact_bytes = files_are_identical(source, stable_shim);
                        if (result.exact_bytes) {
                            (void)MoveFileExW(
                                retired.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
                            return result;
                        }
                    }
                    (void)MoveFileExW(retired.c_str(), stable_shim.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                }
            }
            std::filesystem::remove(temporary, ignored);
            return result;
        }
        (void)grant_app_container_read(stable_shim);
        result.error = ERROR_SUCCESS;
        result.exact_bytes = files_are_identical(source, stable_shim);
        return result;
    } catch (...) {
        if (result.error == ERROR_SUCCESS) result.error = ERROR_WRITE_FAULT;
        return result;
    }
}

std::filesystem::path resolve_current_host(
    const std::filesystem::path& runtime_root) noexcept {
    try {
        // A fixed path, so the Host is found without consulting a version
        // marker that can disagree with what is actually on disk.
        const auto host = runtime_root / L"bin" / L"PiInputHost.exe";
        std::error_code error;
        return std::filesystem::is_regular_file(host, error) && !error
            ? host
            : std::filesystem::path{};
    } catch (...) {
        return {};
    }
}

std::filesystem::path machine_runtime_root(
    const std::filesystem::path& program_files) noexcept {
    try {
        if (program_files.empty() || !program_files.is_absolute()) return {};
        return (program_files / L"PiInput" / L"Runtime").lexically_normal();
    } catch (...) {
        return {};
    }
}

std::filesystem::path machine_shim_path(
    const std::filesystem::path& program_files) noexcept {
    const auto root = machine_runtime_root(program_files);
    return root.empty() ? std::filesystem::path{}
                        : root / L"Shim" / L"PiInputTSF.dll";
}

bool is_safe_machine_runtime_root(
    const std::filesystem::path& runtime_root,
    const std::filesystem::path& program_files) noexcept {
    try {
        const auto expected = machine_runtime_root(program_files);
        if (expected.empty() || runtime_root.empty()) return false;
        const std::wstring actual_text = runtime_root.lexically_normal().wstring();
        const std::wstring expected_text = expected.wstring();
        return CompareStringOrdinal(
            actual_text.c_str(), static_cast<int>(actual_text.size()),
            expected_text.c_str(), static_cast<int>(expected_text.size()), TRUE) == CSTR_EQUAL;
    } catch (...) {
        return false;
    }
}

}  // namespace piinput::windows::installer
