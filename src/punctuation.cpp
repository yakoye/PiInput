#include "liteime/punctuation.h"

namespace liteime {

std::string PunctuationTransformer::transform(
    const char key,
    const PunctuationMode mode,
    const bool shift) const {
    if (mode == PunctuationMode::english) {
        return std::string(1U, key);
    }
    if (mode == PunctuationMode::programmer) {
        switch (key) {
        case ',': return shift ? "<" : ",";
        case '.': return shift ? ">" : ".";
        default: return std::string(1U, key);
        }
    }

    switch (key) {
    case ',': return shift ? "《" : "，";
    case '.': return shift ? "》" : "。";
    case '/': return shift ? "？" : "、";
    case ';': return shift ? "：" : "；";
    case '\\': return shift ? "｜" : "、";
    case '-': return shift ? "——" : "－";
    case '=': return shift ? "＋" : "＝";
    case '[': return shift ? "【" : "「";
    case ']': return shift ? "】" : "」";
    case '`': return shift ? "～" : "·";
    case '1': return shift ? "！" : "1";
    case '4': return shift ? "￥" : "4";
    case '6': return shift ? "……" : "6";
    case '9': return shift ? "（" : "9";
    case '0': return shift ? "）" : "0";
    case '\"': {
        const std::string result = next_double_quote_open_ ? "“" : "”";
        next_double_quote_open_ = !next_double_quote_open_;
        return result;
    }
    case '\'': {
        const std::string result = next_single_quote_open_ ? "‘" : "’";
        next_single_quote_open_ = !next_single_quote_open_;
        return result;
    }
    default: return std::string(1U, key);
    }
}

void PunctuationTransformer::reset_quotes() noexcept {
    next_double_quote_open_ = true;
    next_single_quote_open_ = true;
}

}  // namespace liteime
