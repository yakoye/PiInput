#include "piinput/pinyin_prefix.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace piinput {
namespace {

[[nodiscard]] std::string join_prefix(
    const std::vector<std::string>& complete,
    const std::string_view trailing) {
    std::string canonical = PinyinSegmenter::join(complete);
    if (!canonical.empty()) {
        canonical.push_back('\'');
    }
    canonical.append(trailing);
    return canonical;
}

[[nodiscard]] std::string flypy_initial_prefix(const char key) {
    switch (key) {
    case 'a':
    case 'e':
    case 'o':
        return std::string(1U, key);
    case 'i': return "ch";
    case 'u': return "sh";
    case 'v': return "zh";
    default:
        if (key >= 'b' && key <= 'z') {
            return std::string(1U, key);
        }
        return {};
    }
}

void sort_and_limit(std::vector<PinyinPrefix>& results, const std::size_t limit) {
    std::stable_sort(results.begin(), results.end(), [](const PinyinPrefix& left, const PinyinPrefix& right) {
        if (left.complete_syllables.size() != right.complete_syllables.size()) {
            return left.complete_syllables.size() > right.complete_syllables.size();
        }
        if (left.trailing_prefix.size() != right.trailing_prefix.size()) {
            return left.trailing_prefix.size() > right.trailing_prefix.size();
        }
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.canonical_prefix < right.canonical_prefix;
    });
    results.erase(std::unique(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.canonical_prefix == right.canonical_prefix;
    }), results.end());
    if (results.size() > limit) {
        results.resize(limit);
    }
}

[[nodiscard]] std::vector<PinyinPrefix> expand_full_prefix(
    const std::string& normalized,
    const PinyinSegmenter& pinyin,
    const std::size_t limit) {
    std::vector<PinyinPrefix> results;
    const std::size_t separator = normalized.rfind('\'');
    const std::size_t first_tail = separator == std::string::npos ? 0U : separator + 1U;
    const std::size_t last_tail = normalized.size();

    for (std::size_t tail_start = first_tail; tail_start < last_tail; ++tail_start) {
        const std::string trailing = normalized.substr(tail_start);
        if (trailing.find('\'') != std::string::npos || !pinyin.is_valid_prefix(trailing)) {
            continue;
        }
        std::string complete_input = normalized.substr(0U, tail_start);
        while (!complete_input.empty() && complete_input.back() == '\'') {
            complete_input.pop_back();
        }
        if (complete_input.empty()) {
            results.push_back(PinyinPrefix{{}, trailing, trailing, static_cast<int>(trailing.size() * 10U)});
            continue;
        }
        for (const auto& segmentation : pinyin.segment(complete_input, limit)) {
            results.push_back(PinyinPrefix{
                segmentation.syllables,
                trailing,
                join_prefix(segmentation.syllables, trailing),
                segmentation.score + static_cast<int>(trailing.size() * 10U),
            });
        }
    }
    sort_and_limit(results, limit);
    return results;
}

[[nodiscard]] std::vector<PinyinPrefix> expand_flypy_prefix(
    const std::string& normalized,
    const ShuangpinDecoder& shuangpin,
    const std::size_t limit) {
    std::string compact;
    compact.reserve(normalized.size());
    for (const char character : normalized) {
        if (character != '\'') {
            compact.push_back(character);
        }
    }
    if (compact.empty() || (compact.size() % 2U) == 0U) {
        return {};
    }

    const std::string trailing = flypy_initial_prefix(compact.back());
    if (trailing.empty()) {
        return {};
    }
    compact.pop_back();

    std::vector<PinyinSegmentation> bases;
    if (compact.empty()) {
        bases.push_back(PinyinSegmentation{});
    } else {
        bases = shuangpin.decode("flypy", compact, limit);
    }

    std::vector<PinyinPrefix> results;
    for (const auto& base : bases) {
        results.push_back(PinyinPrefix{
            base.syllables,
            trailing,
            join_prefix(base.syllables, trailing),
            base.score + static_cast<int>(trailing.size() * 10U),
        });
    }
    sort_and_limit(results, limit);
    return results;
}

}  // namespace

std::vector<PinyinPrefix> expand_input_prefix(
    const std::string_view input,
    const std::string_view schema,
    const PinyinSegmenter& pinyin,
    const ShuangpinDecoder& shuangpin,
    const std::size_t limit) {
    if (limit == 0U || input.empty()) {
        return {};
    }
    const std::string normalized = PinyinSegmenter::normalize(input);
    if (normalized.empty()) {
        return {};
    }
    if (schema == "full" || schema == "full-pinyin" || schema == "pinyin") {
        return expand_full_prefix(normalized, pinyin, limit);
    }
    if (schema == "flypy") {
        return expand_flypy_prefix(normalized, shuangpin, limit);
    }
    return {};
}

}  // namespace piinput
