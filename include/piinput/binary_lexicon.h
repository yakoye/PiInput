#pragma once

#include "piinput/lexicon.h"

#include <cstddef>
#include <filesystem>
#include <memory>
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
    [[nodiscard]] bool memory_mapped() const noexcept;
    [[nodiscard]] std::size_t mapped_bytes() const noexcept;

private:
    struct Storage;
    struct ReverseWordIndex;
    struct SimplifiedIndex;
    std::shared_ptr<const Storage> storage_;
    mutable std::shared_ptr<ReverseWordIndex> reverse_word_index_;
    mutable std::shared_ptr<SimplifiedIndex> simplified_index_;
};

void compile_tsv_to_binary(
    const std::filesystem::path& input_tsv,
    const std::filesystem::path& output_lex);

[[nodiscard]] bool is_binary_lexicon(const std::filesystem::path& path);

}  // namespace piinput
