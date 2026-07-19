#pragma once

#include "piinput/settings.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace piinput {

class SettingsManager final {
public:
    explicit SettingsManager(std::filesystem::path path);

    [[nodiscard]] std::shared_ptr<const SettingsSnapshot> current() const noexcept;
    void poll();
    void apply_pending_at_composition_boundary();
    [[nodiscard]] std::vector<std::string> last_errors() const;

private:
    std::filesystem::path path_;
    std::atomic<std::shared_ptr<const SettingsSnapshot>> current_;
    mutable std::mutex pending_mutex_;
    std::optional<SettingsSnapshot> pending_;
    std::optional<std::string> last_content_;
    std::vector<std::string> last_errors_;
};

}  // namespace piinput
