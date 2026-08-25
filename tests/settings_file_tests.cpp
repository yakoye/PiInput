#include "settings_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path temp_directory() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("piinput-visual-settings-" + std::to_string(tick));
    std::filesystem::create_directories(path);
    return path;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_missing_file_uses_defaults() {
    std::string error;
    const auto values = piinput::windows::load_candidate_visual_settings(
        temp_directory() / "missing.ini", error);
    check(values.font_size == 16U && values.window_height == 40U &&
            values.visible_rows == 5U &&
            values.schema == piinput::InputSchema::flypy &&
            values.default_language == piinput::DefaultInputLanguage::chinese,
        "missing settings file uses visual, Xiaohe, and Chinese defaults");
    check(error.empty(), "missing settings file is not reported as corruption");
}

void test_atomic_save_preserves_unrelated_user_settings() {
    const auto directory = temp_directory();
    const auto path = directory / "settings.ini";
    write_text(path,
        "# keep this comment\n"
        "[general]\nschema=full\nhot_reload=true\n"
        "[candidates]\nitems_per_row=9\nvisible_rows=4\nfont_size=18\nwindow_height=48\n"
        "[english]\nenabled=true\n");

    std::string error;
    check(piinput::windows::save_candidate_visual_settings_atomic(
              path, {.font_size = 24U, .window_height = 60U,
                     .schema = piinput::InputSchema::full,
                     .default_language = piinput::DefaultInputLanguage::english,
                     .visible_rows = 6U}, error),
        "valid visual settings save atomically");
    check(error.empty(), "valid save has no error");
    const auto content = read_text(path);
    check(content.find("# keep this comment") != std::string::npos,
        "settings save preserves comments");
    check(content.find("schema=full") != std::string::npos &&
            content.find("default_language=english") != std::string::npos &&
            content.find("items_per_row=9") != std::string::npos &&
            content.find("enabled=true") != std::string::npos,
        "settings save preserves unrelated sections and candidate keys");
    check(content.find("font_size=24") != std::string::npos &&
            content.find("window_height=60") != std::string::npos &&
            content.find("visible_rows=6") != std::string::npos,
        "settings save replaces visual values and expanded row count");

    const auto loaded = piinput::windows::load_candidate_visual_settings(path, error);
    check(loaded.font_size == 24U && loaded.window_height == 60U &&
            loaded.visible_rows == 6U &&
            loaded.schema == piinput::InputSchema::full &&
            loaded.default_language == piinput::DefaultInputLanguage::english,
        "saved visual, schema, and language values load through the shared parser");
    check(!std::filesystem::exists(path.wstring() + L".tmp"),
        "successful atomic save leaves no temporary file");
    std::filesystem::remove_all(directory);
}

void test_invalid_save_leaves_original_file_unchanged() {
    const auto directory = temp_directory();
    const auto path = directory / "settings.ini";
    write_text(path, "[candidates]\nfont_size=20\nwindow_height=52\n");
    const auto before = read_text(path);
    for (const auto invalid : {
             piinput::windows::CandidateVisualFileSettings{9U, 48U},
             piinput::windows::CandidateVisualFileSettings{29U, 48U},
             piinput::windows::CandidateVisualFileSettings{18U, 19U},
             piinput::windows::CandidateVisualFileSettings{18U, 73U},
             piinput::windows::CandidateVisualFileSettings{
                 .font_size = 18U, .window_height = 48U, .visible_rows = 0U},
             piinput::windows::CandidateVisualFileSettings{
                 .font_size = 18U, .window_height = 48U, .visible_rows = 7U}}) {
        std::string error;
        check(!piinput::windows::save_candidate_visual_settings_atomic(path, invalid, error),
            "out-of-range visual settings are rejected");
        check(!error.empty(), "rejected visual settings provide an error");
        check(read_text(path) == before, "rejected save does not modify the original file");
    }
    std::filesystem::remove_all(directory);
}

void test_visual_keys_are_added_to_an_existing_candidate_section() {
    const auto directory = temp_directory();
    const auto path = directory / "settings.ini";
    write_text(path, "[candidates]\nitems_per_row=6\n[punctuation]\nmode=chinese\n");
    std::string error;
    check(piinput::windows::save_candidate_visual_settings_atomic(
              path, {22U, 56U}, error),
        "visual keys can be added when older settings do not contain them");
    const auto content = read_text(path);
    check(content.find("items_per_row=6\nfont_size=22\nwindow_height=56\nvisible_rows=5\n[punctuation]") !=
            std::string::npos,
        "new visual keys stay inside the candidates section");
    std::filesystem::remove_all(directory);
}

void test_mouse_wheel_steps_numeric_settings_by_one() {
    check(piinput::windows::step_numeric_setting(12U, 120, 10U, 28U) == 13U,
        "wheel up increases a numeric setting by one");
    check(piinput::windows::step_numeric_setting(16U, -120, 10U, 28U) == 15U,
        "wheel down decreases a numeric setting by one");
    check(piinput::windows::step_numeric_setting(28U, 120, 10U, 28U) == 28U,
        "wheel up clamps at the maximum");
    check(piinput::windows::step_numeric_setting(20U, -240, 20U, 72U) == 20U,
        "multiple downward notches clamp candidate height at the minimum");
}

}  // namespace

// The settings window edits every option the engine reads, so each one has to
// survive a load/store round trip -- and the user's own comments and any key
// this build does not know have to survive it too.
void test_every_option_round_trips_without_disturbing_the_file() {
    const auto directory = temp_directory();
    const auto path = directory / "settings.ini";
    write_text(path,
        "# a comment the user wrote\n"
        "[general]\n"
        "schema=full\n"
        "hot_reload=false\n"
        "\n"
        "[pinyin]\n"
        "simplified_pinyin=false\n"
        "prefix_scan_limit=8192\n"
        "a_key_this_build_does_not_know=keep me\n"
        "\n"
        "[candidates]\n"
        "items_per_row=9\n"
        "visible_rows=6\n");

    std::string error;
    auto loaded = piinput::windows::load_all_settings(path, error);
    check(error.empty(), "a hand-written settings file loads without complaint");
    check(loaded.general.schema == piinput::InputSchema::full, "schema is read back");
    check(!loaded.general.hot_reload, "hot_reload is read back");
    check(!loaded.pinyin.simplified_pinyin, "simplified_pinyin is read back");
    check(loaded.pinyin.prefix_scan_limit == 8192U, "prefix_scan_limit is read back");
    check(loaded.candidates.items_per_row == 9U, "items_per_row is read back");
    check(loaded.candidates.visible_rows == 6U, "visible_rows is read back");

    // Touch one option in every section, including ones the file never listed.
    loaded.general.schema = piinput::InputSchema::flypy;
    loaded.pinyin.simplified_pinyin = true;
    loaded.pinyin.accept_u_colon = false;
    loaded.candidates.horizontal = false;
    loaded.candidates.up_key = piinput::RowNavigationAction::next_row;
    loaded.punctuation = piinput::PunctuationMode::english;
    loaded.punctuation_bracket_style = piinput::PunctuationBracketStyle::wechat;
    loaded.commands.hotkey = piinput::CommandHotkey::disabled;
    loaded.commands.middle_dot_alias = true;
    loaded.english.enabled = true;
    loaded.english.items_per_row = 7U;
    loaded.general.symbol_tool = "D:/tools/yesymbol.exe";
    loaded.custom_shortcuts[0] = {
        "calc,jsq", 3U, "My calculator", "D:/tools/calc.html"};
    check(piinput::windows::save_all_settings_atomic(path, loaded, error),
        "every option saves");

    const auto reloaded = piinput::windows::load_all_settings(path, error);
    check(reloaded.general.schema == piinput::InputSchema::flypy, "schema round trips");
    check(reloaded.pinyin.simplified_pinyin, "simplified_pinyin round trips");
    check(!reloaded.pinyin.accept_u_colon, "accept_u_colon round trips");
    check(reloaded.pinyin.prefix_scan_limit == 8192U,
        "an option nobody touched keeps its value");
    check(!reloaded.candidates.horizontal, "horizontal round trips");
    check(reloaded.candidates.up_key == piinput::RowNavigationAction::next_row,
        "up_key round trips");
    check(reloaded.punctuation == piinput::PunctuationMode::english,
        "punctuation mode round trips");
    check(reloaded.punctuation_bracket_style == piinput::PunctuationBracketStyle::wechat,
        "bracket style round trips");
    check(reloaded.commands.hotkey == piinput::CommandHotkey::disabled,
        "command hotkey round trips");
    check(reloaded.commands.middle_dot_alias, "middle dot alias round trips");
    check(reloaded.english.enabled, "english enabled round trips");
    check(reloaded.english.items_per_row == 7U, "english items_per_row round trips");
    check(reloaded.general.symbol_tool == "D:/tools/yesymbol.exe",
        "the tray's symbol tool path round trips");
    check(reloaded.custom_shortcuts[0] == loaded.custom_shortcuts[0],
        "custom shortcut aliases, position, name, and target round trip");

    const auto text = read_text(path);
    check(text.find("# a comment the user wrote") != std::string::npos,
        "the user's comment survives a save");
    check(text.find("a_key_this_build_does_not_know=keep me") != std::string::npos,
        "a key this build does not know survives a save");
    check(text.find("[punctuation]") != std::string::npos,
        "a section the file never had is appended");
    check(text.find("[shortcuts]") != std::string::npos &&
            text.find("target_1=D:/tools/calc.html") != std::string::npos,
        "custom shortcut section is appended with its launch target");
    std::filesystem::remove_all(directory);
}

int main() {
    test_missing_file_uses_defaults();
    test_atomic_save_preserves_unrelated_user_settings();
    test_invalid_save_leaves_original_file_unchanged();
    test_visual_keys_are_added_to_an_existing_candidate_section();
    test_mouse_wheel_steps_numeric_settings_by_one();
    test_every_option_round_trips_without_disturbing_the_file();
    if (failures != 0) return 1;
    std::cout << "PiInput settings file tests passed.\n";
    return 0;
}
