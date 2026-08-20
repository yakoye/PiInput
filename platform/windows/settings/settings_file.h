#pragma once

#include "piinput/settings.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace piinput::windows {

struct CandidateVisualFileSettings final {
    std::uint32_t font_size{16U};
    std::uint32_t window_height{40U};
    InputSchema schema{InputSchema::flypy};
    DefaultInputLanguage default_language{DefaultInputLanguage::chinese};
    std::uint32_t visible_rows{5U};

    bool operator==(const CandidateVisualFileSettings&) const = default;
};

[[nodiscard]] std::uint32_t step_numeric_setting(
    std::uint32_t value,
    int wheel_delta,
    std::uint32_t minimum,
    std::uint32_t maximum) noexcept;

[[nodiscard]] CandidateVisualFileSettings load_candidate_visual_settings(
    const std::filesystem::path& path,
    std::string& error) noexcept;

[[nodiscard]] bool save_candidate_visual_settings_atomic(
    const std::filesystem::path& path,
    CandidateVisualFileSettings settings,
    std::string& error) noexcept;

// The whole settings file, not just the handful of visual fields above. The
// settings window edits every option the engine reads, so it needs to load and
// store all of them; writing still rewrites only the keys it owns, leaving
// comments and anything it does not recognise untouched.
[[nodiscard]] SettingsSnapshot load_all_settings(
    const std::filesystem::path& path,
    std::string& error) noexcept;

[[nodiscard]] bool save_all_settings_atomic(
    const std::filesystem::path& path,
    const SettingsSnapshot& settings,
    std::string& error) noexcept;

}  // namespace piinput::windows
