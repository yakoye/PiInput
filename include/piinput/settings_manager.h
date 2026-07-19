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

struct SettingsFileMetadata {
    std::filesystem::file_time_type last_write_time;
    std::uintmax_t size{0U};

    bool operator==(const SettingsFileMetadata&) const = default;
};

class SettingsFileReader {
public:
    virtual ~SettingsFileReader() = default;
    [[nodiscard]] virtual SettingsFileMetadata metadata(
        const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual std::string read(
        const std::filesystem::path& path,
        std::uintmax_t expected_size) = 0;
};

class SettingsManager final {
public:
    explicit SettingsManager(std::filesystem::path path);
    SettingsManager(
        std::filesystem::path path,
        std::shared_ptr<SettingsFileReader> file_reader);

    [[nodiscard]] std::shared_ptr<const SettingsSnapshot> current() const noexcept;
    void poll() noexcept;
    void apply_pending_at_composition_boundary();
    [[nodiscard]] std::vector<std::string> last_errors() const;

private:
    void set_errors(std::vector<std::string> errors, bool new_round) noexcept;

    std::filesystem::path path_;
    std::filesystem::path log_path_;
    std::shared_ptr<SettingsFileReader> file_reader_;
    std::atomic<std::shared_ptr<const SettingsSnapshot>> current_;
    mutable std::mutex pending_mutex_;
    std::optional<SettingsSnapshot> pending_;
    std::optional<SettingsFileMetadata> last_metadata_;
    std::vector<std::string> last_errors_;
};

}  // namespace piinput
