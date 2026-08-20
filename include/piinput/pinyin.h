#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace piinput {

struct PinyinSegmentation {
    std::vector<std::string> syllables;
    std::string canonical;
    int score{};
};

class PinyinSegmenter final {
public:
    PinyinSegmenter();

    [[nodiscard]] bool is_syllable(std::string_view syllable) const;
    [[nodiscard]] bool is_valid_prefix(std::string_view prefix) const;

    [[nodiscard]] std::vector<PinyinSegmentation> segment(
        std::string_view input,
        std::size_t limit = 8U) const;

    [[nodiscard]] static std::string normalize(std::string_view input);
    [[nodiscard]] static std::string join(const std::vector<std::string>& syllables);
    [[nodiscard]] static const std::vector<std::string>& standard_syllables();

private:
    std::unordered_set<std::string> syllable_set_;
    std::unordered_set<std::string> prefix_set_;
};

// The first letter of every syllable, joined. zh/ch/sh reduce to z/c/s simply
// by being first letters, which is what makes 张靓颖 reachable as both zly and
// zhly: the query normalises the same way before it looks anything up.
//
//     zhi'shi'jing'shen -> zsjs
//     shu'ru'fa         -> srf
[[nodiscard]] std::string simplified_pinyin_key(std::string_view canonical_pinyin);

// One reading of an input where each syllable is either spelled out or reduced
// to its initial. `key` is the per-syllable initials; `syllables` holds the
// spelled-out ones and an empty string where only the initial was typed.
struct SimplifiedPinyinReading {
    std::string key;
    std::vector<std::string> syllables;
};

// Every way `input` can be read as simplified pinyin, mixed with full syllables.
// "srf" yields one reading with three bare initials; "sruf" yields the reading
// s + ru + f, and "shrfa" yields sh + r + fa. Returns nothing when the input
// cannot be a simplified spelling at all.
[[nodiscard]] std::vector<SimplifiedPinyinReading> simplified_pinyin_readings(
    std::string_view input,
    const PinyinSegmenter& segmenter,
    std::size_t limit);

}  // namespace piinput
