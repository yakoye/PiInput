#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace piinput {

struct LexiconCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t weight{};
    bool authoritative_weight{true};
};

class DevLexicon final {
public:
    void load_tsv(const std::filesystem::path& path);
    void load_entries(std::vector<LexiconCandidate> entries);

    [[nodiscard]] std::vector<LexiconCandidate> query_exact(
        const std::string& pinyin,
        std::size_t limit = 10) const;

    // `max_syllables` of 0 leaves the span unbounded. A non-zero bound drops
    // entries longer than the user has actually typed towards, so completing
    // one unfinished syllable cannot pull in whole sentences behind it.
    [[nodiscard]] std::vector<LexiconCandidate> query_prefix(
        const std::string& pinyin_prefix,
        std::size_t limit = 10U,
        std::size_t scan_limit = 512U,
        std::size_t max_syllables = 0U) const;

    [[nodiscard]] std::vector<LexiconCandidate> query_word(
        std::string_view word,
        std::size_t limit = 10U) const;

    // Entries whose per-syllable initials match `key`, which may itself be a
    // prefix: "sr" reaches 输入法 the same way "srf" does, so the candidate
    // list keeps moving while the user is still typing. `syllable_filter` holds
    // one entry per syllable -- empty where the user typed only an initial, and
    // the full syllable where they typed it out -- which is what lets sruf and
    // shrfa mean 输入法 without also meaning every other srf word.
    [[nodiscard]] std::vector<LexiconCandidate> query_simplified(
        const std::string& key,
        const std::vector<std::string>& syllable_filter,
        std::size_t limit = 10U,
        std::size_t scan_limit = 512U) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    struct ReverseWordIndex;
    struct SimplifiedIndex;
    std::unordered_map<std::string, std::vector<LexiconCandidate>> entries_by_pinyin_;
    std::vector<std::string> pinyin_keys_;
    mutable std::shared_ptr<ReverseWordIndex> reverse_word_index_;
    // Built on first use, like the reverse word index: the on-disk lexicon
    // format stays untouched so lexicons compiled by older releases keep
    // working, and users who leave simplified pinyin off pay nothing for it.
    mutable std::shared_ptr<SimplifiedIndex> simplified_index_;
    std::size_t entry_count_{};
};

}  // namespace piinput
