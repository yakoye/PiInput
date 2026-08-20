#pragma once

#include "piinput/pinyin.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piinput {

struct ShuangpinSchemeInfo {
    std::string id;
    std::string name;
};

class ShuangpinDecoder final {
public:
    ShuangpinDecoder();

    [[nodiscard]] const std::vector<ShuangpinSchemeInfo>& schemes() const noexcept;
    [[nodiscard]] bool has_scheme(std::string_view scheme_id) const;

    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        std::string_view scheme_id,
        std::string_view input,
        std::size_t limit = 16U,
        bool uv_compatibility = true) const;

    [[nodiscard]] std::vector<std::string> syllables_for_code(
        std::string_view scheme_id,
        std::string_view code,
        bool uv_compatibility = true) const;

private:
    struct SchemeData {
        ShuangpinSchemeInfo info;
        std::unordered_map<std::string, std::vector<std::string>> code_to_syllables;
    };

    std::vector<ShuangpinSchemeInfo> scheme_infos_;
    std::unordered_map<std::string, SchemeData> schemes_;
};

}  // namespace piinput
