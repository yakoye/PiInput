#pragma once

#include "piinput/punctuation.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

enum class InputSchema {
    full,
    flypy,
    natural,
    mspy,
    abc,
};

enum class RowNavigationAction {
    next_row,
    previous_row,
};

struct GeneralSettings {
    InputSchema schema{InputSchema::flypy};
    bool hot_reload{true};

    bool operator==(const GeneralSettings&) const = default;
};

struct PinyinSettings {
    bool uv_compatibility{true};
    bool accept_u_colon{true};
    bool incomplete_candidates{true};
    std::uint32_t prefix_beam_width{32U};
    std::uint32_t prefix_scan_limit{4096U};

    bool operator==(const PinyinSettings&) const = default;
};

struct CandidateSettings {
    std::uint32_t items_per_row{6U};
    std::uint32_t visible_rows{3U};
    std::uint32_t max_items{90U};
    bool horizontal{true};
    RowNavigationAction equal_key{RowNavigationAction::next_row};
    RowNavigationAction minus_key{RowNavigationAction::previous_row};
    RowNavigationAction down_key{RowNavigationAction::next_row};
    RowNavigationAction up_key{RowNavigationAction::previous_row};

    bool operator==(const CandidateSettings&) const = default;
};

struct EnglishSettings {
    bool enabled{false};
    bool builtin_dictionary{true};
    bool user_dictionary{true};
    bool user_learning{true};
    std::uint32_t items_per_row{6U};

    bool operator==(const EnglishSettings&) const = default;
};

struct SettingsSnapshot {
    std::uint64_t generation{0U};
    GeneralSettings general;
    PinyinSettings pinyin;
    CandidateSettings candidates;
    EnglishSettings english;
    PunctuationMode punctuation{PunctuationMode::chinese};

    bool operator==(const SettingsSnapshot&) const = default;
};

struct SettingsParseResult {
    SettingsSnapshot settings;
    std::vector<std::string> errors;
    bool document_fatal{false};
};

[[nodiscard]] SettingsSnapshot default_settings();
[[nodiscard]] SettingsParseResult parse_settings_text(
    std::string_view text,
    const SettingsSnapshot& previous);
[[nodiscard]] std::string serialize_default_settings();

}  // namespace piinput
