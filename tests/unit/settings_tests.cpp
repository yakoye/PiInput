#include "piinput/settings.h"
#include "piinput/settings_manager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write settings fixture");
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::filesystem::path make_temp_directory(const std::string& label) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("piinput-" + label + "-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

class ScriptedSettingsFileReader final : public piinput::SettingsFileReader {
public:
    std::vector<piinput::SettingsFileMetadata> metadata_results;
    std::string content;
    bool throw_on_read{false};
    std::size_t metadata_calls{0U};
    std::size_t read_calls{0U};

    [[nodiscard]] piinput::SettingsFileMetadata metadata(
        const std::filesystem::path&) override {
        const auto index = metadata_calls++;
        if (metadata_results.empty()) {
            throw std::runtime_error("missing scripted metadata");
        }
        return metadata_results[std::min(index, metadata_results.size() - 1U)];
    }

    [[nodiscard]] std::string read(
        const std::filesystem::path&,
        const std::uintmax_t) override {
        ++read_calls;
        if (throw_on_read) {
            throw std::runtime_error("SECRET-READER-EXCEPTION");
        }
        return content;
    }
};

[[nodiscard]] piinput::SettingsFileMetadata metadata_at(
    const std::int64_t tick,
    const std::uintmax_t size) {
    return {
        std::filesystem::file_time_type{std::filesystem::file_time_type::duration{tick}},
        size,
    };
}

void test_defaults_and_round_trip() {
    const auto defaults = piinput::default_settings();
    check(defaults.generation == 0U, "default generation");
    check(defaults.general.schema == piinput::InputSchema::flypy, "default schema");
    check(defaults.general.default_language == piinput::DefaultInputLanguage::chinese,
        "default input language is Chinese");
    check(defaults.general.hot_reload, "default hot reload");
    check(defaults.pinyin.uv_compatibility, "default uv compatibility");
    check(defaults.pinyin.accept_u_colon, "default u colon");
    check(defaults.pinyin.incomplete_candidates, "default incomplete candidates");
    check(defaults.pinyin.user_learning, "default Chinese user learning");
    check(defaults.pinyin.prefix_beam_width == 32U, "default prefix beam width");
    check(defaults.pinyin.prefix_scan_limit == 4096U, "default prefix scan limit");
    check(defaults.candidates.items_per_row == 6U, "default candidate items per row");
    check(defaults.candidates.visible_rows == 5U, "default candidate visible rows");
    check(defaults.candidates.max_items == 90U, "default candidate max items");
    check(defaults.candidates.font_size == 16U, "default candidate font size");
    check(defaults.candidates.window_height == 40U, "default candidate window height");
    check(defaults.candidates.show_composition,
        "the composition row is on by default: a duplicated line is a nuisance, "
        "but a terminal with no line at all leaves nothing to read");
    check(defaults.candidates.horizontal, "default candidate direction");
    check(defaults.candidates.equal_key == piinput::RowNavigationAction::next_row, "default equal key");
    check(defaults.candidates.minus_key == piinput::RowNavigationAction::previous_row, "default minus key");
    check(defaults.candidates.down_key == piinput::RowNavigationAction::next_row, "default down key");
    check(defaults.candidates.up_key == piinput::RowNavigationAction::previous_row, "default up key");
    check(!defaults.english.enabled, "default English disabled");
    // Mixing English into Chinese is a new surface for every existing user, so
    // it stays off until asked for. The two switches are independent: turning
    // one on must never imply the other.
    check(!defaults.english.chinese_mode_completion,
        "default Chinese-mode English completion disabled");
    check(defaults.english.builtin_dictionary, "default built-in English dictionary");
    check(defaults.english.user_dictionary, "default user English dictionary");
    check(defaults.english.user_learning, "default English learning");
    check(defaults.english.items_per_row == 6U, "default English items per row");
    check(defaults.commands.enabled, "default command menu enabled");
    check(defaults.commands.hotkey == piinput::CommandHotkey::ctrl_alt_grave,
        "default command hotkey avoids the VS Code terminal shortcut");
    check(!defaults.commands.middle_dot_alias,
        "middle-dot command aliases are opt-in");
    check(defaults.custom_shortcuts.size() == 6U &&
            defaults.custom_shortcuts[0].aliases == "fh,fuhao,fuh,fuhc" &&
            defaults.custom_shortcuts[0].icon == "Ω" &&
            defaults.custom_shortcuts[3].target == "system:calculator" &&
            defaults.custom_shortcuts[5].target == "system:mspaint",
        "default shortcut table exposes all built-in tools as editable rows");
    check(defaults.punctuation == piinput::PunctuationMode::chinese, "default punctuation mode");
    check(defaults.punctuation_bracket_style == piinput::PunctuationBracketStyle::sogou,
        "default Chinese bracket style follows Sogou");

    const auto text = piinput::serialize_default_settings();
    check(text.rfind("\xEF\xBB\xBF", 0) != 0U, "serialized defaults have no BOM");
    const auto parsed = piinput::parse_settings_text(text, defaults);
    check(parsed.errors.empty(), "serialized defaults parse without errors");
    check(parsed.settings == defaults, "serialized defaults round trip");

    const auto bom_parsed = piinput::parse_settings_text("\xEF\xBB\xBF" + text, defaults);
    check(bom_parsed.errors.empty(), "UTF-8 BOM is accepted");
    check(bom_parsed.settings == defaults, "BOM does not change settings");
}

void test_valid_values_and_boundaries() {
    const auto previous = piinput::default_settings();
    const auto parsed = piinput::parse_settings_text(
        "[general]\n"
        "schema=natural\n"
        "default_language=english\n"
        "hot_reload=false\n"
        "[pinyin]\n"
        "uv_compatibility=false\n"
        "accept_u_colon=false\n"
        "incomplete_candidates=false\n"
        "user_learning=false\n"
        "prefix_beam_width=8\n"
        "prefix_scan_limit=16384\n"
        "[candidates]\n"
        "show_composition=false\n"
        "items_per_row=9\n"
        "visible_rows=6\n"
        "max_items=54\n"
        "font_size=28\n"
        "window_height=72\n"
        "horizontal=false\n"
        "equal_key=previous_row\n"
        "minus_key=next_row\n"
        "down_key=previous_row\n"
        "up_key=next_row\n"
        "[english]\n"
        "enabled=true\n"
        "chinese_mode_completion=true\n"
        "builtin_dictionary=false\n"
        "user_dictionary=false\n"
        "user_learning=false\n"
        "items_per_row=5\n"
        "[commands]\n"
        "enabled=false\n"
        "hotkey=ctrl_grave\n"
        "middle_dot_alias=true\n"
        "[shortcuts]\n"
        "count=1\n"
        "aliases_1=git, github\n"
        "position_1=4\n"
        "icon_1=🌐\n"
        "name_1=GitHub\n"
        "target_1=https://github.com\n"
        "[punctuation]\n"
        "mode=english\n"
        "bracket_style=wechat\n",
        previous);
    check(parsed.errors.empty(), "valid boundary settings parse");
    check(parsed.settings.general.schema == piinput::InputSchema::natural, "natural schema");
    check(parsed.settings.general.default_language == piinput::DefaultInputLanguage::english,
        "English can be selected as the default input language");
    check(!parsed.settings.general.hot_reload, "false hot reload");
    check(!parsed.settings.pinyin.uv_compatibility, "false uv compatibility");
    check(!parsed.settings.pinyin.accept_u_colon, "false u colon compatibility");
    check(!parsed.settings.pinyin.incomplete_candidates, "false incomplete candidates");
    check(!parsed.settings.pinyin.user_learning, "Chinese user learning can be disabled");
    check(parsed.settings.pinyin.prefix_beam_width == 8U, "minimum beam width");
    check(parsed.settings.pinyin.prefix_scan_limit == 16384U, "maximum scan limit");
    check(parsed.settings.candidates.items_per_row == 9U, "maximum candidate row size");
    check(parsed.settings.candidates.visible_rows == 6U, "maximum visible rows");
    check(parsed.settings.candidates.max_items == 54U, "one-screen max items accepted");
    check(parsed.settings.candidates.font_size == 28U, "maximum candidate font size");
    check(parsed.settings.candidates.window_height == 72U, "maximum candidate window height");
    check(!parsed.settings.candidates.show_composition, "composition row switched off");
    check(!parsed.settings.candidates.horizontal, "vertical candidate layout");
    check(parsed.settings.candidates.equal_key == piinput::RowNavigationAction::previous_row,
        "equal key navigation");
    check(parsed.settings.candidates.minus_key == piinput::RowNavigationAction::next_row,
        "minus key navigation");
    check(parsed.settings.candidates.down_key == piinput::RowNavigationAction::previous_row,
        "down key navigation");
    check(parsed.settings.candidates.up_key == piinput::RowNavigationAction::next_row,
        "up key navigation");
    check(parsed.settings.english.enabled, "English enabled");
    check(parsed.settings.english.chinese_mode_completion,
        "Chinese-mode English completion is read from the file");
    check(!parsed.settings.english.builtin_dictionary, "built-in English dictionary disabled");
    check(!parsed.settings.english.user_dictionary, "user English dictionary disabled");
    check(!parsed.settings.english.user_learning, "English learning disabled");
    check(parsed.settings.english.items_per_row == 5U, "minimum English row size");
    check(!parsed.settings.commands.enabled, "command menu can be disabled");
    check(parsed.settings.commands.hotkey == piinput::CommandHotkey::ctrl_grave,
        "configured command hotkey parses");
    check(parsed.settings.commands.middle_dot_alias,
        "middle-dot aliases can be explicitly enabled");
    check(parsed.settings.custom_shortcuts[0].aliases == "git, github" &&
            parsed.settings.custom_shortcuts[0].position == 4U &&
            parsed.settings.custom_shortcuts[0].icon == "🌐" &&
            parsed.settings.custom_shortcuts[0].name == "GitHub" &&
            parsed.settings.custom_shortcuts[0].target == "https://github.com",
        "custom shortcut aliases, position, name, and target parse together");
    check(parsed.settings.punctuation == piinput::PunctuationMode::english, "English punctuation");
    check(parsed.settings.punctuation_bracket_style == piinput::PunctuationBracketStyle::wechat,
        "WeChat Chinese bracket style parses");

    const auto english_punctuation = piinput::parse_settings_text(
        "[punctuation]\nmode=english\n", previous);
    check(english_punctuation.errors.empty(),
        "Chinese-input English punctuation setting parses without errors");
    check(english_punctuation.settings.punctuation == piinput::PunctuationMode::english,
        "English punctuation can be selected without changing the input language");

    const auto migrated_programmer = piinput::parse_settings_text(
        "[punctuation]\nmode=programmer\n", previous);
    check(migrated_programmer.errors.empty() &&
            migrated_programmer.settings.punctuation == piinput::PunctuationMode::english,
        "removed programmer punctuation migrates to its identical English behavior");

    const auto invalid_shortcut = piinput::parse_settings_text(
        "[shortcuts]\ncount=1\naliases_1=bad!alias\nposition_1=10\n", previous);
    check(invalid_shortcut.errors.size() == 2U &&
            invalid_shortcut.settings.custom_shortcuts[0] ==
                previous.custom_shortcuts[0],
        "invalid shortcut aliases and candidate positions retain prior values");

    const auto legacy_shortcut = piinput::parse_settings_text(
        "[shortcuts]\n"
        "aliases_1=github,gh\n"
        "position_1=4\n"
        "name_1=GitHub\n"
        "target_1=https://github.com\n",
        previous);
    check(legacy_shortcut.errors.empty() &&
            legacy_shortcut.settings.custom_shortcuts.size() == 7U &&
            legacy_shortcut.settings.custom_shortcuts.back().aliases == "github,gh" &&
            legacy_shortcut.settings.custom_shortcuts.back().target == "https://github.com",
        "legacy three-slot settings append user rows after the new built-in table");

    const auto cleared_shortcuts = piinput::parse_settings_text(
        "[shortcuts]\ncount=0\n", previous);
    check(cleared_shortcuts.errors.empty() &&
            cleared_shortcuts.settings.custom_shortcuts.empty(),
        "an explicit zero count keeps a user-deleted shortcut table empty");

    const auto minimum_visuals = piinput::parse_settings_text(
        "[candidates]\nfont_size=10\nwindow_height=20\n", previous);
    check(minimum_visuals.errors.empty(), "minimum candidate visual settings parse");
    check(minimum_visuals.settings.candidates.font_size == 10U,
        "minimum candidate font size");
    check(minimum_visuals.settings.candidates.window_height == 20U,
        "minimum candidate window height");

    auto chinese_default = previous;
    chinese_default.general.default_language = piinput::DefaultInputLanguage::chinese;
    const auto invalid_default_language = piinput::parse_settings_text(
        "[general]\ndefault_language=secret-invalid-language\n", chinese_default);
    check(invalid_default_language.errors.size() == 1U,
        "invalid default language reports one error");
    check(invalid_default_language.settings.general.default_language ==
            piinput::DefaultInputLanguage::chinese,
        "invalid default language preserves the previous value");

    auto sogou_brackets = previous;
    sogou_brackets.punctuation_bracket_style = piinput::PunctuationBracketStyle::sogou;
    const auto invalid_brackets = piinput::parse_settings_text(
        "[punctuation]\nbracket_style=secret-invalid-value\n", sogou_brackets);
    check(invalid_brackets.errors.size() == 1U,
        "invalid bracket style reports one error");
    check(invalid_brackets.settings.punctuation_bracket_style ==
            piinput::PunctuationBracketStyle::sogou,
        "invalid bracket style preserves the previous value");

    for (const auto hotkey : {"ctrl_alt_grave", "ctrl_grave", "disabled"}) {
        const auto result = piinput::parse_settings_text(
            std::string("[commands]\nhotkey=") + hotkey + "\n", previous);
        check(result.errors.empty(), std::string("command hotkey accepted: ") + hotkey);
    }

    auto configured = previous;
    configured.commands.hotkey = piinput::CommandHotkey::ctrl_grave;
    const auto invalid_hotkey = piinput::parse_settings_text(
        "[commands]\nhotkey=secret-invalid-value\n", configured);
    check(invalid_hotkey.errors.size() == 1U,
        "invalid command hotkey reports one error");
    check(invalid_hotkey.settings.commands.hotkey == piinput::CommandHotkey::ctrl_grave,
        "invalid command hotkey preserves the previous value");

    for (const auto schema : {"full", "flypy", "mspy", "abc"}) {
        const auto result = piinput::parse_settings_text(
            std::string("[general]\nschema=") + schema + "\n", previous);
        check(result.errors.empty(), std::string("built-in schema accepted: ") + schema);
    }
}

void test_candidate_screen_validation_is_key_order_independent() {
    const auto previous = piinput::default_settings();
    const auto parsed = piinput::parse_settings_text(
        "[candidates]\n"
        "max_items=9\n"
        "items_per_row=5\n"
        "visible_rows=1\n",
        previous);
    check(parsed.errors.empty(), "valid one-screen settings do not depend on key order");
    check(parsed.settings.candidates.max_items == 9U, "max items can precede screen dimensions");
    check(parsed.settings.candidates.items_per_row == 5U, "ordered candidate row size applied");
    check(parsed.settings.candidates.visible_rows == 1U, "ordered candidate rows applied");
}

void test_invalid_candidate_screen_size_falls_back_independently_of_key_order() {
    const auto previous = piinput::default_settings();
    const std::array<std::array<std::string, 3U>, 6U> permutations{{
        {{"max_items=9\n", "items_per_row=9\n", "visible_rows=5\n"}},
        {{"max_items=9\n", "visible_rows=5\n", "items_per_row=9\n"}},
        {{"items_per_row=9\n", "max_items=9\n", "visible_rows=5\n"}},
        {{"items_per_row=9\n", "visible_rows=5\n", "max_items=9\n"}},
        {{"visible_rows=5\n", "max_items=9\n", "items_per_row=9\n"}},
        {{"visible_rows=5\n", "items_per_row=9\n", "max_items=9\n"}},
    }};

    for (std::size_t index = 0U; index < permutations.size(); ++index) {
        const auto& permutation = permutations[index];
        const auto parsed = piinput::parse_settings_text(
            "[candidates]\n" + permutation[0] + permutation[1] + permutation[2], previous);
        const auto label = "candidate permutation " + std::to_string(index);
        check(parsed.settings.candidates.max_items == 90U, label + " rolls back max items");
        check(parsed.settings.candidates.items_per_row == 9U, label + " keeps row size");
        check(parsed.settings.candidates.visible_rows == 5U, label + " keeps visible rows");
        check(parsed.errors.size() == 1U, label + " reports one error");
        check(!parsed.errors.empty() &&
                parsed.errors.front().find("key 'max_items'") != std::string::npos,
            label + " attributes the error to max items");
        check(parsed.errors.empty() || parsed.errors.front().find("=9") == std::string::npos,
            label + " does not expose the rejected value");
    }
}

void test_candidate_screen_size_uses_fixed_dimension_fallback_priority() {
    // The grid must satisfy max_items >= items_per_row * visible_rows. These
    // numbers are sized against the shipped defaults (6 columns, 5 rows) so the
    // rollback has somewhere to land: 30 fits 6x5 but neither 9x6 nor 9x5.
    auto previous = piinput::default_settings();
    previous.candidates.max_items = 45U;
    const std::array<std::array<std::string, 3U>, 6U> permutations{{
        {{"max_items=30\n", "items_per_row=9\n", "visible_rows=6\n"}},
        {{"max_items=30\n", "visible_rows=6\n", "items_per_row=9\n"}},
        {{"items_per_row=9\n", "max_items=30\n", "visible_rows=6\n"}},
        {{"items_per_row=9\n", "visible_rows=6\n", "max_items=30\n"}},
        {{"visible_rows=6\n", "max_items=30\n", "items_per_row=9\n"}},
        {{"visible_rows=6\n", "items_per_row=9\n", "max_items=30\n"}},
    }};

    for (std::size_t index = 0U; index < permutations.size(); ++index) {
        const auto& permutation = permutations[index];
        const auto parsed = piinput::parse_settings_text(
            "[candidates]\n" + permutation[0] + permutation[1] + permutation[2], previous);
        const auto label = "candidate fallback permutation " + std::to_string(index);
        check(parsed.settings.candidates.max_items == 30U, label + " keeps max items");
        check(parsed.settings.candidates.items_per_row == 6U, label + " rolls back row size");
        check(parsed.settings.candidates.visible_rows == 5U, label + " rolls back visible rows");
        check(parsed.errors.size() == 2U, label + " reports each rolled-back field once");
        check(parsed.errors.size() >= 2U &&
                parsed.errors[0].find("key 'visible_rows'") != std::string::npos &&
                parsed.errors[1].find("key 'items_per_row'") != std::string::npos,
            label + " uses fixed visible rows then row size priority");
    }
}

void test_invalid_values_fallback_and_errors() {
    auto previous = piinput::default_settings();
    previous.pinyin.prefix_beam_width = 64U;
    previous.pinyin.prefix_scan_limit = 8192U;
    previous.candidates.max_items = 60U;
    previous.candidates.font_size = 20U;
    previous.candidates.window_height = 54U;
    previous.english.items_per_row = 7U;

    const auto parsed = piinput::parse_settings_text(
        "[general]\n"
        "schema=secret-schema-value\n"
        "hot_reload=TRUE\n"
        "[pinyin]\n"
        "prefix_beam_width=7\n"
        "prefix_scan_limit=999999999999999999999999\n"
        "[candidates]\n"
        "show_composition=sometimes\n"
        "items_per_row=10\n"
        "visible_rows=0\n"
        "max_items=9\n"
        "font_size=9\n"
        "window_height=73\n"
        "horizontal=yes\n"
        "equal_key=sideways-secret\n"
        "[english]\n"
        "items_per_row=4\n"
        "[punctuation]\n"
        "mode=secret-punctuation-value\n",
        previous);
    check(parsed.errors.size() == 14U, "all invalid known fields report errors");
    check(!parsed.document_fatal, "known field errors are not document-fatal");
    check(parsed.settings == previous, "invalid known values preserve previous fields");
    for (const auto& error : parsed.errors) {
        check(error.find("line ") != std::string::npos, "error contains line number");
        check(error.find('[') != std::string::npos, "error contains section");
        check(error.find("key '") != std::string::npos, "error contains key");
        check(error.find("secret") == std::string::npos, "error does not leak user value");
        check(error.find("999999") == std::string::npos, "overflow value is not leaked");
    }
}

void test_syntax_unknown_and_duplicate_keys() {
    const auto previous = piinput::default_settings();
    const auto parsed = piinput::parse_settings_text(
        "[future]\n"
        "anything=goes\n"
        "[general]\n"
        "future_key=ignored\n"
        "hot_reload=false\n"
        "hot_reload=INVALID-SECRET\n"
        "hot_reload=true\n"
        "[pinyin]\n"
        "prefix_beam_width=64\n"
        "prefix_beam_width=999\n"
        "missing equals secret text\n"
        "=empty-key-secret\n"
        "[broken\n",
        previous);
    check(parsed.errors.size() == 5U, "invalid duplicate and syntax errors reported");
    check(parsed.settings == previous, "document syntax errors preserve the whole previous snapshot");
    for (const auto& error : parsed.errors) {
        check(error.find("SECRET") == std::string::npos, "uppercase secret is not leaked");
        check(error.find("secret") == std::string::npos, "syntax content is not leaked");
    }
}

void test_document_fatal_errors_are_redacted_and_preserve_previous_snapshot() {
    auto previous = piinput::default_settings();
    previous.general.schema = piinput::InputSchema::mspy;
    previous.pinyin.prefix_beam_width = 64U;
    const std::array<std::string, 3U> texts{{
        "[general]\nschema=full\nmissing-equals-SECRET-TOKEN\n",
        "[general]\nschema=full\n=empty-key-SECRET-TOKEN\n",
        "[general]\nschema=full\n[broken-section-SECRET-TOKEN\n",
    }};

    for (std::size_t index = 0U; index < texts.size(); ++index) {
        const auto parsed = piinput::parse_settings_text(texts[index], previous);
        const auto label = "document-fatal syntax case " + std::to_string(index);
        check(parsed.settings == previous, label + " preserves the whole snapshot");
        check(parsed.document_fatal, label + " is classified as document-fatal");
        check(!parsed.errors.empty(), label + " reports an error");
        for (const auto& error : parsed.errors) {
            check(error.find("SECRET-TOKEN") == std::string::npos,
                label + " does not expose raw syntax content");
        }
    }

    const auto unknown_section = piinput::parse_settings_text(
        "[unknown-section-SECRET-TOKEN]\nmissing-equals-SECRET-TOKEN\n", previous);
    check(unknown_section.settings == previous,
        "syntax error in unknown section preserves the whole snapshot");
    check(unknown_section.document_fatal,
        "syntax error in unknown section is classified as document-fatal");
    check(unknown_section.errors.size() == 1U,
        "syntax error in unknown section reports one error");
    check(!unknown_section.errors.empty() &&
            unknown_section.errors.front().find("[<unknown-section>]") != std::string::npos,
        "unknown section syntax error uses a fixed section label");
    check(unknown_section.errors.empty() ||
            unknown_section.errors.front().find("SECRET-TOKEN") == std::string::npos,
        "unknown section syntax error is redacted");
}

void test_manager_rejects_oversized_settings_file() {
    const auto directory = make_temp_directory("settings-oversized");
    const auto path = directory / "settings.ini";
    std::string oversized = "SECRET-OVERSIZED-TOKEN";
    oversized.resize((1024U * 1024U) + 1U, 'x');
    write_text(path, oversized);

    piinput::SettingsManager manager(path);
    const auto original = manager.current();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == original.get(), "oversized settings file is not published");
    check(!manager.last_errors().empty(), "oversized settings file reports an error");
    check(!manager.last_errors().empty() &&
            manager.last_errors().front().find("key '<file-size>'") != std::string::npos,
        "oversized settings file reports the fixed file-size key");
    for (const auto& error : manager.last_errors()) {
        check(error.find("SECRET-OVERSIZED-TOKEN") == std::string::npos,
            "oversized settings error is redacted");
    }
    std::filesystem::remove_all(directory);
}

void test_manager_appends_redacted_errors_to_derived_log_once_per_change() {
    const auto directory = make_temp_directory("settings-log");
    const auto path = directory / "settings.ini";
    const auto log_path = directory / "logs" / "settings.log";
    write_text(path,
        "[general]\nschema=full\nUNKNOWN-KEY-SECRET=UNKNOWN-VALUE-SECRET\n"
        "hot_reload=SECRET-LOG-VALUE\n");

    piinput::SettingsManager manager(path);
    manager.apply_pending_at_composition_boundary();
    check(manager.current()->general.schema == piinput::InputSchema::full,
        "log append does not block valid fields from a field-error reload");
    check(std::filesystem::is_regular_file(log_path), "settings error log uses derived logs path");
    const auto first_log = read_text(log_path);
    check(first_log.find("key 'hot_reload'") != std::string::npos,
        "settings error log contains the canonical field name");
    check(first_log.find("SECRET-LOG-VALUE") == std::string::npos,
        "settings error log does not contain the invalid value");
    check(first_log.find("UNKNOWN-KEY-SECRET") == std::string::npos &&
            first_log.find("UNKNOWN-VALUE-SECRET") == std::string::npos,
        "settings error log does not contain ignored unknown keys or values");

    manager.poll();
    const auto second_log = read_text(log_path);
    check(second_log == first_log, "unchanged invalid settings are not logged repeatedly");
    std::filesystem::remove_all(directory);
}

void test_manager_log_failure_does_not_escape_or_block_current() {
    const auto directory = make_temp_directory("settings-log-failure");
    const auto path = directory / "settings.ini";
    write_text(directory / "logs", "regular file blocks log directory creation");
    write_text(path,
        "[general]\nschema=full\nhot_reload=SECRET-UNWRITABLE-LOG\n");

    bool threw = false;
    try {
        piinput::SettingsManager manager(path);
        manager.apply_pending_at_composition_boundary();
        check(manager.current()->general.schema == piinput::InputSchema::full,
            "log failure does not block publishing valid fields");
        check(!manager.last_errors().empty(), "last errors remain available after log failure");
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw, "log failure does not escape SettingsManager");
    std::filesystem::remove_all(directory);
}

void test_manager_uses_metadata_fast_path_without_rereading_content() {
    const auto directory = make_temp_directory("settings-metadata-fast-path");
    const auto content = std::string("[general]\nschema=full\n");
    const auto stable = metadata_at(1, content.size());
    auto reader = std::make_shared<ScriptedSettingsFileReader>();
    reader->metadata_results = {stable, stable, stable};
    reader->content = content;

    piinput::SettingsManager manager(directory / "settings.ini", reader);
    manager.apply_pending_at_composition_boundary();
    check(manager.current()->general.schema == piinput::InputSchema::full,
        "scripted stable settings are published");
    check(reader->read_calls == 1U, "initial stable settings are read once");

    manager.poll();
    check(reader->read_calls == 1U, "unchanged metadata skips settings content read");
    std::filesystem::remove_all(directory);
}

void test_manager_rejects_torn_and_incomplete_reads_deterministically() {
    const auto directory = make_temp_directory("settings-stable-read");
    const auto content = std::string("[general]\nschema=full\n");
    const auto before = metadata_at(1, content.size());
    const auto after = metadata_at(2, content.size());
    auto torn_reader = std::make_shared<ScriptedSettingsFileReader>();
    torn_reader->metadata_results = {before, after};
    torn_reader->content = content;

    piinput::SettingsManager torn_manager(directory / "torn-settings.ini", torn_reader);
    const auto torn_original = torn_manager.current();
    torn_manager.apply_pending_at_composition_boundary();
    check(torn_manager.current().get() == torn_original.get(),
        "metadata change during read does not publish pending settings");
    check(!torn_manager.last_errors().empty() &&
            torn_manager.last_errors().front().find("key '<unstable-file>'") != std::string::npos,
        "torn read reports a fixed error key");
    torn_manager.poll();
    torn_manager.apply_pending_at_composition_boundary();
    check(torn_reader->read_calls == 2U,
        "stable metadata after a torn read retries the content read");
    check(torn_manager.current()->general.schema == piinput::InputSchema::full,
        "stable retry after a torn read publishes valid settings");

    auto short_reader = std::make_shared<ScriptedSettingsFileReader>();
    short_reader->metadata_results = {before, before};
    short_reader->content = "short";
    piinput::SettingsManager short_manager(directory / "short-settings.ini", short_reader);
    const auto short_original = short_manager.current();
    short_manager.apply_pending_at_composition_boundary();
    check(short_manager.current().get() == short_original.get(),
        "incomplete read does not publish pending settings");
    check(!short_manager.last_errors().empty() &&
            short_manager.last_errors().front().find("key '<file>'") != std::string::npos,
        "incomplete read reports a fixed file error");
    std::filesystem::remove_all(directory);
}

void test_manager_catches_reader_exceptions_and_redacts_them() {
    const auto directory = make_temp_directory("settings-reader-exception");
    const auto content = std::string("[general]\nschema=full\n");
    const auto stable = metadata_at(1, content.size());
    auto reader = std::make_shared<ScriptedSettingsFileReader>();
    reader->metadata_results = {stable};
    reader->content = content;
    reader->throw_on_read = true;

    bool threw = false;
    try {
        piinput::SettingsManager manager(directory / "throwing-settings.ini", reader);
        const auto original = manager.current();
        manager.apply_pending_at_composition_boundary();
        check(manager.current().get() == original.get(), "reader exception preserves current settings");
        check(!manager.last_errors().empty(), "reader exception reports a fixed error");
        for (const auto& error : manager.last_errors()) {
            check(error.find("SECRET-READER-EXCEPTION") == std::string::npos,
                "reader exception detail is redacted");
        }
        reader->throw_on_read = false;
        manager.poll();
        manager.apply_pending_at_composition_boundary();
        check(reader->read_calls == 2U,
            "same metadata after a transient read exception retries the content read");
        check(manager.current()->general.schema == piinput::InputSchema::full,
            "retry after a transient read exception publishes valid settings");
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw, "reader exception does not escape SettingsManager");
    std::filesystem::remove_all(directory);
}

void test_manager_does_not_repeat_stable_document_fatal_errors() {
    const auto directory = make_temp_directory("settings-stable-fatal");
    const auto content = std::string(
        "[general]\nschema=full\nmissing-equals-SECRET-STABLE-FATAL\n");
    const auto stable = metadata_at(1, content.size());
    auto reader = std::make_shared<ScriptedSettingsFileReader>();
    reader->metadata_results = {stable, stable, stable};
    reader->content = content;
    const auto log_path = directory / "logs" / "settings.log";

    piinput::SettingsManager manager(directory / "settings.ini", reader);
    const auto first_log = read_text(log_path);
    manager.poll();
    const auto second_log = read_text(log_path);
    check(reader->read_calls == 1U, "unchanged stable fatal settings are not reread");
    check(second_log == first_log, "unchanged stable fatal errors are not logged repeatedly");
    check(second_log.find("SECRET-STABLE-FATAL") == std::string::npos,
        "stable fatal log remains redacted");
    std::filesystem::remove_all(directory);
}

void test_invalid_utf8_is_rejected_as_a_whole() {
    const auto previous = piinput::default_settings();
    std::string invalid = "[general]\nschema=full\n";
    invalid.push_back(static_cast<char>(0xFF));
    const auto parsed = piinput::parse_settings_text(invalid, previous);
    check(parsed.settings == previous, "invalid UTF-8 cannot partially alter a snapshot");
    check(parsed.errors.size() == 1U, "invalid UTF-8 reports one document error");
    check(!parsed.errors.empty() && parsed.errors.front().find("<encoding>") != std::string::npos,
        "invalid UTF-8 error identifies encoding without content");
}

void test_manager_boundary_generation_and_immutability() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-settings-manager.ini";
    std::filesystem::remove(path);
    piinput::SettingsManager manager(path);
    const auto original = manager.current();
    check(original->generation == 0U, "manager starts at generation zero");
    check(manager.last_errors().empty(), "optional missing settings file is not an initial error");

    write_text(path, "[general]\nschema=full\n");
    manager.poll();
    check(manager.current().get() == original.get(), "poll does not replace current snapshot");
    manager.apply_pending_at_composition_boundary();
    const auto first = manager.current();
    check(first.get() != original.get(), "composition boundary replaces snapshot");
    check(first->generation == 1U, "first applied generation");
    check(first->general.schema == piinput::InputSchema::full, "polled value applied");
    check(original->general.schema == piinput::InputSchema::flypy,
        "old composition snapshot remains immutable");

    write_text(path, "[general]\nschema=abc\nmissing-equals-SECRET-TOKEN\n");
    manager.poll();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == first.get(),
        "document-fatal reload does not publish a partial snapshot");
    for (const auto& error : manager.last_errors()) {
        check(error.find("SECRET-TOKEN") == std::string::npos,
            "manager document errors do not expose raw syntax content");
    }

    manager.poll();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == first.get(), "unchanged file creates no generation");

    write_text(path, "[general]\nschema=abc\n");
    manager.poll();
    manager.apply_pending_at_composition_boundary();
    const auto second = manager.current();
    check(second->generation == 2U, "generation increases monotonically");
    check(second->general.schema == piinput::InputSchema::abc, "second update applied");
    check(first->general.schema == piinput::InputSchema::full, "previous snapshot remains unchanged");

    std::filesystem::remove(path);
    manager.poll();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == second.get(), "deleted file does not damage current snapshot");
}

void test_manager_partial_errors_and_hot_reload_false() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-settings-manager-errors.ini";
    write_text(path,
        "[general]\nschema=full\nhot_reload=false\n"
        "[pinyin]\nprefix_beam_width=leaked-invalid-value\n");
    piinput::SettingsManager manager(path);
    const auto before = manager.current();
    check(before->general.schema == piinput::InputSchema::flypy,
        "constructor load stays pending until boundary");
    manager.apply_pending_at_composition_boundary();
    const auto loaded = manager.current();
    check(loaded->general.schema == piinput::InputSchema::full,
        "valid fields from partially invalid file are applied");
    check(!loaded->general.hot_reload, "initial explicit load can disable hot reload");
    check(!manager.last_errors().empty(), "parse errors are available to caller");
    for (const auto& error : manager.last_errors()) {
        check(error.find("leaked-invalid-value") == std::string::npos,
            "manager errors do not expose invalid values");
    }

    write_text(path, "[general]\nschema=abc\nhot_reload=true\n");
    manager.poll();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == loaded.get(), "hot_reload=false suppresses subsequent polling");
    std::filesystem::remove(path);
}

void test_manager_candidate_grid_settings_apply_only_at_boundary() {
    const auto path =
        std::filesystem::temp_directory_path() / "piinput-settings-manager-candidate-grid.ini";
    write_text(path,
        "[candidates]\n"
        "items_per_row=9\n"
        "visible_rows=5\n"
        "max_items=45\n");
    piinput::SettingsManager manager(path);

    const auto before_boundary = manager.current();
    check(before_boundary->candidates.items_per_row == 6U &&
            before_boundary->candidates.visible_rows == 5U &&
            before_boundary->candidates.max_items == 90U,
        "candidate grid settings stay pending before the composition boundary");

    manager.apply_pending_at_composition_boundary();
    const auto loaded = manager.current();
    check(loaded->candidates.items_per_row == 9U &&
            loaded->candidates.visible_rows == 5U &&
            loaded->candidates.max_items == 45U,
        "candidate grid settings load at the composition boundary");

    write_text(path,
        "[candidates]\n"
        "items_per_row=5\n"
        "visible_rows=1\n"
        "max_items=9\n");
    manager.poll();
    check(manager.current().get() == loaded.get(),
        "candidate grid hot reload does not alter an active composition snapshot");

    manager.apply_pending_at_composition_boundary();
    const auto reloaded = manager.current();
    check(reloaded->candidates.items_per_row == 5U &&
            reloaded->candidates.visible_rows == 1U &&
            reloaded->candidates.max_items == 9U,
        "candidate grid hot reload applies at the next composition boundary");

    write_text(path, std::string("\xFF", 1U));
    manager.poll();
    manager.apply_pending_at_composition_boundary();
    check(manager.current().get() == reloaded.get(),
        "damaged candidate settings preserve the previous boundary snapshot");
    std::filesystem::remove(path);
}

void test_settings_poll_throttle_limits_boundary_file_checks() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    piinput::SettingsPollThrottle throttle(1s);
    const Clock::time_point initial{10s};
    check(throttle.should_poll(initial), "settings poll is due initially");

    throttle.mark_polled(initial);
    check(!throttle.should_poll(initial), "same-boundary settings poll is throttled");
    check(!throttle.should_poll(initial + 999ms), "settings poll remains throttled before one second");
    check(throttle.should_poll(initial + 1s), "settings poll is due after one second");

    throttle.mark_polled(initial + 1s);
    check(!throttle.should_poll(initial + 1500ms), "new poll restarts the throttle interval");
}

void test_concurrent_current_reads() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-settings-manager-concurrent.ini";
    const std::string compact =
        "[candidates]\nitems_per_row=5\nvisible_rows=1\nmax_items=9\n";
    const std::string expanded =
        "[candidates]\nitems_per_row=9\nvisible_rows=5\nmax_items=90\n";
    write_text(path, compact);
    piinput::SettingsManager manager(path);
    manager.apply_pending_at_composition_boundary();

    std::atomic<bool> stop{false};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            std::uint64_t previous_generation = 0U;
            while (!stop.load(std::memory_order_relaxed)) {
                const auto snapshot = manager.current();
                const auto compact_snapshot = snapshot &&
                    snapshot->candidates.items_per_row == 5U &&
                    snapshot->candidates.visible_rows == 1U &&
                    snapshot->candidates.max_items == 9U;
                const auto expanded_snapshot = snapshot &&
                    snapshot->candidates.items_per_row == 9U &&
                    snapshot->candidates.visible_rows == 5U &&
                    snapshot->candidates.max_items == 90U;
                if ((!compact_snapshot && !expanded_snapshot) ||
                    snapshot->generation < previous_generation) {
                    invalid.store(true, std::memory_order_relaxed);
                }
                previous_generation = snapshot->generation;
            }
        });
    }
    for (int index = 0; index < 20; ++index) {
        write_text(path, index % 2 == 0 ? expanded : compact);
        manager.poll();
        manager.apply_pending_at_composition_boundary();
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& reader : readers) {
        reader.join();
    }
    check(!invalid.load(std::memory_order_relaxed), "concurrent current() reads see valid snapshots");
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_defaults_and_round_trip();
    test_valid_values_and_boundaries();
    test_candidate_screen_validation_is_key_order_independent();
    test_invalid_candidate_screen_size_falls_back_independently_of_key_order();
    test_candidate_screen_size_uses_fixed_dimension_fallback_priority();
    test_invalid_values_fallback_and_errors();
    test_syntax_unknown_and_duplicate_keys();
    test_document_fatal_errors_are_redacted_and_preserve_previous_snapshot();
    test_invalid_utf8_is_rejected_as_a_whole();
    test_manager_rejects_oversized_settings_file();
    test_manager_appends_redacted_errors_to_derived_log_once_per_change();
    test_manager_log_failure_does_not_escape_or_block_current();
    test_manager_uses_metadata_fast_path_without_rereading_content();
    test_manager_rejects_torn_and_incomplete_reads_deterministically();
    test_manager_catches_reader_exceptions_and_redacts_them();
    test_manager_does_not_repeat_stable_document_fatal_errors();
    test_manager_boundary_generation_and_immutability();
    test_manager_partial_errors_and_hot_reload_false();
    test_manager_candidate_grid_settings_apply_only_at_boundary();
    test_settings_poll_throttle_limits_boundary_file_checks();
    test_concurrent_current_reads();

    if (failures != 0) {
        std::cerr << failures << " settings test(s) failed\n";
        return 1;
    }
    std::cout << "All settings tests passed\n";
    return 0;
}
