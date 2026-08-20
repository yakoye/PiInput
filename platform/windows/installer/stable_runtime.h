#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace piinput::windows::installer {

struct StableRuntimeLayout final {
    std::filesystem::path root;
    std::filesystem::path shim_directory;
    std::filesystem::path shim_dll;
    std::filesystem::path versions_directory;
    std::filesystem::path version_root;
    std::filesystem::path current_marker;
    std::filesystem::path rollback_marker;
};

struct RuntimeMarker final {
    std::wstring version_id;
    std::uint32_t protocol_version{1U};

    bool operator==(const RuntimeMarker&) const = default;
};

struct StableShimRefreshResult final {
    std::filesystem::path path;
    std::filesystem::path retired_path;
    std::uint32_t error{};
    bool exact_bytes{};
};

[[nodiscard]] std::optional<StableRuntimeLayout> make_stable_runtime_layout(
    const std::filesystem::path& piinput_root,
    std::wstring_view version_id) noexcept;

[[nodiscard]] bool write_runtime_marker_atomic(
    const std::filesystem::path& path,
    const RuntimeMarker& marker) noexcept;

[[nodiscard]] std::optional<RuntimeMarker> read_runtime_marker(
    const std::filesystem::path& path) noexcept;

[[nodiscard]] bool files_are_identical(
    const std::filesystem::path& first,
    const std::filesystem::path& second) noexcept;

[[nodiscard]] bool can_reuse_registered_stable_shim(
    const std::filesystem::path& registered_dll,
    const std::filesystem::path& stable_shim,
    const std::filesystem::path& packaged_shim) noexcept;

[[nodiscard]] std::filesystem::path stable_shim_registration_fallback(
    const std::filesystem::path& stable_shim,
    std::uint32_t replacement_error) noexcept;

[[nodiscard]] StableShimRefreshResult refresh_stable_shim(
    const std::filesystem::path& source,
    const std::filesystem::path& stable_shim,
    std::wstring_view refresh_id) noexcept;

[[nodiscard]] std::filesystem::path resolve_current_host(
    const std::filesystem::path& runtime_root) noexcept;

}  // namespace piinput::windows::installer
