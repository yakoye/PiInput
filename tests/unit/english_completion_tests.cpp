#include "piinput/english_completion.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        (void)std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

using piinput::apply_input_case;
using piinput::ChineseCandidateSummary;
using piinput::english_start_position;

// Real numbers measured on this machine with the shipped lexicon, in full
// pinyin: he -> 和 6252031, an -> 按 3826950, name -> 那么 501795,
// code -> 错的 105000. "time" produces 提 at 1026285 but only consumes "ti",
// so its candidate does not cover the input.
void test_full_pinyin_start_position() {
    check(english_start_position({false, false, 0}, false) == 1U,
        "no Chinese candidate puts English first");
    check(english_start_position({true, false, 1026285}, false) == 1U,
        "a candidate that does not cover the input puts English first");
    check(english_start_position({true, true, 105000}, false) == 1U,
        "a low-frequency exact match still puts English first");
    check(english_start_position({true, true, 501795}, false) == 2U,
        "a mid-frequency exact match puts English second");
    check(english_start_position({true, true, 3826950}, false) == 5U,
        "a high-frequency exact match pushes English down the row");
    check(english_start_position({true, true, 6252031}, false) == 0U,
        "a dominant exact match suppresses English entirely");
}

void test_double_pinyin_thresholds_are_stricter() {
    // Four letters land on two valid syllables far more easily in double
    // pinyin, so covers_all_input is almost always true there and cannot
    // carry the decision on its own. The frequency bar rises to compensate.
    check(english_start_position({true, true, 500000}, true) == 2U,
        "double pinyin still puts English second at 500k");
    check(english_start_position({true, true, 3826950}, true) == 0U,
        "double pinyin suppresses English where full pinyin merely demotes it");
    check(english_start_position({true, false, 3826950}, true) == 1U,
        "a guess from an incomplete reading yields to English in either schema");
}

void test_input_case_is_carried_to_the_word() {
    check(apply_input_case("book", "book") == "book",
        "lowercase input keeps the word lowercase");
    check(apply_input_case("Book", "book") == "Book",
        "capitalised input capitalises the word");
    check(apply_input_case("BOOK", "book") == "BOOK",
        "all-caps input upper-cases the word");
    check(apply_input_case("belie", "believe") == "believe",
        "a completion longer than the input still follows its case");
    check(apply_input_case("B", "book") == "Book",
        "a single capital capitalises rather than upper-cases");
    check(apply_input_case("apple", "Apple") == "Apple",
        "a word stored as a proper noun keeps its own form");
}

std::filesystem::path write_english_fixture() {
    const auto path =
        std::filesystem::temp_directory_path() / "piinput-english-completion.tsv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "believe\t1024030\t2\n"
           << "believed\t1023000\t2\n"
           << "belief\t1021406\t2\n"
           << "believes\t1020000\t2\n"
           << "book\t1023969\t2\n";
    return path;
}

void test_plan_respects_the_trigger_conditions() {
    piinput::EnglishLexicon lexicon;
    const auto path = write_english_fixture();
    check(lexicon.load_builtin_tsv(path) > 0U, "fixture loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 3U;
    const ChineseCandidateSummary none{false, false, 0};

    check(plan_english_completion("belie", none, lexicon, settings).words.size() == 3U,
        "an ordinary prefix yields up to three completions");

    settings.enabled = false;
    check(plan_english_completion("belie", none, lexicon, settings).start_position == 0U,
        "the switch being off produces nothing at all");
    settings.enabled = true;

    check(plan_english_completion("b", none, lexicon, settings).start_position == 0U,
        "a single letter is too noisy to complete");
    check(plan_english_completion("bel1", none, lexicon, settings).start_position == 0U,
        "anything but ASCII letters is not an English word");
    check(plan_english_completion("zzzz", none, lexicon, settings).start_position == 0U,
        "a prefix no word starts with yields nothing");

    const ChineseCandidateSummary dominant{true, true, 6252031};
    check(plan_english_completion("belie", dominant, lexicon, settings).start_position == 0U,
        "a dominant Chinese candidate suppresses the whole plan");

    std::filesystem::remove(path);
}

void test_plan_reports_where_to_insert() {
    piinput::EnglishLexicon lexicon;
    const auto path = write_english_fixture();
    check(lexicon.load_builtin_tsv(path) > 0U, "fixture loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 3U;

    const auto first = plan_english_completion(
        "book", {true, false, 500}, lexicon, settings);
    check(first.start_position == 1U && first.words.front() == "book",
        "a guessed Chinese reading leaves the first slot to English");

    const auto second = plan_english_completion(
        "book", {true, true, 501795}, lexicon, settings);
    check(second.start_position == 2U,
        "a solid mid-frequency Chinese word keeps the first slot");

    const auto capitalised = plan_english_completion(
        "Book", {false, false, 0}, lexicon, settings);
    check(!capitalised.words.empty() && capitalised.words.front() == "Book",
        "the plan carries the input case through to the words");

    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_full_pinyin_start_position();
    test_double_pinyin_thresholds_are_stricter();
    test_input_case_is_carried_to_the_word();
    test_plan_respects_the_trigger_conditions();
    test_plan_reports_where_to_insert();
    if (failures != 0) {
        (void)std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    (void)std::printf("english completion tests passed\n");
    return 0;
}
