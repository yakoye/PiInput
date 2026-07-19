#include "piinput/settings.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace piinput {
namespace {

[[nodiscard]] std::string_view trim(const std::string_view value) noexcept {
    constexpr std::string_view whitespace = " \t\r";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool is_valid_utf8(const std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t continuation_count = 0U;
        std::uint32_t code_point = 0U;
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            continuation_count = 1U;
            code_point = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuation_count = 2U;
            code_point = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuation_count = 3U;
            code_point = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuation_count >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if ((continuation_count == 2U && code_point < 0x800U) ||
            (continuation_count == 3U && code_point < 0x10000U) ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU) || code_point > 0x10FFFFU) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

[[nodiscard]] std::optional<bool> parse_bool(const std::string_view value) noexcept {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> parse_integer(const std::string_view value) noexcept {
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<InputSchema> parse_schema(const std::string_view value) noexcept {
    if (value == "full") {
        return InputSchema::full;
    }
    if (value == "flypy") {
        return InputSchema::flypy;
    }
    if (value == "natural") {
        return InputSchema::natural;
    }
    if (value == "mspy") {
        return InputSchema::mspy;
    }
    if (value == "abc") {
        return InputSchema::abc;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<RowNavigationAction> parse_navigation(
    const std::string_view value) noexcept {
    if (value == "next_row") {
        return RowNavigationAction::next_row;
    }
    if (value == "previous_row") {
        return RowNavigationAction::previous_row;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PunctuationMode> parse_punctuation(
    const std::string_view value) noexcept {
    if (value == "chinese") {
        return PunctuationMode::chinese;
    }
    if (value == "english") {
        return PunctuationMode::english;
    }
    if (value == "programmer") {
        return PunctuationMode::programmer;
    }
    return std::nullopt;
}

void add_error(
    SettingsParseResult& result,
    const std::size_t line,
    const std::string_view section,
    const std::string_view key,
    const std::string_view reason) {
    result.errors.push_back(
        "line " + std::to_string(line) + " [" + std::string(section) + "] key '" +
        std::string(key) + "': " + std::string(reason));
}

template <typename Destination, typename Parser>
void assign_parsed(
    SettingsParseResult& result,
    Destination& destination,
    Parser parser,
    const std::string_view value,
    const std::size_t line,
    const std::string_view section,
    const std::string_view key) {
    const auto parsed = parser(value);
    if (!parsed) {
        add_error(result, line, section, key, "invalid value");
        return;
    }
    destination = *parsed;
}

enum class CandidateNumericField {
    items_per_row,
    visible_rows,
    max_items,
};

struct CandidateAssignment {
    std::uint32_t previous_value;
    std::optional<std::size_t> line;
};

using CandidateAssignments = std::array<CandidateAssignment, 3U>;

[[nodiscard]] CandidateAssignment& candidate_assignment(
    CandidateAssignments& assignments,
    const CandidateNumericField field) noexcept {
    return assignments[static_cast<std::size_t>(field)];
}

void parse_general(
    SettingsParseResult& result,
    const std::string_view key,
    const std::string_view value,
    const std::size_t line) {
    if (key == "schema") {
        assign_parsed(result, result.settings.general.schema, parse_schema, value, line, "general", key);
    } else if (key == "hot_reload") {
        assign_parsed(result, result.settings.general.hot_reload, parse_bool, value, line, "general", key);
    }
}

void parse_pinyin(
    SettingsParseResult& result,
    const std::string_view key,
    const std::string_view value,
    const std::size_t line) {
    if (key == "uv_compatibility") {
        assign_parsed(result, result.settings.pinyin.uv_compatibility, parse_bool, value, line, "pinyin", key);
    } else if (key == "accept_u_colon") {
        assign_parsed(result, result.settings.pinyin.accept_u_colon, parse_bool, value, line, "pinyin", key);
    } else if (key == "incomplete_candidates") {
        assign_parsed(
            result, result.settings.pinyin.incomplete_candidates, parse_bool, value, line, "pinyin", key);
    } else if (key == "prefix_beam_width") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 8U || *parsed > 128U) {
            add_error(result, line, "pinyin", key, "invalid value");
        } else {
            result.settings.pinyin.prefix_beam_width = *parsed;
        }
    } else if (key == "prefix_scan_limit") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 128U || *parsed > 16384U) {
            add_error(result, line, "pinyin", key, "invalid value");
        } else {
            result.settings.pinyin.prefix_scan_limit = *parsed;
        }
    }
}

void parse_candidates(
    SettingsParseResult& result,
    const std::string_view key,
    const std::string_view value,
    const std::size_t line,
    CandidateAssignments& assignments) {
    auto& candidates = result.settings.candidates;
    if (key == "items_per_row") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 5U || *parsed > 9U) {
            add_error(result, line, "candidates", key, "invalid value");
        } else {
            candidate_assignment(assignments, CandidateNumericField::items_per_row).line = line;
            candidates.items_per_row = *parsed;
        }
    } else if (key == "visible_rows") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 1U || *parsed > 5U) {
            add_error(result, line, "candidates", key, "invalid value");
        } else {
            candidate_assignment(assignments, CandidateNumericField::visible_rows).line = line;
            candidates.visible_rows = *parsed;
        }
    } else if (key == "max_items") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 9U || *parsed > 180U) {
            add_error(result, line, "candidates", key, "invalid value");
        } else {
            candidate_assignment(assignments, CandidateNumericField::max_items).line = line;
            candidates.max_items = *parsed;
        }
    } else if (key == "horizontal") {
        assign_parsed(result, candidates.horizontal, parse_bool, value, line, "candidates", key);
    } else if (key == "equal_key") {
        assign_parsed(result, candidates.equal_key, parse_navigation, value, line, "candidates", key);
    } else if (key == "minus_key") {
        assign_parsed(result, candidates.minus_key, parse_navigation, value, line, "candidates", key);
    } else if (key == "down_key") {
        assign_parsed(result, candidates.down_key, parse_navigation, value, line, "candidates", key);
    } else if (key == "up_key") {
        assign_parsed(result, candidates.up_key, parse_navigation, value, line, "candidates", key);
    }
}

[[nodiscard]] bool candidate_screen_size_is_valid(const CandidateSettings& candidates) noexcept {
    return candidates.max_items >= candidates.items_per_row * candidates.visible_rows;
}

void rollback_candidate_assignment(
    SettingsParseResult& result,
    CandidateAssignments& assignments,
    const CandidateNumericField field) {
    auto& candidates = result.settings.candidates;
    const auto& assignment = candidate_assignment(assignments, field);
    if (!assignment.line) {
        return;
    }

    std::uint32_t* destination = nullptr;
    std::string_view key;
    switch (field) {
    case CandidateNumericField::items_per_row:
        destination = &candidates.items_per_row;
        key = "items_per_row";
        break;
    case CandidateNumericField::visible_rows:
        destination = &candidates.visible_rows;
        key = "visible_rows";
        break;
    case CandidateNumericField::max_items:
        destination = &candidates.max_items;
        key = "max_items";
        break;
    }
    if (*destination == assignment.previous_value) {
        return;
    }
    *destination = assignment.previous_value;
    add_error(result, *assignment.line, "candidates", key, "invalid value");
}

void enforce_candidate_screen_size(
    SettingsParseResult& result,
    CandidateAssignments& assignments) {
    auto& candidates = result.settings.candidates;
    if (candidate_screen_size_is_valid(candidates)) {
        return;
    }

    const auto& max_assignment = candidate_assignment(assignments, CandidateNumericField::max_items);
    const auto required_items = candidates.items_per_row * candidates.visible_rows;
    if (max_assignment.line && max_assignment.previous_value >= required_items) {
        rollback_candidate_assignment(result, assignments, CandidateNumericField::max_items);
        return;
    }

    // If the preceding max_items cannot hold the requested dimensions, use a stable
    // field priority: visible_rows, then items_per_row, and finally max_items.
    // This makes both the resulting snapshot and error order independent of INI key order.
    for (const auto field : {CandidateNumericField::visible_rows,
             CandidateNumericField::items_per_row,
             CandidateNumericField::max_items}) {
        rollback_candidate_assignment(result, assignments, field);
        if (candidate_screen_size_is_valid(candidates)) {
            return;
        }
    }
}

void parse_english(
    SettingsParseResult& result,
    const std::string_view key,
    const std::string_view value,
    const std::size_t line) {
    auto& english = result.settings.english;
    if (key == "enabled") {
        assign_parsed(result, english.enabled, parse_bool, value, line, "english", key);
    } else if (key == "builtin_dictionary") {
        assign_parsed(result, english.builtin_dictionary, parse_bool, value, line, "english", key);
    } else if (key == "user_dictionary") {
        assign_parsed(result, english.user_dictionary, parse_bool, value, line, "english", key);
    } else if (key == "user_learning") {
        assign_parsed(result, english.user_learning, parse_bool, value, line, "english", key);
    } else if (key == "items_per_row") {
        const auto parsed = parse_integer(value);
        if (!parsed || *parsed < 5U || *parsed > 9U) {
            add_error(result, line, "english", key, "invalid value");
        } else {
            english.items_per_row = *parsed;
        }
    }
}

}  // namespace

SettingsSnapshot default_settings() {
    return {};
}

SettingsParseResult parse_settings_text(
    std::string_view text,
    const SettingsSnapshot& previous) {
    SettingsParseResult result{previous, {}};
    constexpr std::string_view bom = "\xEF\xBB\xBF";
    if (text.starts_with(bom)) {
        text.remove_prefix(bom.size());
    }
    if (!is_valid_utf8(text)) {
        add_error(result, 1U, "document", "<encoding>", "invalid UTF-8");
        return result;
    }
    std::string section;
    CandidateAssignments candidate_assignments{{
        {previous.candidates.items_per_row, std::nullopt},
        {previous.candidates.visible_rows, std::nullopt},
        {previous.candidates.max_items, std::nullopt},
    }};
    std::size_t line_number = 0U;
    while (!text.empty()) {
        ++line_number;
        const auto newline = text.find('\n');
        const auto raw_line = text.substr(0U, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1U);
        const auto line = trim(raw_line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        if (line.front() == '[') {
            if (line.size() < 3U || line.back() != ']') {
                add_error(result, line_number, "syntax", "<section>", "malformed section");
                continue;
            }
            const auto parsed_section = trim(line.substr(1U, line.size() - 2U));
            if (parsed_section.empty()) {
                add_error(result, line_number, "syntax", "<section>", "malformed section");
                continue;
            }
            section.assign(parsed_section);
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            add_error(result, line_number, section.empty() ? "syntax" : section, "<syntax>", "missing equals");
            continue;
        }
        const auto key = trim(line.substr(0U, equals));
        const auto value = trim(line.substr(equals + 1U));
        if (key.empty()) {
            add_error(result, line_number, section.empty() ? "syntax" : section, "<empty>", "empty key");
            continue;
        }

        if (section == "general") {
            parse_general(result, key, value, line_number);
        } else if (section == "pinyin") {
            parse_pinyin(result, key, value, line_number);
        } else if (section == "candidates") {
            parse_candidates(result, key, value, line_number, candidate_assignments);
        } else if (section == "english") {
            parse_english(result, key, value, line_number);
        } else if (section == "punctuation" && key == "mode") {
            assign_parsed(
                result, result.settings.punctuation, parse_punctuation, value, line_number, "punctuation", key);
        }
    }
    enforce_candidate_screen_size(result, candidate_assignments);
    return result;
}

std::string serialize_default_settings() {
    return
        "[general]\n"
        "schema=flypy\n"
        "hot_reload=true\n"
        "[pinyin]\n"
        "uv_compatibility=true\n"
        "accept_u_colon=true\n"
        "incomplete_candidates=true\n"
        "prefix_beam_width=32\n"
        "prefix_scan_limit=4096\n"
        "[candidates]\n"
        "items_per_row=6\n"
        "visible_rows=3\n"
        "max_items=90\n"
        "horizontal=true\n"
        "equal_key=next_row\n"
        "minus_key=previous_row\n"
        "down_key=next_row\n"
        "up_key=previous_row\n"
        "[english]\n"
        "enabled=false\n"
        "builtin_dictionary=true\n"
        "user_dictionary=true\n"
        "user_learning=true\n"
        "items_per_row=6\n"
        "[punctuation]\n"
        "mode=chinese\n";
}

}  // namespace piinput
