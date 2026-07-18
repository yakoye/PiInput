#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace liteime {

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

}  // namespace liteime
