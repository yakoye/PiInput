#pragma once

#include "piinput/pinyin.h"
#include "piinput/shuangpin.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

struct PinyinPrefix {
    std::vector<std::string> complete_syllables;
    std::string trailing_prefix;
    std::string canonical_prefix;
    int score{};
};

[[nodiscard]] std::vector<PinyinPrefix> expand_input_prefix(
    std::string_view input,
    std::string_view schema,
    const PinyinSegmenter& pinyin,
    const ShuangpinDecoder& shuangpin,
    std::size_t limit = 16U);

}  // namespace piinput
