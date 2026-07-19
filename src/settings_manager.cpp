#include "piinput/settings_manager.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace piinput {

SettingsManager::SettingsManager(std::filesystem::path path)
    : path_(std::move(path)),
      current_(std::make_shared<const SettingsSnapshot>(default_settings())) {
    std::error_code error;
    if (std::filesystem::exists(path_, error) && !error) {
        poll();
    }
}

std::shared_ptr<const SettingsSnapshot> SettingsManager::current() const noexcept {
    return current_.load(std::memory_order_acquire);
}

void SettingsManager::poll() {
    std::lock_guard lock(pending_mutex_);
    const auto base = current_.load(std::memory_order_acquire);
    if (!base->general.hot_reload) {
        return;
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        pending_.reset();
        last_content_.reset();
        last_errors_ = {"line 0 [document] key '<file>': unable to read settings file"};
        return;
    }
    std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        pending_.reset();
        last_content_.reset();
        last_errors_ = {"line 0 [document] key '<file>': unable to read settings file"};
        return;
    }
    if (last_content_ && *last_content_ == content) {
        return;
    }
    last_content_ = content;

    auto parsed = parse_settings_text(content, *base);
    last_errors_ = parsed.errors;
    if (parsed.settings == *base) {
        pending_.reset();
        return;
    }
    pending_ = std::move(parsed.settings);
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
