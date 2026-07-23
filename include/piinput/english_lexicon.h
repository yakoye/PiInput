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
};

struct EnglishCandidate {
    std::uint64_t id{};
    std::string word;
    std::uint64_t base_weight{};
    std::uint64_t learning_count{};
    bool user_entry{};
    std::uint32_t flags{};

    bool operator==(const EnglishCandidate&) const = default;
};

class EnglishLexicon final {
public:
    [[nodiscard]] std::size_t load_builtin_tsv(const std::filesystem::path& path);
    [[nodiscard]] std::size_t load_user_tsv(const std::filesystem::path& path);
    [[nodiscard]] std::size_t load_learning_tsv(const std::filesystem::path& path);

    [[nodiscard]] std::vector<EnglishCandidate> query(
        std::string_view prefix,
        std::size_t limit) const;
    [[nodiscard]] bool record_selection(std::string_view word) noexcept;
    [[nodiscard]] bool save_learning_tsv(const std::filesystem::path& path) const noexcept;

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
    std::uint64_t next_id_{1U};
};

}  // namespace piinput
