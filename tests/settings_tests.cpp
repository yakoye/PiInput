#include "piinput/settings.h"
#include "piinput/settings_manager.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void test_defaults_and_round_trip() {
    const auto defaults = piinput::default_settings();
    check(defaults.generation == 0U, "default generation");
    check(defaults.general.schema == piinput::InputSchema::flypy, "default schema");
    check(defaults.general.hot_reload, "default hot reload");
    check(defaults.pinyin.uv_compatibility, "default uv compatibility");
    check(defaults.pinyin.accept_u_colon, "default u colon");
    check(defaults.pinyin.incomplete_candidates, "default incomplete candidates");
    check(defaults.pinyin.prefix_beam_width == 32U, "default prefix beam width");
    check(defaults.pinyin.prefix_scan_limit == 4096U, "default prefix scan limit");
    check(defaults.candidates.items_per_row == 6U, "default candidate items per row");
    check(defaults.candidates.visible_rows == 3U, "default candidate visible rows");
    check(defaults.candidates.max_items == 90U, "default candidate max items");
    check(defaults.candidates.horizontal, "default candidate direction");
    check(defaults.candidates.equal_key == piinput::RowNavigationAction::next_row, "default equal key");
    check(defaults.candidates.minus_key == piinput::RowNavigationAction::previous_row, "default minus key");
    check(defaults.candidates.down_key == piinput::RowNavigationAction::next_row, "default down key");
    check(defaults.candidates.up_key == piinput::RowNavigationAction::previous_row, "default up key");
    check(!defaults.english.enabled, "default English disabled");
    check(defaults.english.builtin_dictionary, "default built-in English dictionary");
    check(defaults.english.user_dictionary, "default user English dictionary");
    check(defaults.english.user_learning, "default English learning");
    check(defaults.english.items_per_row == 6U, "default English items per row");
    check(defaults.punctuation == piinput::PunctuationMode::chinese, "default punctuation mode");

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
        "hot_reload=false\n"
        "[pinyin]\n"
        "uv_compatibility=false\n"
        "accept_u_colon=false\n"
        "incomplete_candidates=false\n"
        "prefix_beam_width=8\n"
        "prefix_scan_limit=16384\n"
        "[candidates]\n"
        "items_per_row=9\n"
        "visible_rows=5\n"
        "max_items=45\n"
        "horizontal=false\n"
        "equal_key=previous_row\n"
        "minus_key=next_row\n"
        "down_key=previous_row\n"
        "up_key=next_row\n"
        "[english]\n"
        "enabled=true\n"
        "builtin_dictionary=false\n"
        "user_dictionary=false\n"
        "user_learning=false\n"
        "items_per_row=5\n"
        "[punctuation]\n"
        "mode=programmer\n",
        previous);
    check(parsed.errors.empty(), "valid boundary settings parse");
    check(parsed.settings.general.schema == piinput::InputSchema::natural, "natural schema");
    check(!parsed.settings.general.hot_reload, "false hot reload");
    check(!parsed.settings.pinyin.uv_compatibility, "false uv compatibility");
    check(!parsed.settings.pinyin.accept_u_colon, "false u colon compatibility");
    check(!parsed.settings.pinyin.incomplete_candidates, "false incomplete candidates");
    check(parsed.settings.pinyin.prefix_beam_width == 8U, "minimum beam width");
    check(parsed.settings.pinyin.prefix_scan_limit == 16384U, "maximum scan limit");
    check(parsed.settings.candidates.items_per_row == 9U, "maximum candidate row size");
    check(parsed.settings.candidates.visible_rows == 5U, "maximum visible rows");
    check(parsed.settings.candidates.max_items == 45U, "one-screen max items accepted");
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
    check(!parsed.settings.english.builtin_dictionary, "built-in English dictionary disabled");
    check(!parsed.settings.english.user_dictionary, "user English dictionary disabled");
    check(!parsed.settings.english.user_learning, "English learning disabled");
    check(parsed.settings.english.items_per_row == 5U, "minimum English row size");
    check(parsed.settings.punctuation == piinput::PunctuationMode::programmer, "programmer punctuation");

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

void test_invalid_values_fallback_and_errors() {
    auto previous = piinput::default_settings();
    previous.pinyin.prefix_beam_width = 64U;
    previous.pinyin.prefix_scan_limit = 8192U;
    previous.candidates.max_items = 60U;
    previous.english.items_per_row = 7U;

    const auto parsed = piinput::parse_settings_text(
        "[general]\n"
        "schema=secret-schema-value\n"
        "hot_reload=TRUE\n"
        "[pinyin]\n"
        "prefix_beam_width=7\n"
        "prefix_scan_limit=999999999999999999999999\n"
        "[candidates]\n"
        "items_per_row=10\n"
        "visible_rows=0\n"
        "max_items=9\n"
        "horizontal=yes\n"
        "equal_key=sideways-secret\n"
        "[english]\n"
        "items_per_row=4\n"
        "[punctuation]\n"
        "mode=secret-punctuation-value\n",
        previous);
    check(parsed.errors.size() == 11U, "all invalid known fields report errors");
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
    check(parsed.settings.general.hot_reload, "last valid duplicate bool wins");
    check(parsed.settings.pinyin.prefix_beam_width == 64U,
        "invalid duplicate preserves preceding valid value");
    for (const auto& error : parsed.errors) {
        check(error.find("SECRET") == std::string::npos, "uppercase secret is not leaked");
        check(error.find("secret") == std::string::npos, "syntax content is not leaked");
    }
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

void test_concurrent_current_reads() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-settings-manager-concurrent.ini";
    write_text(path, "[general]\nschema=full\n");
    piinput::SettingsManager manager(path);
    manager.apply_pending_at_composition_boundary();

    std::atomic<bool> stop{false};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                const auto snapshot = manager.current();
                if (!snapshot || snapshot->candidates.items_per_row < 5U ||
                    snapshot->candidates.items_per_row > 9U) {
                    invalid.store(true, std::memory_order_relaxed);
                }
            }
        });
    }
    for (int index = 0; index < 20; ++index) {
        write_text(path, std::string("[general]\nschema=") + (index % 2 == 0 ? "abc\n" : "full\n"));
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
    test_invalid_values_fallback_and_errors();
    test_syntax_unknown_and_duplicate_keys();
    test_invalid_utf8_is_rejected_as_a_whole();
    test_manager_boundary_generation_and_immutability();
    test_manager_partial_errors_and_hot_reload_false();
    test_concurrent_current_reads();

    if (failures != 0) {
        std::cerr << failures << " settings test(s) failed\n";
        return 1;
    }
    std::cout << "All settings tests passed\n";
    return 0;
}
