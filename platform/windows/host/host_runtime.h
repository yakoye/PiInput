#pragma once

#include "piinput/engine.h"
#include "piinput/english_lexicon.h"
#include "piinput/settings.h"
#include "piinput/settings_manager.h"
#include "piinput/symbols.h"
#include "piinput/windows_compat.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace piinput::windows {

struct HostRuntimePaths final {
    std::filesystem::path package_data;
    std::filesystem::path user_data;
};

class HostRuntime final {
public:
    [[nodiscard]] bool load(const HostRuntimePaths& paths, std::string& error) noexcept;
    void poll_settings_at_composition_boundary() noexcept;
    [[nodiscard]] bool save_user_model(std::string& error) const noexcept;

    [[nodiscard]] Engine& engine() noexcept;
    [[nodiscard]] EnglishLexicon& english() noexcept;
    [[nodiscard]] SymbolIndex& symbols() noexcept;
    [[nodiscard]] const SettingsSnapshot& settings() const noexcept;
    [[nodiscard]] const std::string& schema() const noexcept;
    [[nodiscard]] const std::filesystem::path& loaded_lexicon() const noexcept;
    [[nodiscard]] std::size_t prewarmed_prefix_count() const noexcept;

private:
    Engine engine_;
    EnglishLexicon english_;
    SymbolIndex symbols_;
    SettingsSnapshot settings_;
    std::string schema_{"flypy"};
    std::filesystem::path loaded_lexicon_;
    std::filesystem::path user_model_path_;
    std::size_t prewarmed_prefix_count_{};
    std::unique_ptr<SettingsManager> settings_manager_;
};

[[nodiscard]] HostRuntimePaths discover_host_runtime_paths(HINSTANCE module) noexcept;
[[nodiscard]] std::filesystem::path settings_executable_for_host(
    const std::filesystem::path& host_executable);

}  // namespace piinput::windows
