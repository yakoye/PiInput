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
};

struct CandidateEvidence {
    CandidateKind kind{CandidateKind::decoded_sentence};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
    std::size_t single_character_tokens{};
    bool covers_all_input{};
};

}  // namespace piinput
