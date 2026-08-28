#include "settings_file.h"

#include "piinput/settings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace piinput::windows {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

std::string trim_copy(std::string_view value) {
    while (!value.empty() &&
        (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() &&
        (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1U);
    }
    return std::string(value);
}

bool is_candidate_managed_key(const std::string_view line) {
    const auto trimmed = trim_copy(line);
    return trimmed.starts_with("font_size=") || trimmed.starts_with("window_height=") ||
        trimmed.starts_with("visible_rows=");
}

bool is_general_managed_key(const std::string_view line) {
    const auto trimmed = trim_copy(line);
    return trimmed.starts_with("schema=") || trimmed.starts_with("default_language=");
}

bool is_section(const std::string_view line) {
    const auto trimmed = trim_copy(line);
    return trimmed.size() >= 3U && trimmed.front() == '[' && trimmed.back() == ']';
}

bool is_candidate_section(const std::string_view line) {
    return trim_copy(line) == "[candidates]";
}

bool is_general_section(const std::string_view line) {
    return trim_copy(line) == "[general]";
}

std::string schema_text(const InputSchema schema) {
    return schema == InputSchema::full ? "full" : "flypy";
}

std::string language_text(const DefaultInputLanguage language) {
    return language == DefaultInputLanguage::english ? "english" : "chinese";
}

std::string update_visual_settings_text(
    std::string text,
    const CandidateVisualFileSettings settings) {
    bool had_bom = text.starts_with(kBom);
    if (had_bom) text.erase(0U, kBom.size());

    std::vector<std::string> lines;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = text.substr(offset,
            newline == std::string::npos ? std::string::npos : newline - offset);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (newline == std::string::npos) break;
        offset = newline + 1U;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();

    std::vector<std::string> result;
    result.reserve(lines.size() + 3U);
    bool in_general = false;
    bool in_candidates = false;
    bool found_general = false;
    bool found_candidates = false;
    bool general_inserted = false;
    bool candidate_inserted = false;
    const auto insert_general = [&] {
        result.push_back("schema=" + schema_text(settings.schema));
        result.push_back("default_language=" + language_text(settings.default_language));
        general_inserted = true;
    };
    const auto insert_candidates = [&] {
        result.push_back("font_size=" + std::to_string(settings.font_size));
        result.push_back("window_height=" + std::to_string(settings.window_height));
        result.push_back("visible_rows=" + std::to_string(settings.visible_rows));
        candidate_inserted = true;
    };

    for (const auto& line : lines) {
        if (is_section(line)) {
            if (in_general && !general_inserted) insert_general();
            if (in_candidates && !candidate_inserted) insert_candidates();
            in_general = is_general_section(line);
            in_candidates = is_candidate_section(line);
            if (in_general) found_general = true;
            if (in_candidates) found_candidates = true;
        }
        if (in_general && is_general_managed_key(line)) continue;
        if (in_candidates && is_candidate_managed_key(line)) continue;
        result.push_back(line);
    }
    if (in_general && !general_inserted) insert_general();
    if (in_candidates && !candidate_inserted) insert_candidates();
    if (!found_general) {
        if (!result.empty() && !result.back().empty()) result.push_back({});
        result.push_back("[general]");
        insert_general();
    }
    if (!found_candidates) {
        if (!result.empty() && !result.back().empty()) result.push_back({});
        result.push_back("[candidates]");
        insert_candidates();
    }

    std::string updated;
    if (had_bom) updated.append(kBom);
    for (const auto& line : result) {
        updated += line;
        updated.push_back('\n');
    }
    return updated;
}

bool valid(const CandidateVisualFileSettings settings) noexcept {
    return settings.font_size >= 10U && settings.font_size <= 28U &&
        settings.window_height >= 20U && settings.window_height <= 72U &&
        settings.visible_rows >= 1U && settings.visible_rows <= 6U &&
        (settings.schema == InputSchema::full || settings.schema == InputSchema::flypy);
}

}  // namespace

std::uint32_t step_numeric_setting(
    const std::uint32_t value,
    const int wheel_delta,
    const std::uint32_t minimum,
    const std::uint32_t maximum) noexcept {
    if (minimum >= maximum || wheel_delta == 0) {
        return (std::clamp)(value, minimum, maximum);
    }
    const auto magnitude = static_cast<unsigned>(
        wheel_delta < 0 ? -static_cast<long long>(wheel_delta) : wheel_delta);
    const std::uint32_t steps = (std::max)(1U, magnitude / WHEEL_DELTA);
    const std::uint32_t normalized = (std::clamp)(value, minimum, maximum);
    if (wheel_delta > 0) {
        return maximum - normalized < steps ? maximum : normalized + steps;
    }
    return normalized - minimum < steps ? minimum : normalized - steps;
}

// One assignment the settings window owns. Anything not listed here -- a
// comment, a hand-added key, a section this build does not know -- survives a
// save untouched.
struct Assignment final {
    std::string section;
    std::string key;
    std::string value;
};

[[nodiscard]] std::string section_name(const std::string_view line) {
    const auto trimmed = trim_copy(line);
    if (trimmed.size() < 3U || trimmed.front() != '[' || trimmed.back() != ']') return {};
    return trimmed.substr(1U, trimmed.size() - 2U);
}

[[nodiscard]] std::string line_key(const std::string_view line) {
    const auto trimmed = trim_copy(line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') return {};
    const auto equals = trimmed.find('=');
    if (equals == std::string::npos) return {};
    return trim_copy(std::string_view(trimmed).substr(0U, equals));
}

[[nodiscard]] std::vector<std::string> split_lines(std::string text, bool& had_bom) {
    had_bom = text.starts_with(kBom);
    if (had_bom) text.erase(0U, kBom.size());
    std::vector<std::string> lines;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = text.substr(offset,
            newline == std::string::npos ? std::string::npos : newline - offset);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (newline == std::string::npos) break;
        offset = newline + 1U;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// Rewrites exactly the listed keys in place, keeping their section. Keys whose
// section is missing get the section appended at the end.
[[nodiscard]] std::string apply_assignments(
    std::string text,
    const std::vector<Assignment>& assignments) {
    bool had_bom = false;
    auto lines = split_lines(std::move(text), had_bom);

    std::vector<std::string> sections;
    for (const auto& assignment : assignments) {
        const std::string name(assignment.section);
        if (std::find(sections.begin(), sections.end(), name) == sections.end()) {
            sections.push_back(name);
        }
    }
    std::vector<bool> written(sections.size(), false);
    const auto section_index = [&sections](const std::string& name) -> std::size_t {
        const auto found = std::find(sections.begin(), sections.end(), name);
        return found == sections.end()
            ? sections.size()
            : static_cast<std::size_t>(std::distance(sections.begin(), found));
    };
    const auto emit_section = [&](std::vector<std::string>& out, const std::size_t index) {
        if (index >= sections.size() || written[index]) return;
        for (const auto& assignment : assignments) {
            if (assignment.section != sections[index]) continue;
            out.push_back(std::string(assignment.key) + "=" + assignment.value);
        }
        written[index] = true;
    };

    std::vector<std::string> result;
    result.reserve(lines.size() + assignments.size() + sections.size() * 2U);
    std::size_t current = sections.size();
    for (const auto& line : lines) {
        const auto name = section_name(line);
        if (!name.empty()) {
            emit_section(result, current);
            current = section_index(name);
            result.push_back(line);
            continue;
        }
        if (current < sections.size()) {
            const auto key = line_key(line);
            const bool owned = !key.empty() && std::any_of(
                assignments.begin(), assignments.end(), [&](const Assignment& assignment) {
                    return assignment.section == sections[current] && assignment.key == key;
                });
            if (owned) continue;
        }
        result.push_back(line);
    }
    emit_section(result, current);
    for (std::size_t index = 0U; index < sections.size(); ++index) {
        if (written[index]) continue;
        if (!result.empty() && !result.back().empty()) result.push_back({});
        result.push_back("[" + sections[index] + "]");
        emit_section(result, index);
    }

    std::string output;
    if (had_bom) output.append(kBom);
    for (const auto& line : result) {
        output.append(line);
        output.push_back('\n');
    }
    return output;
}

CandidateVisualFileSettings load_candidate_visual_settings(
    const std::filesystem::path& path,
    std::string& error) noexcept {
    error.clear();
    try {
        if (!std::filesystem::exists(path)) return {};
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot read settings.";
            return {};
        }
        const std::string text{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto defaults = default_settings();
        const auto parsed = parse_settings_text(text, defaults);
        if (parsed.document_fatal) {
            error = "Settings file is damaged.";
            return {};
        }
        if (!parsed.errors.empty()) error = "Some settings are invalid.";
        return {
            parsed.settings.candidates.font_size,
            parsed.settings.candidates.window_height,
            parsed.settings.general.schema,
            parsed.settings.general.default_language,
            parsed.settings.candidates.visible_rows,
        };
    } catch (...) {
        error = "Cannot read settings.";
        return {};
    }
}

bool save_candidate_visual_settings_atomic(
    const std::filesystem::path& path,
    const CandidateVisualFileSettings settings,
    std::string& error) noexcept {
    error.clear();
    if (!valid(settings)) {
        error = "Candidate visual settings are out of range.";
        return false;
    }
    try {
        std::filesystem::create_directories(path.parent_path());
        std::string original;
        if (std::filesystem::exists(path)) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "Cannot read settings.";
                return false;
            }
            original.assign(
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        } else {
            original = serialize_default_settings();
        }
        const auto updated = update_visual_settings_text(std::move(original), settings);
        const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
            output.flush();
            if (!output) {
                error = "Cannot write settings.";
                std::filesystem::remove(temporary);
                return false;
            }
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            error = "Cannot replace settings.";
            std::filesystem::remove(temporary);
            return false;
        }
        return true;
    } catch (...) {
        error = "Cannot write settings.";
        return false;
    }
}

SettingsSnapshot load_all_settings(
    const std::filesystem::path& path,
    std::string& error) noexcept {
    error.clear();
    try {
        if (!std::filesystem::exists(path)) return default_settings();
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Cannot read settings.";
            return default_settings();
        }
        const std::string text{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        auto parsed = parse_settings_text(text, default_settings());
        if (parsed.document_fatal) {
            error = "Settings file is damaged.";
            return default_settings();
        }
        if (!parsed.errors.empty()) error = "Some settings are invalid.";
        return parsed.settings;
    } catch (...) {
        error = "Cannot read settings.";
        return default_settings();
    }
}

bool save_all_settings_atomic(
    const std::filesystem::path& path,
    const SettingsSnapshot& settings,
    std::string& error) noexcept {
    error.clear();
    if (settings.custom_shortcuts.size() > max_custom_shortcuts) {
        error = "Too many shortcut rows.";
        return false;
    }
    const auto boolean = [](const bool value) -> std::string {
        return value ? "true" : "false";
    };
    const auto row_key = [](const RowNavigationAction action) -> std::string {
        return action == RowNavigationAction::previous_row ? "previous_row" : "next_row";
    };
    const auto schema_name = [](const InputSchema schema) -> std::string {
        switch (schema) {
        case InputSchema::full: return "full";
        case InputSchema::natural: return "natural";
        case InputSchema::mspy: return "mspy";
        case InputSchema::abc: return "abc";
        case InputSchema::flypy: break;
        }
        return "flypy";
    };
    const auto punctuation_name = [](const PunctuationMode mode) -> std::string {
        switch (mode) {
        case PunctuationMode::english: return "english";
        case PunctuationMode::chinese: break;
        }
        return "chinese";
    };
    const auto hotkey_name = [](const CommandHotkey hotkey) -> std::string {
        switch (hotkey) {
        case CommandHotkey::ctrl_grave: return "ctrl_grave";
        case CommandHotkey::disabled: return "disabled";
        case CommandHotkey::ctrl_alt_grave: break;
        }
        return "ctrl_alt_grave";
    };

    std::vector<Assignment> assignments{
        {"general", "schema", schema_name(settings.general.schema)},
        {"general", "default_language",
            settings.general.default_language == DefaultInputLanguage::english
                ? "english" : "chinese"},
        {"general", "hot_reload", boolean(settings.general.hot_reload)},
        {"general", "symbol_tool", settings.general.symbol_tool},
        {"pinyin", "uv_compatibility", boolean(settings.pinyin.uv_compatibility)},
        {"pinyin", "accept_u_colon", boolean(settings.pinyin.accept_u_colon)},
        {"pinyin", "incomplete_candidates", boolean(settings.pinyin.incomplete_candidates)},
        {"pinyin", "simplified_pinyin", boolean(settings.pinyin.simplified_pinyin)},
        {"pinyin", "user_learning", boolean(settings.pinyin.user_learning)},
        {"pinyin", "prefix_beam_width", std::to_string(settings.pinyin.prefix_beam_width)},
        {"pinyin", "prefix_scan_limit", std::to_string(settings.pinyin.prefix_scan_limit)},
        {"candidates", "show_composition", boolean(settings.candidates.show_composition)},
        {"candidates", "items_per_row", std::to_string(settings.candidates.items_per_row)},
        {"candidates", "visible_rows", std::to_string(settings.candidates.visible_rows)},
        {"candidates", "max_items", std::to_string(settings.candidates.max_items)},
        {"candidates", "font_size", std::to_string(settings.candidates.font_size)},
        {"candidates", "window_height", std::to_string(settings.candidates.window_height)},
        {"candidates", "horizontal", boolean(settings.candidates.horizontal)},
        {"candidates", "equal_key", row_key(settings.candidates.equal_key)},
        {"candidates", "minus_key", row_key(settings.candidates.minus_key)},
        {"candidates", "down_key", row_key(settings.candidates.down_key)},
        {"candidates", "up_key", row_key(settings.candidates.up_key)},
        {"punctuation", "mode", punctuation_name(settings.punctuation)},
        {"punctuation", "bracket_style",
            settings.punctuation_bracket_style == PunctuationBracketStyle::wechat
                ? "wechat" : "sogou"},
        {"commands", "enabled", boolean(settings.commands.enabled)},
        {"commands", "hotkey", hotkey_name(settings.commands.hotkey)},
        {"commands", "middle_dot_alias", boolean(settings.commands.middle_dot_alias)},
        {"english", "enabled", boolean(settings.english.enabled)},
        {"english", "chinese_mode_completion",
            boolean(settings.english.chinese_mode_completion)},
        {"english", "builtin_dictionary", boolean(settings.english.builtin_dictionary)},
        {"english", "user_dictionary", boolean(settings.english.user_dictionary)},
        {"english", "user_learning", boolean(settings.english.user_learning)},
        {"english", "items_per_row", std::to_string(settings.english.items_per_row)},
    };
    assignments.push_back(
        {"shortcuts", "count", std::to_string(settings.custom_shortcuts.size())});
    for (std::size_t index = 0U; index < settings.custom_shortcuts.size(); ++index) {
        const auto& shortcut = settings.custom_shortcuts[index];
        const std::string suffix = std::to_string(index + 1U);
        assignments.push_back({"shortcuts", "aliases_" + suffix, shortcut.aliases});
        assignments.push_back(
            {"shortcuts", "position_" + suffix, std::to_string(shortcut.position)});
        assignments.push_back({"shortcuts", "icon_" + suffix, shortcut.icon});
        assignments.push_back({"shortcuts", "name_" + suffix, shortcut.name});
        assignments.push_back({"shortcuts", "target_" + suffix, shortcut.target});
    }

    try {
        std::filesystem::create_directories(path.parent_path());
        std::string original;
        if (std::filesystem::exists(path)) {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "Cannot read settings.";
                return false;
            }
            original.assign(
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        } else {
            original = serialize_default_settings();
        }
        const auto updated = apply_assignments(std::move(original), assignments);
        const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
            output.flush();
            if (!output) {
                error = "Cannot write settings.";
                std::filesystem::remove(temporary);
                return false;
            }
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            error = "Cannot replace settings.";
            std::filesystem::remove(temporary);
            return false;
        }
        return true;
    } catch (...) {
        error = "Cannot write settings.";
        return false;
    }
}

}  // namespace piinput::windows
