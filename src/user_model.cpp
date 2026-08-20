#include "piinput/user_model.h"
#include "piinput/windows_compat.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace piinput {
namespace {

std::atomic<std::uint64_t> user_model_temporary_counter{};

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(line.data() + start, line.size() - start);
            break;
        }
        fields.emplace_back(line.data() + start, tab - start);
        start = tab + 1U;
    }
    return fields;
}

[[nodiscard]] std::uint64_t now_seconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

template <typename T>
[[nodiscard]] T parse_integer(const std::string_view value, const std::size_t line_number) {
    T output{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::runtime_error("Invalid user model number at line " + std::to_string(line_number));
    }
    return output;
}

[[nodiscard]] std::filesystem::path unique_temporary_path(
    const std::filesystem::path& path) {
    auto temporary = path;
    temporary += ".tmp." + std::to_string(
#ifdef _WIN32
        static_cast<std::uint64_t>(GetCurrentProcessId())) + "." +
#else
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count())) + "." +
#endif
        std::to_string(++user_model_temporary_counter);
    return temporary;
}

[[nodiscard]] bool replace_file_atomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error_message) noexcept {
#ifdef _WIN32
    if (MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    error_message = "Windows error " + std::to_string(GetLastError());
    return false;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) return true;
    error_message = error.message();
    return false;
#endif
}

}  // namespace

UserModel::UserModel(const UserModel& other) {
    std::shared_lock lock(other.mutex_);
    entries_by_pinyin_ = other.entries_by_pinyin_;
    entry_count_ = other.entry_count_;
}

UserModel& UserModel::operator=(const UserModel& other) {
    if (this == &other) return *this;
    std::shared_lock other_lock(other.mutex_);
    std::unique_lock self_lock(mutex_);
    entries_by_pinyin_ = other.entries_by_pinyin_;
    entry_count_ = other.entry_count_;
    return *this;
}

std::uint8_t UserModel::learning_tier(const std::uint32_t count) noexcept {
    return static_cast<std::uint8_t>((std::min)(count, 3U));
}

const UserModel::Entry* UserModel::find_entry(
    const std::string_view pinyin,
    const std::string_view word) const noexcept {
    const auto bucket = entries_by_pinyin_.find(std::string(pinyin));
    if (bucket == entries_by_pinyin_.end()) return nullptr;
    const auto found = bucket->second.find(std::string(word));
    return found == bucket->second.end() ? nullptr : &found->second;
}

std::vector<std::string> UserModel::load(const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    entries_by_pinyin_.clear();
    entry_count_ = 0U;
    std::vector<std::string> diagnostics;
    if (!std::filesystem::exists(path)) {
        return diagnostics;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open user model: " + path.string());
    }
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto fields = split_tabs(line);
        if (fields.size() >= 2U && fields[0] == "pinyin" && fields[1] == "word") {
            continue;
        }
        try {
            if (fields.size() < 4U || fields.size() > 7U ||
                fields[0].empty() || fields[1].empty()) {
                throw std::runtime_error("invalid field count or empty key");
            }
            Entry entry{
                parse_integer<std::uint32_t>(fields[2], line_number),
                parse_integer<std::uint64_t>(fields[3], line_number),
            };
            if (fields.size() >= 5U) entry.pinned =
                parse_integer<unsigned int>(fields[4], line_number) != 0U;
            if (fields.size() >= 6U) entry.suppressed =
                parse_integer<unsigned int>(fields[5], line_number) != 0U;
            if (fields.size() >= 7U) entry.user_created =
                parse_integer<unsigned int>(fields[6], line_number) != 0U;
            auto& bucket = entries_by_pinyin_[std::string(fields[0])];
            const auto [position, inserted] = bucket.insert_or_assign(
                std::string(fields[1]), entry);
            (void)position;
            if (inserted) ++entry_count_;
        } catch (const std::exception& exception) {
            diagnostics.push_back(
                "line " + std::to_string(line_number) + ": " + exception.what());
        }
    }
    return diagnostics;
}

void UserModel::save(const std::filesystem::path& path) const {
    struct SerializedEntry final {
        std::string pinyin;
        std::string word;
        Entry entry;
    };
    std::vector<SerializedEntry> snapshot;
    {
        std::shared_lock lock(mutex_);
        snapshot.reserve(entry_count_);
        for (const auto& [pinyin, bucket] : entries_by_pinyin_) {
            for (const auto& [word, entry] : bucket) {
                snapshot.push_back({pinyin, word, entry});
            }
        }
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.pinyin < right.pinyin ||
            (left.pinyin == right.pinyin && left.word < right.word);
    });
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = unique_temporary_path(path);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create user model: " + temporary.string());
    }
    output << "# PiInput user selection model\n"
           << "pinyin\tword\tcount\tlast_used\tpinned\tsuppressed\tuser_created\n";

    for (const auto& row : snapshot) {
        output << row.pinyin << '\t' << row.word << '\t'
               << row.entry.count << '\t' << row.entry.last_used << '\t'
               << (row.entry.pinned ? 1 : 0) << '\t'
               << (row.entry.suppressed ? 1 : 0) << '\t'
               << (row.entry.user_created ? 1 : 0) << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Failed while writing user model");
    }

    std::string replace_error;
    if (!replace_file_atomically(temporary, path, replace_error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("Cannot replace user model: " + replace_error);
    }
}

void UserModel::record_selection(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    auto& bucket = entries_by_pinyin_[pinyin];
    const auto [position, inserted] = bucket.try_emplace(word);
    if (inserted) ++entry_count_;
    auto& entry = position->second;
    if (entry.count < std::numeric_limits<std::uint32_t>::max()) {
        ++entry.count;
    }
    entry.suppressed = false;
    entry.last_used = now_seconds();
}

void UserModel::record_composed_phrase(
    const std::string& pinyin,
    const std::string& word) {
    std::unique_lock lock(mutex_);
    auto& bucket = entries_by_pinyin_[pinyin];
    const auto [position, inserted] = bucket.try_emplace(word);
    if (inserted) ++entry_count_;
    auto& entry = position->second;
    if (entry.count < std::numeric_limits<std::uint32_t>::max()) ++entry.count;
    entry.last_used = now_seconds();
    entry.suppressed = false;
    entry.user_created = true;
}

void UserModel::remove(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    const auto bucket = entries_by_pinyin_.find(pinyin);
    if (bucket == entries_by_pinyin_.end()) return;
    if (bucket->second.erase(word) != 0U) --entry_count_;
    if (bucket->second.empty()) entries_by_pinyin_.erase(bucket);
}

void UserModel::remove_learning(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    const auto bucket = entries_by_pinyin_.find(pinyin);
    if (bucket == entries_by_pinyin_.end()) return;
    const auto found = bucket->second.find(word);
    if (found == bucket->second.end()) return;
    if (!found->second.suppressed) {
        bucket->second.erase(found);
        --entry_count_;
        if (bucket->second.empty()) entries_by_pinyin_.erase(bucket);
        return;
    }
    found->second.count = 0U;
    found->second.last_used = now_seconds();
    found->second.pinned = false;
    found->second.user_created = false;
}

void UserModel::pin(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    auto& bucket = entries_by_pinyin_[pinyin];
    const auto [position, inserted] = bucket.try_emplace(word);
    if (inserted) ++entry_count_;
    auto& entry = position->second;
    entry.pinned = true;
    entry.suppressed = false;
    entry.last_used = now_seconds();
}

void UserModel::unpin(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    const auto bucket = entries_by_pinyin_.find(pinyin);
    if (bucket == entries_by_pinyin_.end()) return;
    const auto found = bucket->second.find(word);
    if (found == bucket->second.end()) return;
    found->second.pinned = false;
}

void UserModel::suppress(const std::string& pinyin, const std::string& word) {
    std::unique_lock lock(mutex_);
    auto& bucket = entries_by_pinyin_[pinyin];
    const auto [position, inserted] = bucket.try_emplace(word);
    if (inserted) ++entry_count_;
    auto& entry = position->second;
    entry.pinned = false;
    entry.suppressed = true;
    entry.last_used = now_seconds();
}

std::vector<UserPhrase> UserModel::query_exact(const std::string_view pinyin) const {
    std::shared_lock lock(mutex_);
    const auto bucket = entries_by_pinyin_.find(std::string(pinyin));
    if (bucket == entries_by_pinyin_.end()) return {};
    std::vector<UserPhrase> result;
    result.reserve(bucket->second.size());
    for (const auto& [word, entry] : bucket->second) {
        result.push_back({
            std::string(pinyin),
            word,
            entry.count,
            entry.last_used,
            learning_tier(entry.count),
            entry.pinned,
            entry.suppressed,
            entry.user_created,
        });
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.word < right.word;
    });
    return result;
}

int UserModel::score_adjustment(
    const std::string_view pinyin,
    const std::string_view word) const {
    std::shared_lock lock(mutex_);
    const Entry* const found = find_entry(pinyin, word);
    if (found == nullptr) return 0;
    if (found->pinned) return 1'500'000'000;
    if (found->suppressed) return 0;
    const std::uint64_t now = now_seconds();
    const std::uint64_t age = now > found->last_used ? now - found->last_used : 0U;
    int recency = 0;
    if (age < 3600U) {
        recency = 120000;
    } else if (age < 86400U) {
        recency = 80000;
    } else if (age < 7U * 86400U) {
        recency = 40000;
    }
    const std::uint8_t tier = learning_tier(found->count);
    const std::uint32_t extra = found->count > 3U
        ? (std::min)(found->count - 3U, 10U) * 5000U
        : 0U;
    return static_cast<int>(tier) * 150000 + static_cast<int>(extra) + recency;
}

bool UserModel::is_pinned(
    const std::string_view pinyin, const std::string_view word) const {
    std::shared_lock lock(mutex_);
    const Entry* const found = find_entry(pinyin, word);
    return found != nullptr && found->pinned;
}

bool UserModel::is_suppressed(
    const std::string_view pinyin, const std::string_view word) const {
    std::shared_lock lock(mutex_);
    const Entry* const found = find_entry(pinyin, word);
    return found != nullptr && found->suppressed;
}

std::size_t UserModel::entry_count() const noexcept {
    std::shared_lock lock(mutex_);
    return entry_count_;
}

}  // namespace piinput
