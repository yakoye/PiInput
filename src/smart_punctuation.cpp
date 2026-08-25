#include "piinput/smart_punctuation.h"

#include <array>
#include <cstddef>

namespace piinput {
namespace {

constexpr std::size_t context_window_limit = 64U;

[[nodiscard]] bool is_ascii_alpha(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] bool is_ascii_digit_local(const char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_ascii_alphanumeric(const char value) noexcept {
    return is_ascii_alpha(value) || is_ascii_digit_local(value);
}

[[nodiscard]] bool is_local_token_character(const char value) noexcept {
    if (is_ascii_alphanumeric(value)) return true;
    switch (value) {
    case '_':
    case '+':
    case '@':
    case '%':
    case '#':
    case '?':
    case '!':
    case '=':
    case '&':
    case ':':
    case '/':
    case '\\':
    case '.':
    case ',':
    case '-':
    case '[':
    case ']':
    case '(':
    case ')':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

struct LocalToken final {
    std::array<char, context_window_limit * 2U + 1U> bytes{};
    std::size_t size{};
    std::size_t symbol_index{};

    [[nodiscard]] std::string_view view() const noexcept {
        return {bytes.data(), size};
    }
};

[[nodiscard]] LocalToken make_local_token(
    std::string_view left,
    const char symbol,
    std::string_view right) noexcept {
    if (left.size() > context_window_limit) {
        left.remove_prefix(left.size() - context_window_limit);
    }
    if (right.size() > context_window_limit) right.remove_suffix(right.size() - context_window_limit);

    std::size_t left_start = left.size();
    while (left_start != 0U && is_local_token_character(left[left_start - 1U])) --left_start;
    std::size_t right_end = 0U;
    while (right_end < right.size() && is_local_token_character(right[right_end])) ++right_end;

    LocalToken result;
    for (std::size_t index = left_start; index < left.size(); ++index) {
        result.bytes[result.size++] = left[index];
    }
    result.symbol_index = result.size;
    result.bytes[result.size++] = symbol;
    for (std::size_t index = 0U; index < right_end; ++index) {
        result.bytes[result.size++] = right[index];
    }
    return result;
}

[[nodiscard]] char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool starts_with_ascii_case_insensitive(
    const std::string_view text,
    const std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (ascii_lower(text[index]) != ascii_lower(prefix[index])) return false;
    }
    return true;
}

[[nodiscard]] bool equals_ascii_case_insensitive(
    const std::string_view text,
    const std::string_view expected) noexcept {
    return text.size() == expected.size() &&
        starts_with_ascii_case_insensitive(text, expected);
}

[[nodiscard]] bool is_url_token(const std::string_view token) noexcept {
    return starts_with_ascii_case_insensitive(token, "http://") ||
        starts_with_ascii_case_insensitive(token, "https://") ||
        starts_with_ascii_case_insensitive(token, "ftp://") ||
        starts_with_ascii_case_insensitive(token, "file://") ||
        starts_with_ascii_case_insensitive(token, "mailto:") ||
        starts_with_ascii_case_insensitive(token, "www.");
}

[[nodiscard]] bool is_url_scheme_colon(
    const LocalToken& token,
    const char symbol) noexcept {
    if (symbol != ':') return false;
    const std::string_view left{token.bytes.data(), token.symbol_index};
    return equals_ascii_case_insensitive(left, "http") ||
        equals_ascii_case_insensitive(left, "https") ||
        equals_ascii_case_insensitive(left, "ftp") ||
        equals_ascii_case_insensitive(left, "file") ||
        equals_ascii_case_insensitive(left, "mailto");
}

[[nodiscard]] bool is_email_token(const std::string_view token) noexcept {
    const std::size_t at = token.find('@');
    return at != std::string_view::npos && at != 0U && at + 1U < token.size();
}

[[nodiscard]] bool is_path_token(const std::string_view token) noexcept {
    if (token.starts_with("/") || token.starts_with("./") || token.starts_with("../") ||
        token.find('\\') != std::string_view::npos) {
        return true;
    }
    return token.size() >= 3U && is_ascii_alpha(token[0]) && token[1] == ':' &&
        (token[2] == '/' || token[2] == '\\');
}

[[nodiscard]] bool is_filename_dot(const LocalToken& token, const char symbol) noexcept {
    if (symbol != '.' || token.symbol_index == 0U || token.symbol_index + 1U >= token.size) {
        return false;
    }
    bool basename_has_name_character = false;
    for (std::size_t index = 0U; index < token.symbol_index; ++index) {
        basename_has_name_character = basename_has_name_character ||
            is_ascii_alpha(token.bytes[index]) || token.bytes[index] == '_' ||
            token.bytes[index] == '-';
    }
    if (!basename_has_name_character) return false;

    std::size_t extension_length = 0U;
    for (std::size_t index = token.symbol_index + 1U; index < token.size; ++index) {
        const char value = token.bytes[index];
        if (!(is_ascii_alphanumeric(value) || value == '_' || value == '-')) break;
        ++extension_length;
    }
    return extension_length != 0U && extension_length <= 16U;
}

[[nodiscard]] bool parse_unsigned(
    const std::string_view text,
    unsigned& value) noexcept {
    if (text.empty()) return false;
    value = 0U;
    for (const char character : text) {
        if (!is_ascii_digit_local(character)) return false;
        value = value * 10U + static_cast<unsigned>(character - '0');
        if (value > 9999U) return false;
    }
    return true;
}

[[nodiscard]] bool valid_ipv4(const std::string_view token) noexcept {
    std::size_t begin = 0U;
    unsigned segments = 0U;
    while (begin <= token.size()) {
        const std::size_t end = token.find('.', begin);
        const std::size_t stop = end == std::string_view::npos ? token.size() : end;
        unsigned value = 0U;
        if (!parse_unsigned(token.substr(begin, stop - begin), value) || value > 255U) return false;
        ++segments;
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return segments == 4U;
}

[[nodiscard]] bool valid_numeric_version(std::string_view token) noexcept {
    const bool explicit_version = !token.empty() && (token.front() == 'v' || token.front() == 'V');
    if (explicit_version) token.remove_prefix(1U);
    std::size_t begin = 0U;
    unsigned segments = 0U;
    while (begin <= token.size()) {
        const std::size_t end = token.find('.', begin);
        const std::size_t stop = end == std::string_view::npos ? token.size() : end;
        unsigned ignored = 0U;
        if (!parse_unsigned(token.substr(begin, stop - begin), ignored)) return false;
        ++segments;
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return explicit_version ? segments >= 2U : segments >= 3U;
}

[[nodiscard]] bool valid_decimal(std::string_view token) noexcept {
    if (!token.empty() && (token.front() == '-' || token.front() == '+')) token.remove_prefix(1U);
    const std::size_t dot = token.find('.');
    if (dot == std::string_view::npos || token.find('.', dot + 1U) != std::string_view::npos) {
        return false;
    }
    unsigned ignored = 0U;
    return parse_unsigned(token.substr(0U, dot), ignored) &&
        parse_unsigned(token.substr(dot + 1U), ignored);
}

[[nodiscard]] bool valid_grouped_number(std::string_view token) noexcept {
    if (!token.empty() && (token.front() == '-' || token.front() == '+')) token.remove_prefix(1U);
    const std::size_t dot = token.find('.');
    const std::string_view integer = token.substr(0U, dot);
    if (dot != std::string_view::npos) {
        unsigned ignored = 0U;
        if (!parse_unsigned(token.substr(dot + 1U), ignored)) return false;
    }

    const std::size_t first_comma = integer.find(',');
    if (first_comma == std::string_view::npos || first_comma == 0U || first_comma > 3U) return false;
    unsigned ignored = 0U;
    if (!parse_unsigned(integer.substr(0U, first_comma), ignored)) return false;
    std::size_t begin = first_comma + 1U;
    while (begin <= integer.size()) {
        const std::size_t comma = integer.find(',', begin);
        const std::size_t stop = comma == std::string_view::npos ? integer.size() : comma;
        if (stop - begin != 3U || !parse_unsigned(integer.substr(begin, 3U), ignored)) return false;
        if (comma == std::string_view::npos) break;
        begin = comma + 1U;
    }
    return true;
}

[[nodiscard]] bool valid_time(const std::string_view token) noexcept {
    const std::size_t first = token.find(':');
    if (first == std::string_view::npos) return false;
    const std::size_t second = token.find(':', first + 1U);
    if (second != std::string_view::npos &&
        token.find(':', second + 1U) != std::string_view::npos) {
        return false;
    }
    unsigned hour = 0U;
    unsigned minute = 0U;
    unsigned second_value = 0U;
    if (!parse_unsigned(token.substr(0U, first), hour)) return false;
    const std::size_t minute_end = second == std::string_view::npos ? token.size() : second;
    if (!parse_unsigned(token.substr(first + 1U, minute_end - first - 1U), minute)) return false;
    if (second != std::string_view::npos &&
        !parse_unsigned(token.substr(second + 1U), second_value)) {
        return false;
    }
    return hour <= 23U && minute <= 59U &&
        (second == std::string_view::npos || second_value <= 59U);
}

[[nodiscard]] bool valid_ratio(const std::string_view token) noexcept {
    const std::size_t colon = token.find(':');
    if (colon == std::string_view::npos || token.find(':', colon + 1U) != std::string_view::npos) {
        return false;
    }
    unsigned ignored = 0U;
    return parse_unsigned(token.substr(0U, colon), ignored) &&
        parse_unsigned(token.substr(colon + 1U), ignored);
}

[[nodiscard]] bool is_numeric_punctuation_candidate(const std::string_view token) noexcept {
    bool has_digit = false;
    for (const char value : token) {
        if (is_ascii_digit_local(value)) {
            has_digit = true;
            continue;
        }
        if (value != '.' && value != ',' && value != ':' && value != '-' && value != '+') {
            return false;
        }
    }
    return has_digit;
}

[[nodiscard]] bool is_technical_infix(const LocalToken& token) noexcept {
    if (token.symbol_index == 0U || token.symbol_index + 1U >= token.size) return false;
    const char left = token.bytes[token.symbol_index - 1U];
    const char right = token.bytes[token.symbol_index + 1U];
    if (!(is_ascii_alphanumeric(left) || left == '_' || left == ']') ||
        !(is_ascii_alphanumeric(right) || right == '_' || right == '[')) {
        return false;
    }
    bool has_alpha_or_bracket = false;
    for (std::size_t index = 0U; index < token.size; ++index) {
        has_alpha_or_bracket = has_alpha_or_bracket || is_ascii_alpha(token.bytes[index]) ||
            token.bytes[index] == '[' || token.bytes[index] == ']';
    }
    return has_alpha_or_bracket;
}

[[nodiscard]] bool is_technical_boundary(
    const LocalToken& token,
    const char symbol) noexcept {
    const char left = token.symbol_index == 0U
        ? '\0'
        : token.bytes[token.symbol_index - 1U];
    const char right = token.symbol_index + 1U >= token.size
        ? '\0'
        : token.bytes[token.symbol_index + 1U];
    bool has_technical_marker = false;
    for (std::size_t index = 0U; index < token.size; ++index) {
        const char value = token.bytes[index];
        has_technical_marker = has_technical_marker || is_ascii_alpha(value) ||
            value == '_' || value == ':' || value == '=';
    }
    switch (symbol) {
    case '[':
    case '(':
        return has_technical_marker &&
            (is_ascii_alphanumeric(left) || left == '_' ||
             is_ascii_alphanumeric(right) || right == '_');
    case ']':
        return token.view().substr(0U, token.symbol_index).find('[') !=
            std::string_view::npos;
    case ')':
        return token.view().substr(0U, token.symbol_index).find('(') !=
            std::string_view::npos;
    case '_':
        return (is_ascii_alphanumeric(left) || left == '_') &&
            (right == '\0' || is_ascii_alphanumeric(right) || right == '_');
    default:
        return false;
    }
}

[[nodiscard]] char last_byte(const std::string_view text) noexcept {
    return text.empty() ? '\0' : text.back();
}

[[nodiscard]] char first_byte(const std::string_view text) noexcept {
    return text.empty() ? '\0' : text.front();
}

[[nodiscard]] std::string_view current_line(const std::string_view text) noexcept {
    const std::size_t break_at = text.find_last_of("\r\n");
    return break_at == std::string_view::npos ? text : text.substr(break_at + 1U);
}

[[nodiscard]] bool is_technical_bang_prefix(
    const std::string_view left,
    const std::string_view right,
    const char symbol) noexcept {
    if (symbol != '!') return false;
    if (right.empty() || !(is_ascii_alphanumeric(right.front()) || right.front() == '_' ||
            right.front() == '-')) {
        return false;
    }
    const std::string_view line = current_line(left);
    return line.empty() || line.back() == ' ' || line.back() == '\t';
}

[[nodiscard]] bool is_technical_quote(
    const std::string_view left,
    const std::string_view right,
    const char symbol) noexcept {
    if (symbol != '\'' && symbol != '"') return false;
    const char previous = last_byte(left);
    if (previous == '=' || previous == '(' || previous == '[' || previous == '{' ||
        previous == ',' || previous == ':') {
        return true;
    }
    unsigned quote_count = 0U;
    for (const char value : current_line(left)) {
        if (value == symbol) ++quote_count;
    }
    if ((quote_count % 2U) != 0U) return true;
    return !right.empty() && is_ascii_alphanumeric(first_byte(right));
}

[[nodiscard]] bool is_decimal_list_prefix(const std::string_view text) noexcept {
    std::string_view line = current_line(text);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1U);
    }
    if (line.empty()) return false;
    for (const char value : line) {
        if (!is_ascii_digit_local(value)) return false;
    }
    return true;
}

[[nodiscard]] std::string_view chinese_form(const char symbol) noexcept {
    switch (symbol) {
    case '.': return "。";
    case ':': return "：";
    case ',': return "，";
    case '?': return "？";
    case '!': return "！";
    case '[': return "【";
    case ']': return "】";
    case '(': return "（";
    case ')': return "）";
    default: return {};
    }
}

}  // namespace

bool SmartPunctuationEngine::is_ascii_digit(const char value) noexcept {
    return is_ascii_digit_local(value);
}

SmartPunctuationDecision SmartPunctuationEngine::decide(
    const SmartPunctuationContext& context) const noexcept {
    if (context.symbol == '/') {
        return {SmartPunctuationAction::literal, "PUNC-SLASH-ASCII", {}, "PATH_OR_FRACTION"};
    }

    const std::string_view chinese = chinese_form(context.symbol);
    if (context.composing) {
        return {SmartPunctuationAction::transform, "PUNC-DEFAULT", chinese, "COMPOSITION"};
    }

    const LocalToken local = make_local_token(
        context.left_text, context.symbol, context.right_text);
    const std::string_view token = local.view();

    if (context.symbol != ',' &&
        (is_url_scheme_colon(local, context.symbol) || is_url_token(token))) {
        return {SmartPunctuationAction::literal, "PUNC-URL", chinese, "URL"};
    }
    if (context.symbol != ',' && is_email_token(token)) {
        return {SmartPunctuationAction::literal, "PUNC-EMAIL", chinese, "EMAIL"};
    }
    if (context.symbol != ',' && is_path_token(token)) {
        return {SmartPunctuationAction::literal, "PUNC-PATH", chinese, "PATH"};
    }

    if (context.symbol == '.' && valid_ipv4(token)) {
        return {SmartPunctuationAction::literal, "PUNC-DOT-IPV4", chinese, "IPV4"};
    }
    if (context.symbol == '.' && valid_decimal(token)) {
        return {SmartPunctuationAction::literal, "PUNC-DOT-DECIMAL", chinese, "DECIMAL"};
    }
    if (context.symbol == '.' && valid_numeric_version(token)) {
        return {SmartPunctuationAction::literal, "PUNC-DOT-VERSION", chinese, "VERSION"};
    }
    if (is_filename_dot(local, context.symbol)) {
        return {SmartPunctuationAction::literal, "PUNC-FILENAME", chinese, "FILENAME"};
    }
    if (context.symbol == ':' && valid_time(token)) {
        return {SmartPunctuationAction::literal, "PUNC-COLON-TIME", chinese, "TIME"};
    }
    if (context.symbol == ':' && valid_ratio(token)) {
        return {SmartPunctuationAction::literal, "PUNC-COLON-RATIO", chinese, "RATIO"};
    }
    if (context.symbol == ',' && valid_grouped_number(token)) {
        return {SmartPunctuationAction::literal, "PUNC-COMMA-THOUSANDS", chinese, "NUMBER"};
    }
    if ((context.symbol == ':' || context.symbol == ',') && is_technical_infix(local)) {
        return {SmartPunctuationAction::literal, "PUNC-TECHNICAL-INFIX", chinese, "TECHNICAL"};
    }
    if (is_technical_boundary(local, context.symbol)) {
        return {SmartPunctuationAction::literal,
            "PUNC-TECHNICAL-BOUNDARY", chinese, "TECHNICAL"};
    }
    if (is_technical_bang_prefix(
            context.left_text, context.right_text, context.symbol)) {
        return {SmartPunctuationAction::literal,
            "PUNC-TECHNICAL-PREFIX", chinese, "TECHNICAL"};
    }
    if (is_technical_quote(
            context.left_text, context.right_text, context.symbol)) {
        return {SmartPunctuationAction::literal,
            "PUNC-TECHNICAL-QUOTE", chinese, "TECHNICAL"};
    }
    if (!context.right_text.empty() && is_numeric_punctuation_candidate(token)) {
        return {SmartPunctuationAction::transform, "PUNC-NUMERIC-INVALID", chinese, "CHINESE_TEXT"};
    }

    // The user's explicit two-key rule: the first period immediately after a
    // digit is ASCII; pressing period again produces the Chinese full stop.
    // Do not leave the first period provisional and rewrite it when prose is
    // typed next -- that is exactly how `1.文本` regressed into `1。文本`.
    if (context.symbol == '.' &&
        is_ascii_digit_local(last_byte(context.left_text)) &&
        context.right_text.empty()) {
        return {SmartPunctuationAction::literal,
            is_decimal_list_prefix(context.left_text)
                ? "PUNC-DECIMAL-LIST"
                : "PUNC-DOT-AFTER-DIGIT",
            chinese, "SEQUENCE"};
    }

    const bool numeric_provisional_symbol =
        context.symbol == ':' || context.symbol == ',';
    if (numeric_provisional_symbol &&
        is_ascii_digit_local(last_byte(context.left_text)) && context.right_text.empty()) {
        return {SmartPunctuationAction::provisional,
            context.symbol == ',' ? "PUNC-COMMA-GROUP-PENDING" : "PUNC-NUMERIC-PENDING",
            chinese, "AMBIGUOUS"};
    }

    if (!context.right_text.empty() &&
        is_ascii_digit_local(last_byte(context.left_text)) &&
        is_ascii_digit_local(first_byte(context.right_text))) {
        return {SmartPunctuationAction::transform, "PUNC-NUMERIC-INVALID", chinese, "CHINESE_TEXT"};
    }

    return {SmartPunctuationAction::transform,
        chinese.empty() ? "PUNC-DEFAULT" : "PUNC-CHINESE",
        chinese, "CHINESE_TEXT"};
}

SmartPunctuationResolution SmartPunctuationEngine::resolve_provisional(
    const char symbol,
    const char next_character,
    const std::string_view provisional_rule_id,
    const std::string_view accumulated_text) const noexcept {
    const std::string_view chinese = chinese_form(symbol);
    const bool ascii_token_pending = provisional_rule_id == "PUNC-ASCII-TOKEN-PENDING";
    if (symbol == ',' && provisional_rule_id == "PUNC-COMMA-GROUP-PENDING" &&
        is_ascii_digit_local(next_character)) {
        bool accumulated_digits = accumulated_text.size() <= 2U;
        for (const char value : accumulated_text) {
            accumulated_digits = accumulated_digits && is_ascii_digit_local(value);
        }
        if (accumulated_digits && accumulated_text.size() < 2U) {
            return {false, "PUNC-PENDING-GROUP-DIGIT", chinese, true};
        }
        if (accumulated_digits && accumulated_text.size() == 2U) {
            return {true, "PUNC-PENDING-GROUP-COMPLETE", chinese};
        }
    }
    if (is_ascii_digit_local(next_character) ||
        (ascii_token_pending && is_ascii_alphanumeric(next_character))) {
        return {true,
            ascii_token_pending ? "PUNC-PENDING-ASCII-TOKEN" : "PUNC-PENDING-DIGIT",
            chinese};
    }
    return {false, "PUNC-PENDING-CHINESE", chinese};
}

}  // namespace piinput
