#pragma once

#include "piinput/punctuation.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace piinput {

enum class InputSchema {
    full,
    flypy,
    natural,
    mspy,
    abc,
};

enum class DefaultInputLanguage {
    chinese,
    english,
};

enum class RowNavigationAction {
    next_row,
    previous_row,
};

// 候选窗外观。`system` 读 Windows 的 AppsUseLightTheme，并在用户改系统设置时
// 跟着变——它是默认挡，因为一个浮在别人窗口上的小面板，跟宿主环境一致是最不
// 容易出错的选择。
enum class CandidateTheme : std::uint8_t {
    light,
    dark,
    system,
};

enum class CommandHotkey : std::uint8_t {
    ctrl_alt_grave,
    ctrl_grave,
    disabled,
};

struct GeneralSettings {
    InputSchema schema{InputSchema::flypy};
    DefaultInputLanguage default_language{DefaultInputLanguage::chinese};
    bool hot_reload{true};
    // External program the tray's symbol entry launches. Empty means PiInput
    // falls back to its own built-in symbol candidates.
    std::string symbol_tool;

    bool operator==(const GeneralSettings&) const = default;
};

struct PinyinSettings {
    bool uv_compatibility{true};
    bool accept_u_colon{true};
    bool incomplete_candidates{true};
    // Full pinyin only:每个音节只打声母，或与全拼混合（zsjs / srf / sruf）。
    bool simplified_pinyin{true};
    bool user_learning{true};
    std::uint32_t prefix_beam_width{32U};
    std::uint32_t prefix_scan_limit{4096U};

    bool operator==(const PinyinSettings&) const = default;
};

struct CandidateSettings {
    // 候选窗要不要把正在打的字母显示出来。
    //
    // 显示合成串本来是应用的职责，绝大多数应用也做了。终端不做：MobaXterm 里
    // 候选正常，而你打的字母一个都看不见，只能靠候选反推。
    //
    // 只有开和关，没有「自动」。自动挡判据是应用报不报得出自己合成串的位置，
    // 而 Chromium 系会在系统光标和 TSF 两种来源之间摇摆，判定跟着翻来覆去，
    // 在 ChatGPT 里这一行忽有忽无。一个稳定的错误也好过一个闪烁的正确。
    bool show_composition{true};
    CandidateTheme theme{CandidateTheme::system};
    std::uint32_t items_per_row{6U};
    std::uint32_t visible_rows{5U};
    std::uint32_t max_items{90U};
    std::uint32_t font_size{16U};
    std::uint32_t window_height{40U};
    bool horizontal{true};
    RowNavigationAction equal_key{RowNavigationAction::next_row};
    RowNavigationAction minus_key{RowNavigationAction::previous_row};
    RowNavigationAction down_key{RowNavigationAction::next_row};
    RowNavigationAction up_key{RowNavigationAction::previous_row};

    bool operator==(const CandidateSettings&) const = default;
};

struct EnglishSettings {
    bool enabled{false};
    // English candidates while typing Chinese. Separate from `enabled`, which
    // governs English mode after Shift: turning this on must not change what
    // English mode does, and vice versa.
    bool chinese_mode_completion{false};
    bool builtin_dictionary{true};
    bool user_dictionary{true};
    bool user_learning{true};
    std::uint32_t items_per_row{6U};

    bool operator==(const EnglishSettings&) const = default;
};

struct CommandSettings final {
    bool enabled{true};
    CommandHotkey hotkey{CommandHotkey::ctrl_alt_grave};
    bool middle_dot_alias{false};

    bool operator==(const CommandSettings&) const = default;
};

inline constexpr std::size_t max_custom_shortcuts = 64U;

struct CustomShortcutSettings final {
    // Comma-separated Latin aliases, for example "git,github".
    std::string aliases;
    std::uint32_t position{2U};
    // Optional text/emoji displayed before the candidate name.
    std::string icon;
    std::string name;
    // A file, URL or executable opened by Windows. Prefix with "cmd:" to run
    // an explicit command through cmd.exe.
    std::string target;

    bool operator==(const CustomShortcutSettings&) const = default;
};

// Phrases outnumber launchers by a lot. A person accumulates addresses, ID
// numbers, mail addresses and stock replies until there are dozens; the 64 that
// suffice for programs do not suffice here.
inline constexpr std::size_t max_custom_phrases = 128U;

// A stored piece of text reached from the candidate row. Deliberately a
// separate table from CustomShortcutSettings rather than another meaning for
// its `target`: that field is "a file, URL or executable opened by Windows", so
// an address stored there would be handed to the shell to launch, and a command
// line stored as a phrase would be typed into the document. Neither mistake is
// cheap, and separate tables make both impossible.
struct CustomPhraseSettings final {
    // Comma-separated Latin aliases, for example "dzjl". Empty means the
    // trigger is derived from `text` itself -- see derived_phrase_aliases and
    // leading_digit_run.
    std::string aliases;
    std::uint32_t position{2U};
    // Shown in the candidate row in place of the text. Empty shows the text.
    std::string label;
    std::string text;

    bool operator==(const CustomPhraseSettings&) const = default;
};

// What an empty `aliases` field falls back to, decided by how `text` begins.
enum class PhraseTrigger : std::uint8_t {
    // Nothing can be derived -- a symbol or an unreadable character leads. The
    // row needs an alias typed by hand; the settings UI says so.
    none,
    // Leading ASCII letters are their own trigger: "github.com" fires on gi,
    // git, gith...
    latin,
    // Leading Chinese characters: the reading's initials fire it, so
    // 北京市海淀区… answers to b, bj, bjs. Derived in the Engine, which is
    // where the lexicon lives.
    chinese,
    // Leading digits. These never open a composition -- see the digit
    // suggestion path -- so the trigger is the typed run itself.
    digits,
};

[[nodiscard]] PhraseTrigger phrase_trigger_for(std::string_view text) noexcept;

// Every Latin prefix of `text` that should fire it, shortest first. Two
// characters is the floor: a single letter would fire on most of the alphabet.
inline constexpr std::size_t min_derived_alias_length = 2U;
inline constexpr std::size_t max_derived_alias_length = 8U;
[[nodiscard]] std::vector<std::string> derived_phrase_aliases(std::string_view text);

// The leading run of ASCII digits, empty when `text` does not start with one.
[[nodiscard]] std::string leading_digit_run(std::string_view text);

// Below this many typed digits the suggestion stays closed. One or two would
// fire constantly -- dates, prices and version numbers are full of short runs.
inline constexpr std::size_t min_digit_suggestion_length = 3U;

struct SettingsSnapshot {
    std::uint64_t generation{0U};
    GeneralSettings general;
    PinyinSettings pinyin;
    CandidateSettings candidates;
    EnglishSettings english;
    CommandSettings commands;
    std::vector<CustomShortcutSettings> custom_shortcuts;
    std::vector<CustomPhraseSettings> custom_phrases;
    PunctuationMode punctuation{PunctuationMode::chinese};
    PunctuationBracketStyle punctuation_bracket_style{PunctuationBracketStyle::sogou};

    bool operator==(const SettingsSnapshot&) const = default;
};

struct SettingsParseResult {
    SettingsSnapshot settings;
    std::vector<std::string> errors;
    bool document_fatal{false};
};

// 候选框里的配色开关。内置项，不进用户可编辑的快捷表：[shortcuts] 段一旦被保存
// 过就带着 count=，解析时会把表截成那个长度，于是新增的内置行永远到不了存过设置
// 的用户手上。日期时间那组入口建在引擎里也是同一个理由。
struct ThemeShortcut final {
    std::uint32_t position;
    std::string_view label;
    std::string_view target;
};

[[nodiscard]] bool is_theme_shortcut(std::string_view key) noexcept;
[[nodiscard]] std::span<const ThemeShortcut> theme_shortcuts() noexcept;

[[nodiscard]] SettingsSnapshot default_settings();
[[nodiscard]] std::vector<CustomShortcutSettings> default_custom_shortcuts();
[[nodiscard]] std::vector<CustomPhraseSettings> default_custom_phrases();
// What the candidate row shows: the label when there is one, else the text.
[[nodiscard]] std::string phrase_candidate_label(const CustomPhraseSettings& phrase);

// Whether `key` -- one keystroke-built reading or raw input -- fires this phrase.
//
// Pure string work, deliberately: this runs for every phrase row on every
// keystroke, and the table holds up to 128 rows. Deriving a trigger for Chinese
// text needs the lexicon, which would mean hundreds of reverse lookups per
// keystroke against a 15000us budget -- so that derivation happens once, in the
// settings process, which writes the result into `aliases`. By the time a row
// reaches here its trigger is already spelled out.
[[nodiscard]] bool phrase_matches_key(
    const CustomPhraseSettings& phrase,
    std::string_view key) noexcept;
[[nodiscard]] bool shortcut_alias_matches(
    std::string_view aliases,
    std::string_view key) noexcept;
[[nodiscard]] std::string shortcut_candidate_label(
    const CustomShortcutSettings& shortcut);
[[nodiscard]] std::string shortcut_action_target(
    const CustomShortcutSettings& shortcut);
[[nodiscard]] SettingsParseResult parse_settings_text(
    std::string_view text,
    const SettingsSnapshot& previous);
[[nodiscard]] std::string serialize_default_settings();

}  // namespace piinput
