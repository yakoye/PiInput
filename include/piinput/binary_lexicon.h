#pragma once

#include "piinput/lexicon.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace piinput {

class BinaryLexicon final {
public:
    void load(const std::filesystem::path& path);

    [[nodiscard]] std::vector<LexiconCandidate> query_exact(
        const std::string& pinyin,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<LexiconCandidate> query_prefix(
        const std::string& pinyin_prefix,
        std::size_t limit = 10U,
        std::size_t scan_limit = 512U,
        std::size_t max_syllables = 0U) const;

    [[nodiscard]] std::vector<LexiconCandidate> query_word(
        std::string_view word,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<LexiconCandidate> query_simplified(
        const std::string& key,
        const std::vector<std::string>& syllable_filter,
        std::size_t limit = 10U,
        std::size_t scan_limit = 512U) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    DevLexicon lexicon_;
};

void compile_tsv_to_binary(
    const std::filesystem::path& input_tsv,
    const std::filesystem::path& output_lex);

[[nodiscard]] bool is_binary_lexicon(const std::filesystem::path& path);

}  // namespace piinput
