#include "piinput/settings_manager.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace piinput {
namespace {

constexpr std::uintmax_t max_settings_file_size = 1024U * 1024U;

class DefaultSettingsFileReader final : public SettingsFileReader {
public:
    [[nodiscard]] SettingsFileMetadata metadata(
        const std::filesystem::path& path) override {
        return {std::filesystem::last_write_time(path), std::filesystem::file_size(path)};
    }

    [[nodiscard]] std::string read(
        const std::filesystem::path& path,
        const std::uintmax_t expected_size) override {
        if (expected_size > std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("settings file is too large");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("unable to open settings file");
        }
        std::string content(static_cast<std::size_t>(expected_size), '\0');
        if (!content.empty()) {
            input.read(content.data(), static_cast<std::streamsize>(content.size()));
        }
        if (input.bad() || static_cast<std::uintmax_t>(input.gcount()) != expected_size) {
            throw std::runtime_error("unable to read complete settings file");
        }
        return content;
    }
};

[[nodiscard]] std::shared_ptr<SettingsFileReader> default_file_reader() {
    return std::make_shared<DefaultSettingsFileReader>();
}

}  // namespace

SettingsPollThrottle::SettingsPollThrottle(const Duration interval) noexcept
    : interval_(interval > Duration::zero() ? interval : Duration::zero()) {}

bool SettingsPollThrottle::should_poll(const TimePoint now) const noexcept {
    return !last_poll_.has_value() ||
        (now >= *last_poll_ && now - *last_poll_ >= interval_);
}

void SettingsPollThrottle::mark_polled(const TimePoint now) noexcept {
    last_poll_ = now;
}

SettingsManager::SettingsManager(std::filesystem::path path)
    : path_(std::move(path)),
      log_path_(path_.parent_path() / "logs" / "settings.log"),
      file_reader_(default_file_reader()),
      current_(std::make_shared<const SettingsSnapshot>(default_settings())) {
    std::error_code error;
    if (std::filesystem::exists(path_, error) && !error) {
        poll();
    }
}

SettingsManager::SettingsManager(
    std::filesystem::path path,
    std::shared_ptr<SettingsFileReader> file_reader)
    : path_(std::move(path)),
      log_path_(path_.parent_path() / "logs" / "settings.log"),
      file_reader_(file_reader ? std::move(file_reader) : default_file_reader()),
      current_(std::make_shared<const SettingsSnapshot>(default_settings())) {
    poll();
}

void SettingsManager::set_errors(
    std::vector<std::string> errors,
    const bool new_round) noexcept {
    const bool should_log = new_round || errors != last_errors_;
    last_errors_ = std::move(errors);
    if (last_errors_.empty() || !should_log) {
        return;
    }
    try {
        std::error_code error;
        std::filesystem::create_directories(log_path_.parent_path(), error);
        if (error) {
            return;
        }
        std::ofstream output(log_path_, std::ios::binary | std::ios::app);
        if (!output) {
            return;
        }
        for (const auto& message : last_errors_) {
            output << message << '\n';
        }
    } catch (...) {
        // Logging is best-effort and must never affect the active settings snapshot.
    }
}

std::shared_ptr<const SettingsSnapshot> SettingsManager::current() const noexcept {
    return current_.load(std::memory_order_acquire);
}

void SettingsManager::poll() noexcept {
    try {
        std::lock_guard lock(pending_mutex_);
        const auto base = current_.load(std::memory_order_acquire);
        if (!base->general.hot_reload) {
            return;
        }

        bool new_round = false;
        try {
            const auto before = file_reader_->metadata(path_);
            const auto already_succeeded =
                last_successful_metadata_ && *last_successful_metadata_ == before;
            const auto already_failed_deterministically =
                last_deterministic_failure_metadata_ &&
                *last_deterministic_failure_metadata_ == before;
            if (already_succeeded || already_failed_deterministically) {
                return;
            }
            new_round = true;
            if (before.size > max_settings_file_size) {
                pending_.reset();
                last_deterministic_failure_metadata_ = before;
                set_errors(
                    {"line 0 [document] key '<file-size>': settings file too large"}, true);
                return;
            }

            auto content = file_reader_->read(path_, before.size);
            const auto after = file_reader_->metadata(path_);
            if (after != before) {
                pending_.reset();
                set_errors(
                    {"line 0 [document] key '<unstable-file>': settings file changed while reading"},
                    true);
                return;
            }
            if (content.size() != before.size) {
                pending_.reset();
                set_errors(
                    {"line 0 [document] key '<file>': unable to read settings file"}, true);
                return;
            }

            auto parsed = parse_settings_text(content, *base);
            set_errors(std::move(parsed.errors), true);
            if (parsed.document_fatal) {
                pending_.reset();
                last_deterministic_failure_metadata_ = before;
                return;
            }
            last_successful_metadata_ = before;
            last_deterministic_failure_metadata_.reset();
            if (parsed.settings == *base) {
                pending_.reset();
                return;
            }
            pending_ = std::move(parsed.settings);
        } catch (const std::exception&) {
            pending_.reset();
            set_errors(
                {"line 0 [document] key '<file>': unable to read settings file"}, new_round);
        } catch (...) {
            pending_.reset();
            set_errors(
                {"line 0 [document] key '<file>': unable to read settings file"}, new_round);
        }
    } catch (...) {
        // File, allocation, and logging failures must not escape into the IME host.
    }
}

void SettingsManager::apply_pending_at_composition_boundary() {
    std::lock_guard lock(pending_mutex_);
    if (!pending_) {
        return;
    }
    auto next = std::move(*pending_);
    pending_.reset();
    const auto active = current_.load(std::memory_order_acquire);
    next.generation = active->generation + 1U;
    current_.store(
        std::make_shared<const SettingsSnapshot>(std::move(next)),
        std::memory_order_release);
}

std::vector<std::string> SettingsManager::last_errors() const {
    std::lock_guard lock(pending_mutex_);
    return last_errors_;
}

}  // namespace piinput
