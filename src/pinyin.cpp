#include "piinput/pinyin.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace piinput {
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

std::string simplified_pinyin_key(const std::string_view canonical_pinyin) {
    std::string key;
    bool at_syllable_start = true;
    for (const char character : canonical_pinyin) {
        if (character == '\'') {
            at_syllable_start = true;
            continue;
        }
        if (at_syllable_start) {
            key.push_back(character);
            at_syllable_start = false;
        }
    }
    return key;
}

namespace {

// Initials a syllable can start with, longest first so zh/ch/sh win over z/c/s
// when both could apply. A bare vowel is its own initial: 安 is reachable as a.
constexpr std::string_view simplified_initials[] = {
    "zh", "ch", "sh",
    "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h",
    "j", "q", "x", "r", "z", "c", "s", "y", "w",
    "a", "e", "o",
};

}  // namespace

std::vector<SimplifiedPinyinReading> simplified_pinyin_readings(
    const std::string_view input,
    const PinyinSegmenter& segmenter,
    const std::size_t limit) {
    // The search below branches at every position, so its cost is exponential
    // in the input length without these bounds. Nobody spells a phrase longer
    // than eight syllables in initials anyway.
    constexpr std::size_t max_simplified_syllables = 8U;
    constexpr std::size_t max_simplified_input = 32U;
    if (input.empty() || limit == 0U || input.size() > max_simplified_input) {
        return {};
    }
    std::vector<SimplifiedPinyinReading> readings;
    // Depth-first over the input: at each position take either a full syllable
    // or a bare initial. Both branches are kept because the same letters can be
    // either -- "sruf" is s + ru + f, and "ru" there is a spelled-out syllable
    // that also happens to start a longer one.
    const auto walk = [&](auto&& self, const std::size_t position,
                          SimplifiedPinyinReading& current) -> void {
        if (readings.size() >= limit || current.key.size() > max_simplified_syllables) {
            return;
        }
        if (position == input.size()) {
            if (current.key.size() >= 2U) {
                readings.push_back(current);
            }
            return;
        }
        for (std::size_t length = (std::min)(input.size() - position, std::size_t{6U});
             length >= 1U; --length) {
            const auto candidate = input.substr(position, length);
            if (!segmenter.is_syllable(candidate)) {
                continue;
            }
            const auto key_size = current.key.size();
            current.key.push_back(candidate.front());
            current.syllables.emplace_back(candidate);
            self(self, position + length, current);
            current.syllables.pop_back();
            current.key.resize(key_size);
            if (readings.size() >= limit) {
                return;
            }
        }
        for (const auto initial : simplified_initials) {
            if (input.compare(position, initial.size(), initial) != 0) {
                continue;
            }
            const auto key_size = current.key.size();
            current.key.push_back(initial.front());
            current.syllables.emplace_back();
            self(self, position + initial.size(), current);
            current.syllables.pop_back();
            current.key.resize(key_size);
            if (readings.size() >= limit) {
                return;
            }
        }
    };
    SimplifiedPinyinReading current;
    walk(walk, 0U, current);

    std::stable_sort(readings.begin(), readings.end(),
        [](const SimplifiedPinyinReading& left, const SimplifiedPinyinReading& right) {
            // More spelled-out syllables first: that reading explains more of
            // what the user actually typed.
            const auto spelled = [](const SimplifiedPinyinReading& reading) {
                return std::count_if(reading.syllables.begin(), reading.syllables.end(),
                    [](const std::string& syllable) { return !syllable.empty(); });
            };
            const auto left_spelled = spelled(left);
            const auto right_spelled = spelled(right);
            if (left_spelled != right_spelled) {
                return left_spelled > right_spelled;
            }
            return left.key.size() < right.key.size();
        });
    readings.erase(std::unique(readings.begin(), readings.end(),
        [](const SimplifiedPinyinReading& left, const SimplifiedPinyinReading& right) {
            return left.key == right.key && left.syllables == right.syllables;
        }), readings.end());
    if (readings.size() > limit) {
        readings.resize(limit);
    }
    return readings;
}

}  // namespace piinput
