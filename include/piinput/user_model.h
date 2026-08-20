#pragma once

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piinput {

struct UserPhrase final {
    std::string pinyin;
    std::string word;
    std::uint32_t selection_count{};
    std::uint64_t last_used{};
    std::uint8_t learning_tier{};
    bool pinned{};
    bool suppressed{};
    bool user_created{};
};

class UserModel final {
public:
    UserModel() = default;
    UserModel(const UserModel& other);
    UserModel& operator=(const UserModel& other);

    std::vector<std::string> load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

    void record_selection(const std::string& pinyin, const std::string& word);
    void record_composed_phrase(const std::string& pinyin, const std::string& word);
    void remove(const std::string& pinyin, const std::string& word);
    void remove_learning(const std::string& pinyin, const std::string& word);
    void pin(const std::string& pinyin, const std::string& word);
    void unpin(const std::string& pinyin, const std::string& word);
    void suppress(const std::string& pinyin, const std::string& word);

    [[nodiscard]] std::vector<UserPhrase> query_exact(std::string_view pinyin) const;
    [[nodiscard]] int score_adjustment(std::string_view pinyin, std::string_view word) const;
    [[nodiscard]] bool is_pinned(std::string_view pinyin, std::string_view word) const;
    [[nodiscard]] bool is_suppressed(std::string_view pinyin, std::string_view word) const;
    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    struct Entry {
        std::uint32_t count{};
        std::uint64_t last_used{};
        bool pinned{};
        bool suppressed{};
        bool user_created{};
    };

    using WordEntries = std::unordered_map<std::string, Entry>;

    [[nodiscard]] static std::uint8_t learning_tier(std::uint32_t count) noexcept;
    [[nodiscard]] const Entry* find_entry(
        std::string_view pinyin, std::string_view word) const noexcept;

    std::unordered_map<std::string, WordEntries> entries_by_pinyin_;
    std::size_t entry_count_{};
    mutable std::shared_mutex mutex_;
};

}  // namespace piinput
