#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piinput {

// Stable uint32 TSV bits. Source provenance and word traits can be ORed together.
enum class EnglishCandidateFlag : std::uint32_t {
    builtin = 1U << 0U,
    downloaded = 1U << 1U,
    user = 1U << 2U,
    proper = 1U << 3U,
    typed = 1U << 4U,
    fuzzy = 1U << 5U,
};

struct EnglishCandidate {
    std::uint64_t id{};
    std::string word;
    std::uint64_t base_weight{};
    std::uint64_t learning_count{};
    bool user_entry{};
    std::uint32_t flags{};
    // Non-empty only for a settings-defined shortcut candidate.
    std::string action_target;

    bool operator==(const EnglishCandidate&) const = default;
};

struct EnglishQueryOptions {
    std::size_t limit{3U};
    // 子序列联想：输入 jiy 也能给出 jimmy。这是为英文模式设计的缩写输入，
    // 在中文模式下必须关掉——那里的字母是拼音，按子序列去凑英文单词只会
    // 得到 tzn -> tarzan、buhc -> buddhic 这种和输入毫无关系的词。
    bool allow_subsequence{true};
    // 词条权重下限。词库最底下一层只有词形没有真实词频，混进中文候选行
    // 里的是 bucolic、tizwin、nizey 这类没人用的词，纯属噪音。
    std::uint64_t minimum_weight{0U};

    bool operator==(const EnglishQueryOptions&) const = default;
};

class EnglishLexicon final {
public:
    [[nodiscard]] std::size_t load_builtin_tsv(const std::filesystem::path& path);
    [[nodiscard]] std::size_t load_user_tsv(const std::filesystem::path& path);
    [[nodiscard]] std::size_t load_learning_tsv(const std::filesystem::path& path);
    [[nodiscard]] std::size_t load_completion_preferences_tsv(
        const std::filesystem::path& path);

    [[nodiscard]] std::vector<EnglishCandidate> query(
        std::string_view prefix,
        std::size_t limit) const;
    [[nodiscard]] std::vector<EnglishCandidate> query(
        std::string_view prefix,
        const EnglishQueryOptions& options) const;
    [[nodiscard]] bool record_selection(std::string_view word) noexcept;
    [[nodiscard]] bool save_learning_tsv(const std::filesystem::path& path) noexcept;

private:
    struct Entry {
        EnglishCandidate candidate;
        std::string lowercase_word;
    };

    [[nodiscard]] std::size_t load_dictionary_tsv(
        const std::filesystem::path& path,
        bool user_dictionary);
    void rebuild_index();

    std::vector<Entry> entries_;
    std::vector<std::size_t> prefix_index_;
    std::unordered_map<std::string, std::size_t> entry_by_word_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>>
        completion_preferences_;
    std::unordered_map<std::string, std::uint64_t> pending_learning_;
    std::uint64_t next_id_{1U};
};

}  // namespace piinput
