#include "piinput/composition_caret.h"
#include "piinput/english_lexicon.h"
#include "piinput/english_key_policy.h"
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

static_assert(static_cast<std::uint32_t>(piinput::EnglishCandidateFlag::builtin) == 1U);
static_assert(static_cast<std::uint32_t>(piinput::EnglishCandidateFlag::downloaded) == 2U);
static_assert(static_cast<std::uint32_t>(piinput::EnglishCandidateFlag::user) == 4U);
static_assert(static_cast<std::uint32_t>(piinput::EnglishCandidateFlag::proper) == 8U);

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
        "ASCII prefix matching is case-insensitive and base weights sort deterministically");
    check(lexicon.query("apple", 1U).front().flags == 9U,
        "numeric third-column flags are preserved on candidates");
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
        "alpha\t10\t1\n"
        "alpha\t30\t8\n"
        "ALPHA\t20\t0\n"
        "algebra\t1000\n"
        "beta\tbad\n"
        "two words\t99\n"
        "\t20\n"
        "gamma\t0\n"
        "delta\t40\t1\textra\n"
        "cafe\xCC\x81\t60\n"
        "negative\t1\t-1\n"
        "overflow\t1\t4294967296\n"
        "textflag\t1\tbuiltin\n"
        "emptyflag\t1\t\n"
        "zero\t1\t0\n");
    write_text(user,
        "alpine\t1\t4\n"
        "alpha\t80\t4\n"
        "ALPHA\t90\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 5U,
        "negative, overflow, text, empty, and extra-column flags are rejected");
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
    check(candidates[0].flags == 0U && candidates[0].user_entry,
        "two-column user rows default flags to zero while user_entry stays independent");
    check(candidates[1].flags == 13U,
        "exact duplicates deterministically merge numeric flags with bitwise OR");
    const auto algebra = lexicon.query("algebra", 1U);
    check(algebra.front().flags == 0U,
        "two-column built-in rows default flags to zero");
    const auto alpine = lexicon.query("alpine", 1U);
    check(alpine.front().flags == 4U && alpine.front().user_entry,
        "numeric user flags load without replacing the independent user_entry marker");
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

void test_independent_lexicons_merge_pending_learning_without_lost_updates() {
    const auto directory = make_temp_directory("learning-merge");
    const auto builtin = directory / "builtin.tsv";
    const auto learning = directory / "learning.tsv";
    write_text(builtin, "alpha\t10\n");

    piinput::EnglishLexicon first;
    piinput::EnglishLexicon second;
    check(first.load_builtin_tsv(builtin) == 1U && second.load_builtin_tsv(builtin) == 1U,
        "independent learning fixtures load");
    check(first.record_selection("alpha") && second.record_selection("alpha"),
        "independent processes can each record a local delta");
    check(first.save_learning_tsv(learning), "first process saves its pending delta");
    check(second.save_learning_tsv(learning), "second process merges instead of overwriting");

    piinput::EnglishLexicon reloaded;
    check(reloaded.load_builtin_tsv(builtin) == 1U, "merged learning fixture reloads");
    check(reloaded.load_learning_tsv(learning) == 1U, "merged learning row is valid");
    check(reloaded.query("a", 1U).front().learning_count == 2U,
        "two independent saves preserve both selection counts");
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        check(item.path().filename().string().find(".tmp.") == std::string::npos,
            "successful learning save leaves no unique temporary file");
    }
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

void test_english_session_candidate_limit_updates_at_boundary() {
    const auto directory = make_temp_directory("session-limit");
    const auto builtin = directory / "builtin.tsv";
    write_text(builtin, "Apple\t100\napplication\t80\napply\t60\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 3U, "candidate-limit fixture loads");
    piinput::EnglishSession session(lexicon, 1U);
    session.insert('a');
    session.insert('p');
    check(session.snapshot().candidates.size() == 1U,
        "old candidate limit applies before settings update");
    session.set_candidate_limit(3U);
    check(session.snapshot().candidates.size() == 3U,
        "updated candidate limit refreshes the active session safely");
    std::filesystem::remove_all(directory);
}

void test_composition_caret_mapping_covers_navigation_boundaries() {
    check(piinput::map_composition_caret(4U, 0U) ==
            piinput::CompositionCaretMapping{0U, 0L},
        "composition caret maps the start to a zero ShiftEnd distance");
    check(piinput::map_composition_caret(4U, 2U) ==
            piinput::CompositionCaretMapping{2U, 2L},
        "composition caret maps a middle position");
    check(piinput::map_composition_caret(4U, 4U) ==
            piinput::CompositionCaretMapping{4U, 4L},
        "composition caret maps the end");
    check(piinput::map_composition_caret(4U, 99U) ==
            piinput::CompositionCaretMapping{4U, 4L},
        "composition caret clamps a position beyond the text");
}

void test_composition_caret_mapping_follows_delete_edits() {
    const auto directory = make_temp_directory("caret-delete");
    const auto builtin = directory / "builtin.tsv";
    write_text(builtin, "Apple\t100\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 1U, "caret fixture loads");
    piinput::EnglishSession session(lexicon);
    session.insert('a');
    session.insert('p');
    session.insert('p');
    session.move_home();
    session.delete_forward();
    const auto& snapshot = session.snapshot();
    check(snapshot.input == "pp" && snapshot.caret == 0U,
        "delete retains the internal caret before host mapping");
    check(piinput::map_composition_caret(snapshot.input.size(), snapshot.caret) ==
            piinput::CompositionCaretMapping{0U, 0L},
        "host caret mapping follows the post-delete position");
    std::filesystem::remove_all(directory);
}

void test_user_priority_precedes_builtin_learning() {
    const auto directory = make_temp_directory("ranking-user-learning");
    const auto builtin = directory / "builtin.tsv";
    const auto user = directory / "user.tsv";
    write_text(builtin, "alpha\t100\n");
    write_text(user, "alpine\t1\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 1U, "ranking built-in fixture loads");
    check(lexicon.load_user_tsv(user) == 1U, "ranking user fixture loads");
    for (int count = 0; count < 20; ++count) {
        check(lexicon.record_selection("alpha"), "built-in learning selection records");
    }
    check(words(lexicon.query("al", 2U)) ==
            std::vector<std::string>({"alpine", "alpha"}),
        "user priority precedes learning count");
    std::filesystem::remove_all(directory);
}

void test_shorter_completion_precedes_stable_id_on_equal_scores() {
    const auto directory = make_temp_directory("ranking-completion");
    const auto builtin = directory / "builtin.tsv";
    write_text(builtin, "application\t50\napply\t50\naptly\t50\n");

    piinput::EnglishLexicon lexicon;
    check(lexicon.load_builtin_tsv(builtin) == 3U, "completion fixture loads");
    check(words(lexicon.query("ap", 3U)) ==
            std::vector<std::string>({"apply", "aptly", "application"}),
        "shorter completion precedes stable ID and stable ID resolves equal lengths");
    std::filesystem::remove_all(directory);
}

void test_english_key_policy_gates_mode_and_lazy_loading() {
    using piinput::EnglishKeyAction;
    using piinput::EnglishKeyContext;
    using piinput::EnglishKeyKind;
    using piinput::EnglishKeyPolicy;

    const auto disabled = EnglishKeyPolicy::decide(
        EnglishKeyContext{true, false, false, false}, EnglishKeyKind::letter);
    check(disabled == piinput::EnglishKeyDecision{
            EnglishKeyAction::pass_through, false, false},
        "default-off English letters pass through without loading");

    const auto chinese = EnglishKeyPolicy::decide(
        EnglishKeyContext{false, true, false, false}, EnglishKeyKind::letter);
    check(chinese == piinput::EnglishKeyDecision{
            EnglishKeyAction::pass_through, false, false},
        "Chinese mode never triggers English resources");

    const auto first_letter = EnglishKeyPolicy::decide(
        EnglishKeyContext{true, true, false, false}, EnglishKeyKind::letter);
    check(first_letter == piinput::EnglishKeyDecision{
            EnglishKeyAction::start_composition, true, true},
        "first enabled English letter starts Composition and loads once");

    const auto warm_start = EnglishKeyPolicy::decide(
        EnglishKeyContext{true, true, false, true}, EnglishKeyKind::letter);
    check(warm_start == piinput::EnglishKeyDecision{
            EnglishKeyAction::start_composition, true, false},
        "an already loaded resource is reused at the next Composition");

    const auto next_letter = EnglishKeyPolicy::decide(
        EnglishKeyContext{true, true, true, true}, EnglishKeyKind::letter);
    check(next_letter == piinput::EnglishKeyDecision{
            EnglishKeyAction::insert_letter, true, false},
        "each following letter queries memory without loading resources");
}

void test_english_key_policy_covers_passthrough_commit_and_grid_actions() {
    using piinput::EnglishKeyAction;
    using piinput::EnglishKeyContext;
    using piinput::EnglishKeyKind;
    using piinput::EnglishKeyPolicy;

    const EnglishKeyContext idle{true, true, false, false};
    check(EnglishKeyPolicy::decide(idle, EnglishKeyKind::punctuation).action ==
            EnglishKeyAction::pass_through,
        "idle English punctuation remains system passthrough");

    const EnglishKeyContext composing{true, true, true, true};
    const std::vector<std::pair<EnglishKeyKind, EnglishKeyAction>> cases{
        {EnglishKeyKind::punctuation, EnglishKeyAction::commit_then_punctuation},
        {EnglishKeyKind::literal, EnglishKeyAction::commit_then_literal},
        {EnglishKeyKind::digit, EnglishKeyAction::choose_digit},
        {EnglishKeyKind::space, EnglishKeyAction::choose_current},
        {EnglishKeyKind::enter, EnglishKeyAction::commit_raw},
        {EnglishKeyKind::escape, EnglishKeyAction::cancel},
        {EnglishKeyKind::backspace, EnglishKeyAction::backspace},
        {EnglishKeyKind::delete_forward, EnglishKeyAction::delete_forward},
        {EnglishKeyKind::move_left, EnglishKeyAction::move_left},
        {EnglishKeyKind::move_right, EnglishKeyAction::move_right},
        {EnglishKeyKind::move_home, EnglishKeyAction::move_home},
        {EnglishKeyKind::move_end, EnglishKeyAction::move_end},
        {EnglishKeyKind::previous_row, EnglishKeyAction::previous_row},
        {EnglishKeyKind::next_row, EnglishKeyAction::next_row},
        {EnglishKeyKind::previous_page, EnglishKeyAction::previous_page},
        {EnglishKeyKind::next_page, EnglishKeyAction::next_page},
    };
    for (const auto& [key, expected] : cases) {
        const auto decision = EnglishKeyPolicy::decide(composing, key);
        check(decision.action == expected && decision.consume && !decision.load_resources,
            "composing English key maps to its state-machine action");
    }
    check(EnglishKeyPolicy::decide(composing, EnglishKeyKind::other).action ==
            EnglishKeyAction::pass_through,
        "unsupported English keys remain passthrough");
}

void test_edit_session_results_and_single_commit_plans_are_atomic() {
    check(piinput::classify_english_ascii_key('0', false) ==
            piinput::EnglishKeyKind::literal,
        "unshifted zero is consumed as a literal suffix during composition");
    check(piinput::classify_english_ascii_key('0', true) ==
            piinput::EnglishKeyKind::punctuation,
        "shifted zero remains closing-parenthesis punctuation");
    check(piinput::classify_english_ascii_key('5', false) ==
            piinput::EnglishKeyKind::digit,
        "digits one through nine still select candidates");
    check(piinput::edit_session_succeeded(0, 0),
        "successful RequestEditSession and DoEditSession results commit state");
    check(!piinput::edit_session_succeeded(static_cast<std::int32_t>(0x80004005U), 0),
        "failed RequestEditSession keeps input state");
    check(!piinput::edit_session_succeeded(0, static_cast<std::int32_t>(0x80004005U)),
        "failed DoEditSession keeps input state");

    const auto candidate = piinput::build_english_commit_plan("app", "Apple", ",");
    check(candidate.text == "Apple," && candidate.used_candidate,
        "candidate and punctuation are submitted in one commit string");
    const auto raw = piinput::build_english_commit_plan("zz", std::nullopt, "0");
    check(raw.text == "zz0" && !raw.used_candidate,
        "raw composition and an unshifted zero are submitted atomically");
}

}  // namespace

int main() {
    test_default_disabled_gate_does_not_start_english_composition();
    test_prefix_case_and_stable_sorting();
    test_invalid_rows_duplicates_and_user_merge();
    test_learning_promotes_and_persists_atomically();
    test_learning_overflow_and_damaged_rows_are_safe();
    test_independent_lexicons_merge_pending_learning_without_lost_updates();
    test_english_session_composition_editing_and_choice();
    test_english_session_can_disable_learning();
    test_english_session_candidate_limit_updates_at_boundary();
    test_composition_caret_mapping_covers_navigation_boundaries();
    test_composition_caret_mapping_follows_delete_edits();
    test_user_priority_precedes_builtin_learning();
    test_shorter_completion_precedes_stable_id_on_equal_scores();
    test_english_key_policy_gates_mode_and_lazy_loading();
    test_english_key_policy_covers_passthrough_commit_and_grid_actions();
    test_edit_session_results_and_single_commit_plans_are_atomic();

    if (failures != 0) {
        std::cerr << failures << " English lexicon test(s) failed\n";
        return 1;
    }
    std::cout << "All English lexicon tests passed\n";
    return 0;
}
