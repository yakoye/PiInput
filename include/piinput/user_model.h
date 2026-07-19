#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace piinput {

class UserModel final {
public:
    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

    void record_selection(const std::string& pinyin, const std::string& word);
    void remove(const std::string& pinyin, const std::string& word);

    [[nodiscard]] int score_adjustment(const std::string& pinyin, const std::string& word) const;
    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    struct Entry {
        std::uint32_t count{};
        std::uint64_t last_used{};
    };

    [[nodiscard]] static std::string make_key(const std::string& pinyin, const std::string& word);

    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace piinput
