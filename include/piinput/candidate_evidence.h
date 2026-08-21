#pragma once

#include <cstddef>
#include <cstdint>

namespace piinput {

enum class CandidateKind : std::uint8_t {
    user_phrase,
    exact_lexicon,
    decoded_sentence,
    prefix_lexicon,
    single_character,
    incomplete_completion,
    simplified_pinyin,
    // A symbol reached by typing its name: pai for π, qiuhe for ∑.
    symbol,
    // The entry that opens the date or time formats. Choosing it replaces the
    // candidate list with those formats instead of committing anything.
    datetime_group,
};

struct CandidateEvidence {
    CandidateKind kind{CandidateKind::decoded_sentence};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
    std::size_t single_character_tokens{};
    bool covers_all_input{};
};

}  // namespace piinput
