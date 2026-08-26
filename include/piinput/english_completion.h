#pragma once

#include "piinput/english_lexicon.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

// What the Chinese side of the row produced, reduced to the two signals that
// decide where an English word belongs among it.
struct ChineseCandidateSummary {
    bool has_candidates{};
    // Taken straight from CandidateEvidence::covers_all_input. When false the
    // Chinese candidates were guessed from an abbreviation or a prefix rather
    // than read off a complete spelling, and they carry much less weight.
    bool covers_all_input{};
    std::int64_t top_score{};

    bool operator==(const ChineseCandidateSummary&) const = default;
};

struct EnglishCompletionSettings {
    bool enabled{};
    std::size_t max_items{3U};
    // Four letters land on two valid syllables far more easily in double
    // pinyin, so covers_all_input is almost always true there and cannot carry
    // the decision alone. Each schema gets its own frequency bar.
    bool double_pinyin{};

    bool operator==(const EnglishCompletionSettings&) const = default;
};

struct EnglishCompletionPlan {
    // One-based candidate number. Zero means the row gets no English at all.
    std::size_t start_position{};
    std::vector<std::string> words;

    bool operator==(const EnglishCompletionPlan&) const = default;
};

[[nodiscard]] std::size_t english_start_position(
    const ChineseCandidateSummary& chinese, bool double_pinyin) noexcept;

// Dresses a dictionary word in the case the user is typing: book stays book,
// Book becomes Book, BOOK becomes BOOK. A word stored as a proper noun keeps
// its own capitals.
[[nodiscard]] std::string apply_input_case(
    std::string_view input, std::string_view word);

[[nodiscard]] EnglishCompletionPlan plan_english_completion(
    std::string_view input,
    const ChineseCandidateSummary& chinese,
    const EnglishLexicon& lexicon,
    const EnglishCompletionSettings& settings);

}  // namespace piinput
