#pragma once

#include "piinput/settings.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

[[nodiscard]] std::vector<std::string> normalize_full_pinyin_variants(
    std::string_view input,
    const PinyinSettings& settings,
    std::size_t variant_limit);

}  // namespace piinput
