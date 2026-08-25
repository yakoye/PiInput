#include "piinput/punctuation.h"

namespace piinput {
namespace {

[[nodiscard]] std::string ascii_symbol(const char key, const bool shift) {
    if (!shift) {
        return std::string(1U, key);
    }
    switch (key) {
    case '`': return "~";
    case '1': return "!";
    case '2': return "@";
    case '3': return "#";
    case '4': return "$";
    case '5': return "%";
    case '6': return "^";
    case '7': return "&";
    case '8': return "*";
    case '9': return "(";
    case '0': return ")";
    case '-': return "_";
    case '=': return "+";
    case '[': return "{";
    case ']': return "}";
    case '\\': return "|";
    case ';': return ":";
    case '\'': return "\"";
    case ',': return "<";
    case '.': return ">";
    case '/': return "?";
    default: return std::string(1U, key);
    }
}

}  // namespace

std::string PunctuationTransformer::transform(
    const char key,
    const PunctuationMode mode,
    const bool shift,
    const PunctuationBracketStyle bracket_style) const {
    if (mode == PunctuationMode::english) {
        return ascii_symbol(key, shift);
    }

    switch (key) {
    case ',': return shift ? "《" : "，";
    case '.': return shift ? "》" : "。";
    case '/': return shift ? "？" : "、";
    case ';': return shift ? "：" : "；";
    case '\\': return shift ? "|" : "、";
    // Keep common programming/editing operators ASCII even while Chinese
    // punctuation is enabled. Shift+- remains the convenient Chinese em dash.
    case '-': return shift ? "——" : "-";
    case '=': return shift ? "+" : "=";
    case '[':
        return shift
            ? (bracket_style == PunctuationBracketStyle::wechat ? "「" : "{")
            : "【";
    case ']':
        return shift
            ? (bracket_style == PunctuationBracketStyle::wechat ? "」" : "}")
            : "】";
    // Keep the grave accent literal so Markdown inline code and fenced code
    // remain usable in Chinese mode. The symbol center is the explicit place
    // to choose the middle dot.
    case '`': return shift ? "~" : "`";
    case '1': return shift ? "！" : "1";
    case '2': return shift ? "@" : "2";
    case '3': return shift ? "#" : "3";
    case '4': return shift ? "￥" : "4";
    case '5': return shift ? "%" : "5";
    case '6': return shift ? "……" : "6";
    case '7': return shift ? "&" : "7";
    case '8': return shift ? "*" : "8";
    case '9': return shift ? "（" : "9";
    case '0': return shift ? "）" : "0";
    case '\'': {
        if (shift) {
            const std::string result = next_double_quote_open_ ? "“" : "”";
            next_double_quote_open_ = !next_double_quote_open_;
            return result;
        }
        const std::string result = next_single_quote_open_ ? "‘" : "’";
        next_single_quote_open_ = !next_single_quote_open_;
        return result;
    }
    default: return ascii_symbol(key, shift);
    }
}

void PunctuationTransformer::reset_quotes() noexcept {
    next_double_quote_open_ = true;
    next_single_quote_open_ = true;
}

}  // namespace piinput
