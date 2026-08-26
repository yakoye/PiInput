#include "piinput/english_completion.h"

#include <algorithm>
#include <cctype>

namespace piinput {
namespace {

// The same letters are both pinyin and an English word, so the position of an
// English candidate cannot be fixed. These thresholds are calibrated against
// measured behaviour in full pinyin: 和 at 6,252,031 leaves no room for
// English, 按 at 3,826,950 pushes it down the row, 那么 at 501,795 concedes
// the second slot, and 错的 at 105,000 concedes the first.
struct StartPositionThresholds {
    std::int64_t suppress_above;
    std::int64_t demote_above;
    std::int64_t second_above;
    std::size_t demoted_position;
};

constexpr StartPositionThresholds kFullPinyin{
    .suppress_above = 5000000,
    .demote_above = 1000000,
    .second_above = 200000,
    .demoted_position = 5U,
};

// Double pinyin spells a whole syllable in two letters, so short English words
// nearly always decode to something. The bar rises accordingly; otherwise
// English would take the first slot away from characters people type daily.
constexpr StartPositionThresholds kDoublePinyin{
    .suppress_above = 3000000,
    .demote_above = 600000,
    .second_above = 100000,
    .demoted_position = 5U,
};

[[nodiscard]] bool is_ascii_letter(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] bool is_ascii_upper(const char value) noexcept {
    return value >= 'A' && value <= 'Z';
}

}  // namespace

std::size_t english_start_position(
    const ChineseCandidateSummary& chinese, const bool double_pinyin) noexcept {
    if (!chinese.has_candidates) return 1U;
    // The Chinese candidates were guessed from an incomplete reading, so they
    // are no more certain than the English word and should not outrank it.
    if (!chinese.covers_all_input) return 1U;
    const StartPositionThresholds& limits = double_pinyin ? kDoublePinyin : kFullPinyin;
    if (chinese.top_score > limits.suppress_above) return 0U;
    if (chinese.top_score > limits.demote_above) return limits.demoted_position;
    if (chinese.top_score > limits.second_above) return 2U;
    return 1U;
}

std::string apply_input_case(
    const std::string_view input, const std::string_view word) {
    std::string result(word);
    if (input.empty() || result.empty()) return result;

    // A single capital is the start of a capitalised word far more often than
    // it is the start of a shouted one, so it only capitalises.
    const bool shouting = input.size() > 1U &&
        std::all_of(input.begin(), input.end(), is_ascii_upper);
    if (shouting) {
        for (char& value : result) {
            value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
        }
        return result;
    }
    if (is_ascii_upper(input.front())) {
        result.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result.front())));
    }
    return result;
}

EnglishCompletionPlan plan_english_completion(
    const std::string_view input,
    const ChineseCandidateSummary& chinese,
    const EnglishLexicon& lexicon,
    const EnglishCompletionSettings& settings) {
    if (!settings.enabled || settings.max_items == 0U) return {};
    // One letter completes to hundreds of words and collides with a great many
    // readings; the suggestions would be noise rather than help.
    if (input.size() < 2U) return {};
    if (!std::all_of(input.begin(), input.end(), is_ascii_letter)) return {};

    const std::size_t position = english_start_position(chinese, settings.double_pinyin);
    if (position == 0U) return {};

    const auto matches = lexicon.query(input, settings.max_items);
    if (matches.empty()) return {};

    EnglishCompletionPlan plan;
    plan.start_position = position;
    plan.words.reserve(matches.size());
    for (const auto& candidate : matches) {
        plan.words.push_back(apply_input_case(input, candidate.word));
    }
    return plan;
}

}  // namespace piinput
