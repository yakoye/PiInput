#pragma once

#include "liteime/lexicon.h"

#include <filesystem>
#include <vector>

namespace liteime {

enum class DictionarySourceFormat {
    liteime_tsv,
    rime_yaml,
    pinyin_data,
    phrase_pinyin_data,
};

[[nodiscard]] std::vector<LexiconCandidate> read_dictionary_source(
    const std::filesystem::path& path,
    DictionarySourceFormat format,
    std::uint32_t default_weight);

void write_dictionary_tsv(
    const std::filesystem::path& path,
    std::vector<LexiconCandidate> entries);

}  // namespace liteime
