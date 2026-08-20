#pragma once

#include "piinput/lexicon.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace piinput {

enum class DictionarySourceFormat {
    piinput_tsv,
    rime_yaml,
    pinyin_data,
    phrase_pinyin_data,
    thuocl,
};

struct DictionaryTerm {
    std::string word;
    std::uint64_t frequency{};
};

struct DictionaryBuildReport {
    std::size_t exact_phrase_count{};
    std::size_t character_derived_count{};
    std::size_t unresolved_count{};
};

struct RimeDictionarySourceReport {
    std::filesystem::path path;
    std::size_t raw_entries{};
    std::size_t accepted_entries{};
    std::size_t duplicate_entries{};
};

struct RimeDictionaryImportReport {
    std::vector<RimeDictionarySourceReport> sources;
    std::size_t raw_entries{};
    std::size_t accepted_entries{};
    std::size_t duplicate_entries{};
};

[[nodiscard]] std::vector<DictionaryTerm> read_thuocl_terms(
    const std::filesystem::path& path);

[[nodiscard]] std::vector<LexiconCandidate> resolve_dictionary_terms(
    const std::vector<DictionaryTerm>& terms,
    const std::vector<LexiconCandidate>& pronunciations,
    std::uint32_t base_weight,
    DictionaryBuildReport& report);

[[nodiscard]] std::vector<LexiconCandidate> read_dictionary_source(
    const std::filesystem::path& path,
    DictionarySourceFormat format,
    std::uint32_t default_weight);

[[nodiscard]] std::vector<LexiconCandidate> read_rime_dictionary(
    const std::filesystem::path& master_path,
    std::uint32_t default_weight,
    RimeDictionaryImportReport& report);

void write_dictionary_tsv(
    const std::filesystem::path& path,
    std::vector<LexiconCandidate> entries);

}  // namespace piinput
