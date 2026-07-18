#include "liteime/pinyin.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace liteime {
namespace {

[[nodiscard]] bool is_ascii_letter(const char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] std::vector<std::string_view> split_manual(std::string_view input) {
    std::vector<std::string_view> parts;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index <= input.size(); ++index) {
        if (index == input.size() || input[index] == '\'') {
            if (index == start) {
                return {};
            }
            parts.push_back(input.substr(start, index - start));
            start = index + 1U;
        }
    }
    return parts;
}

}  // namespace

const std::vector<std::string>& PinyinSegmenter::standard_syllables() {
    static const std::vector<std::string> syllables = {
#include "pinyin_syllables.inc"
    };
    return syllables;
}

PinyinSegmenter::PinyinSegmenter() {
    const auto& syllables = standard_syllables();
    syllable_set_.reserve(syllables.size() * 2U);
    prefix_set_.reserve(syllables.size() * 8U);
    for (const auto& syllable : syllables) {
        syllable_set_.insert(syllable);
        for (std::size_t length = 1U; length <= syllable.size(); ++length) {
            prefix_set_.insert(syllable.substr(0U, length));
        }
    }
}

bool PinyinSegmenter::is_syllable(const std::string_view syllable) const {
    return syllable_set_.contains(std::string(syllable));
}

bool PinyinSegmenter::is_valid_prefix(const std::string_view prefix) const {
    return prefix_set_.contains(std::string(prefix));
}

std::string PinyinSegmenter::normalize(const std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (std::size_t index = 0U; index < input.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(input[index]);
        if (current < 0x80U) {
            const char character = static_cast<char>(current);
            if (is_ascii_letter(character)) {
                output.push_back(static_cast<char>(std::tolower(current)));
            } else if (character == '\'' || character == ' ') {
                if (!output.empty() && output.back() != '\'') {
                    output.push_back('\'');
                }
            } else if (character == ':' && !output.empty() && output.back() == 'u') {
                output.back() = 'v';
            } else {
                throw std::invalid_argument("Unsupported character in pinyin input");
            }
            continue;
        }

        // UTF-8 ü / Ü.
        if (index + 1U < input.size() && current == 0xC3U &&
            (static_cast<unsigned char>(input[index + 1U]) == 0xBCU ||
             static_cast<unsigned char>(input[index + 1U]) == 0x9CU)) {
            output.push_back('v');
            ++index;
            continue;
        }
        throw std::invalid_argument("Unsupported UTF-8 character in pinyin input");
    }

    while (!output.empty() && output.back() == '\'') {
        output.pop_back();
    }
    return output;
}

std::string PinyinSegmenter::join(const std::vector<std::string>& syllables) {
    std::string output;
    std::size_t total = 0U;
    for (const auto& syllable : syllables) {
        total += syllable.size() + 1U;
    }
    output.reserve(total);
    for (const auto& syllable : syllables) {
        if (!output.empty()) {
            output.push_back('\'');
        }
        output.append(syllable);
    }
    return output;
}

std::vector<PinyinSegmentation> PinyinSegmenter::segment(
    const std::string_view raw_input,
    const std::size_t limit) const {
    if (limit == 0U) {
        return {};
    }
    const std::string normalized = normalize(raw_input);
    if (normalized.empty()) {
        return {};
    }

    if (normalized.find('\'') != std::string::npos) {
        const auto parts = split_manual(normalized);
        if (parts.empty()) {
            return {};
        }
        PinyinSegmentation segmentation;
        for (const auto part : parts) {
            if (!is_syllable(part)) {
                return {};
            }
            segmentation.syllables.emplace_back(part);
            segmentation.score += static_cast<int>(part.size() * part.size() * 10U);
        }
        segmentation.score -= static_cast<int>(segmentation.syllables.size() * 4U);
        segmentation.canonical = join(segmentation.syllables);
        return {std::move(segmentation)};
    }

    struct Partial {
        std::vector<std::string> syllables;
        int score{};
    };

    std::vector<std::vector<Partial>> states(normalized.size() + 1U);
    states[0U].push_back(Partial{});

    constexpr std::size_t max_syllable_length = 6U;
    const std::size_t per_position_limit = std::max<std::size_t>(limit * 4U, 16U);

    for (std::size_t position = 0U; position < normalized.size(); ++position) {
        if (states[position].empty()) {
            continue;
        }
        const std::size_t remaining = normalized.size() - position;
        const std::size_t max_length = std::min(max_syllable_length, remaining);
        for (std::size_t length = 1U; length <= max_length; ++length) {
            const std::string syllable = normalized.substr(position, length);
            if (!syllable_set_.contains(syllable)) {
                continue;
            }
            auto& destination = states[position + length];
            for (const auto& partial : states[position]) {
                Partial next = partial;
                next.syllables.push_back(syllable);
                // Prefer fewer, longer syllables while retaining alternate boundaries.
                next.score += static_cast<int>(length * length * 10U) - 4;
                destination.push_back(std::move(next));
            }
            std::stable_sort(destination.begin(), destination.end(), [](const Partial& left, const Partial& right) {
                if (left.score != right.score) {
                    return left.score > right.score;
                }
                return left.syllables.size() < right.syllables.size();
            });
            if (destination.size() > per_position_limit) {
                destination.resize(per_position_limit);
            }
        }
    }

    std::vector<PinyinSegmentation> results;
    results.reserve(std::min(limit, states.back().size()));
    std::unordered_set<std::string> seen;
    for (auto& partial : states.back()) {
        const std::string canonical = join(partial.syllables);
        if (!seen.insert(canonical).second) {
            continue;
        }
        results.push_back(PinyinSegmentation{
            std::move(partial.syllables),
            canonical,
            partial.score,
        });
        if (results.size() >= limit) {
            break;
        }
    }
    return results;
}

}  // namespace liteime
