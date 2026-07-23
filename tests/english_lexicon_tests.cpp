#include "piinput/english_lexicon.h"
#include "piinput/english_session.h"
#include "piinput/settings.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
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
    output << text;
    if (!output) {
        throw std::runtime_error("failed to write English fixture");
    }
}

[[nodiscard]] std::filesystem::path make_temp_directory(const std::string& label) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("piinput-english-" + label + "-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

[[nodiscard]] std::vector<std::string> words(
    const std::vector<piinput::EnglishCandidate>& candidates) {
    std::vector<std::string> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        result.push_back(candidate.word);
    }
    return result;
}

void test_default_disabled_gate_does_not_start_english_composition() {
    const auto defaults = piinput::default_settings();
    check(!defaults.english.enabled, "English suggestions remain disabled by default");
    check(!piinput::EnglishSession::should_start(true, defaults.english),
        "English mode passes letters through while suggestions are disabled");

    auto enabled = defaults.english;
    enabled.enabled = true;
    check(piinput::EnglishSession::should_start(true, enabled),
        "enabled English suggestions capture letters in English mode");
    check(!piinput::EnglishSession::should_start(false, enabled),
        "enabled English suggestions do not affect the Chinese hot path");
}

void test_prefix_case_and_stable_sorting() {
    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(
              std::filesystem::path(PIINPUT_SOURCE_DIR) / "tests/data/english_candidates.tsv") == 7U,
        "valid built-in English entries load");

    check(words(lexicon.query("AP", 10U)) ==
            std::vector<std::string>({"Apple", "application", "apply", "apt"}),
        "ASCII prefix matching is case-insensitive and stable IDs break equal-weight ties");
    check(words(lexicon.query("ban", 10U)) ==
            std::vector<std::string>({"banana", "Band", "bandwidth"}),
        "original candidate casing is preserved");
    check(lexicon.query("", 10U).empty(), "empty prefixes produce no candidates");
    check(lexicon.query("ap", 0U).empty(), "zero limit produces no candidates");
    check(lexicon.query("applicationislongerthaneveryword", 10U).empty(),
        "long prefixes stop safely at the end of the index");
}

void test_invalid_rows_duplicates_and_user_merge() {
    const auto directory = make_temp_directory("merge");
    const auto builtin = directory / "builtin.tsv";
    const auto user = directory / "user.tsv";
    write_text(builtin,
        "alpha\t10\n"
        "alpha\t30\n"
        "ALPHA\t20\n"
        "algebra\t1000\n"
        "beta\tbad\n"
        "two words\t99\n"
        "\t20\n"
        "gamma\t0\n"
        "delta\t40\textra\n"
        "cafe\xCC\x81\t60\n");
    write_text(user,
        "alpine\t1\n"
        "alpha\t80\n"
        "ALPHA\t90\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 4U,
        "damaged rows are rejected and exact duplicates merge");
    check(lexicon.load_user_tsv(user) == 3U,
        "valid user rows load without duplicating exact words");
    check(words(lexicon.query("al", 10U)) ==
            std::vector<std::string>({"ALPHA", "alpha", "alpine", "algebra"}),
        "user entries take priority and merged duplicates keep deterministic casing");

    const auto candidates = lexicon.query("alpha", 10U);
    check(candidates.size() == 2U, "case-distinct English words remain distinct");
    check(candidates[0].word == "ALPHA" && candidates[0].base_weight == 90U,
        "user duplicate raises the merged base weight");
    check(candidates[1].word == "alpha" && candidates[1].base_weight == 80U,
        "exact duplicate merges deterministically by maximum weight");
    std::filesystem::remove_all(directory);
}

void test_learning_promotes_and_persists_atomically() {
    const auto directory = make_temp_directory("learning");
    const auto builtin = directory / "builtin.tsv";
    const auto learning = directory / "learning.tsv";
    write_text(builtin, "alpha\t100\nalpine\t10\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 2U, "learning fixture loads");
    check(words(lexicon.query("al", 2U)) ==
            std::vector<std::string>({"alpha", "alpine"}),
        "base weight controls ordering before learning");
    check(lexicon.record_selection("alpine"), "known selection is recorded");
    check(words(lexicon.query("al", 2U)) ==
            std::vector<std::string>({"alpine", "alpha"}),
        "learning weight promotes a selected candidate");
    check(!lexicon.record_selection("absent"), "unknown selections are ignored");
    check(lexicon.save_learning_tsv(learning), "learning state saves successfully");
    check(std::filesystem::is_regular_file(learning), "learning state uses the requested local path");
    check(!std::filesystem::exists(learning.string() + ".tmp"),
        "successful atomic save leaves no temporary file");

    piinput::EnglishLexicon reloaded;
    check(reloaded.load_builtin_tsv(builtin) == 2U, "reload fixture loads");
    check(reloaded.load_learning_tsv(learning) == 1U, "saved learning state reloads");
    check(words(reloaded.query("al", 2U)) ==
            std::vector<std::string>({"alpine", "alpha"}),
        "reloaded learning preserves promoted ordering");
    std::filesystem::remove_all(directory);
}

void test_learning_overflow_and_damaged_rows_are_safe() {
    const auto directory = make_temp_directory("learning-damaged");
    const auto builtin = directory / "builtin.tsv";
    const auto learning = directory / "learning.tsv";
    write_text(builtin, "alpha\t10\nalpine\t10\n");
    write_text(learning,
        "alpha\t18446744073709551615\n"
        "alpine\tbroken\n"
        "missing\t100\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 2U, "damaged learning fixture loads");
    check(lexicon.load_learning_tsv(learning) == 1U,
        "only valid learning rows for known words load");
    check(lexicon.record_selection("alpha"), "selection at the learning ceiling is safe");
    check(lexicon.query("al", 2U).front().learning_count ==
            (std::numeric_limits<std::uint64_t>::max)(),
        "learning count saturates instead of overflowing");
    std::filesystem::remove_all(directory);
}

void test_english_session_composition_editing_and_choice() {
    const auto directory = make_temp_directory("session");
    const auto builtin = directory / "builtin.tsv";
    write_text(builtin, "Apple\t100\napplication\t80\napply\t60\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 3U, "session fixture loads");
    piinput::EnglishSession session(lexicon, 2U);
    session.insert('A');
    session.insert('p');
    check(session.snapshot().input == "Ap" && session.snapshot().caret == 2U,
        "English session preserves typed ASCII case and caret position");
    check(words(session.snapshot().candidates) ==
            std::vector<std::string>({"Apple", "application"}),
        "English session refreshes prefix candidates immediately");

    check(session.move_left(), "left moves within an English composition");
    session.insert('X');
    check(session.snapshot().input == "AXp" && session.snapshot().caret == 2U,
        "insert follows the current caret");
    check(session.backspace(), "backspace edits before the caret");
    check(session.move_home(), "home moves to the start");
    check(session.delete_forward(), "delete edits at the caret");
    check(session.snapshot().input == "p", "editing operations update English input");
    check(session.move_end(), "end moves to the end");
    session.insert('p');
    check(session.snapshot().input == "pp", "end accepts subsequent insertion");
    session.clear();

    session.insert('a');
    session.insert('p');
    const auto chosen = session.choose(0U);
    check(chosen == std::optional<std::string>("Apple"),
        "Space or digit can choose the indexed English candidate");
    check(session.snapshot().input.empty(), "choosing clears the English composition");
    check(session.raw_input().empty(), "cleared session has no raw input");

    session.insert('z');
    check(session.snapshot().candidates.empty(), "unknown prefixes keep raw input without candidates");
    check(session.raw_input() == "z", "Enter can retrieve the original English input");
    session.clear();
    check(session.snapshot().input.empty(), "Escape can clear the English composition");
    std::filesystem::remove_all(directory);
}

void test_english_session_can_disable_learning() {
    const auto directory = make_temp_directory("session-no-learning");
    const auto builtin = directory / "builtin.tsv";
    write_text(builtin, "alpha\t100\nalpine\t10\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 2U, "no-learning fixture loads");
    piinput::EnglishSession session(lexicon, 2U, false);
    session.insert('a');
    session.insert('l');
    check(session.choose(1U) == std::optional<std::string>("alpine"),
        "candidate choice still works when learning is disabled");
    session.insert('a');
    session.insert('l');
    check(words(session.snapshot().candidates) ==
            std::vector<std::string>({"alpha", "alpine"}),
        "disabled learning does not alter in-memory candidate ordering");
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    test_default_disabled_gate_does_not_start_english_composition();
    test_prefix_case_and_stable_sorting();
    test_invalid_rows_duplicates_and_user_merge();
    test_learning_promotes_and_persists_atomically();
    test_learning_overflow_and_damaged_rows_are_safe();
    test_english_session_composition_editing_and_choice();
    test_english_session_can_disable_learning();

    if (failures != 0) {
        std::cerr << failures << " English lexicon test(s) failed\n";
        return 1;
    }
    std::cout << "All English lexicon tests passed\n";
    return 0;
}
