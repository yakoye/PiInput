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
using piinput::plan_english_completion;

// Every case below came from real typing in double pinyin, with each score
// measured against the shipped dictionary. Two questions, kept separate:
// whether English is offered at all, and where it lands if so.
//
// Signature: english_start_position(chinese, length, typed_a_whole_word, dp)
// where chinese is {has_candidates, covers_all_input, top_score}.

// Below four letters, only a finished word counts. Three letters of pinyin
// are pinyin far more often than they are the start of an English word, and
// the plan-level guard drops these before a position is ever computed.
void test_short_prefixes_never_reach_the_row() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    check(lexicon.load_builtin_tsv(shipped) > 200000U, "shipped dictionary loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 3U;
    settings.double_pinyin = true;

    // Reported as English shouldering Chinese aside. Each prefixes a real
    // word -- wom/women, niz/nizam -- and none should offer anything.
    check(plan_english_completion("wom", {true, false, 509405}, lexicon, settings)
              .start_position == 0U,
        "wom gives 我们 at 509405 and must not offer women");
    check(plan_english_completion("niz", {true, false, 488965}, lexicon, settings)
              .start_position == 0U,
        "niz gives 你在 and must not offer nizam");
    check(plan_english_completion("jiy", {true, false, 500809}, lexicon, settings)
              .start_position == 0U,
        "jiy gives 给予");
    check(plan_english_completion("tzn", {true, false, 181275}, lexicon, settings)
              .start_position == 0U,
        "tzn gives 头脑");

    // Four letters may complete a prefix, but only toward a word people use.
    check(plan_english_completion("buco", {true, false, 500601}, lexicon, settings)
              .start_position == 0U,
        "buco leads only to bucolic, which carries no usage data");
    check(plan_english_completion("buhc", {true, false, 500381}, lexicon, settings)
              .start_position == 0U,
        "buhc leads only to buddhic");
    // Six letters opens the full dictionary, and the common-word list put
    // bucolic in it. This is the design working as stated -- the more that is
    // typed, the further the search reaches -- rather than the earlier
    // behaviour, where bucolic was reachable only by finishing it. What
    // changed is the dictionary, not the rule.
    check(plan_english_completion("bucoli", {true, false, 500601}, lexicon, settings)
              .start_position != 0U,
        "bucoli reaches bucolic now that the common-word list carries it");
}

// Everything that gets typed on the way to Chinese, with the Chinese scores
// piinput-cli actually reports for that input in 小鹤双拼 rather than guessed
// at. This is the test that answers "don't make English keep popping up".
//
// Four defences share the work, and none of them is redundant:
//
//   length          wom niz tzn jiy -- three letters and only a prefix
//   Chinese veto    nim tam wod nid ken -- real words, but 你们 at 501,135
//                   and 他们 at 504,296 are what was being typed
//   base floor      buco buhc -- four letters reaching only bucolic and
//                   buddhic, words carrying no usage data
//   the dictionary  hao xie dui mei -- the ones nothing else catches. Their
//                   Chinese is weak (洗耳 1,505, 么 9,727, 督察 162,515), so
//                   the veto lets them through; they are gone from the
//                   dictionary instead, dropped by build-english-lexicon.ps1
//                   as pinyin syllables nobody types in English.
void test_ordinary_pinyin_never_interrupts() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    check(lexicon.load_builtin_tsv(shipped) > 200000U, "shipped dictionary loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 2U;
    settings.double_pinyin = true;

    const struct {
        const char* typed;
        std::int64_t chinese;
        const char* meant;
    } quiet[] = {
        {"wom", 509405, "我们"},   {"nim", 501135, "你们"},
        {"tam", 504296, "他们"},   {"wod", 502376, "我的"},
        {"nid", 502307, "你的"},   {"ken", 501558, "可能"},
        {"niz", 488965, "你在"},   {"tzn", 181275, "头脑"},
        {"jiy", 500809, "给予"},   {"buco", 500601, "不错"},
        {"buhc", 500381, "不好"},  {"hao", 879528, "哈"},
        {"xie", 1505, "洗耳"},     {"dui", 162515, "督察"},
        {"mei", 9727, "么"},       {"womm", 29569261, "我"},
        {"zhme", 124291, "脏"},    {"keyi", 505814, "可以"},
        {"xihu", 136290, "西湖"},  {"shjn", 128277, "丧"},
    };
    for (const auto& entry : quiet) {
        const auto plan = plan_english_completion(
            entry.typed, {true, false, entry.chinese}, lexicon, settings);
        if (plan.start_position != 0U) {
            (void)std::fprintf(stderr, "  %s (%s) offered %s at slot %zu\n",
                entry.typed, entry.meant,
                plan.words.empty() ? "?" : plan.words.front().c_str(),
                plan.start_position);
        }
        check(plan.start_position == 0U,
            "ordinary pinyin offers no English at all");
    }

    // Two letters offer nothing at all, whatever the Chinese behind them.
    // This is why ta, le and de may stay in the dictionary: the length gate
    // reaches them before the dictionary does, and deleting them would have
    // meant deleting he and me alongside.
    check(plan_english_completion("wo", {true, false, 29569261}, lexicon, settings)
              .start_position == 0U,
        "two letters are one syllable and belong to Chinese outright");
    check(plan_english_completion("me", {true, true, 9727}, lexicon, settings)
              .start_position == 0U,
        "even where the Chinese is weak, two letters offer nothing");
    check(plan_english_completion("de", {true, false, 76938354}, lexicon, settings)
              .start_position == 0U,
        "de is in the dictionary at 1,022,529 and still never reaches the row");
}

// The other half of the bargain: English has to be there when it is wanted.
// Same dictionaries, same 小鹤双拼 scores, opposite expectation.
void test_english_is_there_when_it_is_wanted() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    check(lexicon.load_builtin_tsv(shipped) > 200000U, "shipped dictionary loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 2U;
    settings.double_pinyin = true;

    // Typed toward English, with whatever Chinese happens to sit behind it.
    const struct {
        const char* typed;
        std::int64_t chinese;
        const char* expected;
    } wanted[] = {
        {"wome", 14230, "women"},      // 我么 is weak; the word comes second
        {"book", 0, "book"},           // decodes to no Chinese at all
        {"belie", 0, "believe"},
        {"pallad", 0, "palladium"},
        {"bucolic", 500601, "bucolic"},  // obscure, but finished
        {"ephemeral", 0, "ephemeral"},
        {"obsidian", 0, "obsidian"},
    };
    for (const auto& entry : wanted) {
        const bool has_chinese = entry.chinese > 0;
        const auto plan = plan_english_completion(
            entry.typed, {has_chinese, false, entry.chinese}, lexicon, settings);
        if (plan.words.empty()) {
            (void)std::fprintf(stderr, "  %s offered nothing\n", entry.typed);
        }
        check(!plan.words.empty() && plan.words.front() == entry.expected,
            "the word that was being typed is offered");
        check(plan.start_position != 0U && plan.start_position <= 2U,
            "and it is offered where it can be reached");
    }
}

// Finishing a word is a statement about what was meant, however obscure the
// word. bucolic is offered where buco and bucoli are not, and the difference
// is not the word but whether it was completed.
void test_a_finished_word_is_offered_however_obscure() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    check(lexicon.load_builtin_tsv(shipped) > 200000U, "shipped dictionary loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 3U;
    settings.double_pinyin = true;

    const auto bucolic =
        plan_english_completion("bucolic", {true, false, 500601}, lexicon, settings);
    check(bucolic.start_position == 2U,
        "bucolic is offered, behind 不错 at 500601");
    check(!bucolic.words.empty() && bucolic.words.front() == "bucolic",
        "and it offers the word that was typed");

    // Three letters is always mid-word in double pinyin -- 擦 is merely what
    // `car` passes through on its way to 擦人 -- so a three-letter word is
    // offered at the back of the row rather than second. Other input methods
    // put it second; that interrupts the typing it was passing through.
    const auto car =
        plan_english_completion("car", {true, false, 172541}, lexicon, settings);
    check(car.start_position == 5U, "car waits at the back, behind 擦 at 172541");
    check(car.words.size() == 1U && car.words.front() == "car",
        "a whole short word stands alone rather than dragging carbon along");

    // `bus` used to be declined here, on the theory that 不是 at 501,670 was
    // what anyone typing three letters meant. That test cannot be made to
    // work: `dog` reaches 多个 at 500,505, near enough identical, and `dog` is
    // plainly a word people type. Both are offered now, and both wait at the
    // back of the row where they interrupt nothing.
    const auto bus = plan_english_completion("bus", {true, false, 501670}, lexicon, settings);
    check(bus.start_position == 5U && bus.words.size() == 1U && bus.words.front() == "bus",
        "bus is offered at the back of the row, behind 不是 at 501670");
    const auto dog = plan_english_completion("dog", {true, false, 500505}, lexicon, settings);
    check(dog.start_position == 5U && dog.words.size() == 1U && dog.words.front() == "dog",
        "and so is dog, whose 多个 at 500505 is the same strength");

    // Every three-letter word here is one the twenty-thousand common-word list
    // carries, and each has Chinese behind it strong enough that the old
    // Chinese-side veto would have taken it. Membership is what decides now.
    for (const char* const word : {
            "dog", "egg", "cat", "bus", "car", "cup", "bed", "boy", "sun",
            "map", "box", "key", "job", "red", "top", "big", "new", "run"}) {
        const auto plan = plan_english_completion(
            word, {true, false, 500000}, lexicon, settings);
        if (plan.words.empty()) {
            (void)std::fprintf(stderr, "  %s was not offered\n", word);
        }
        check(!plan.words.empty() && plan.words.front() == word,
            "an everyday three-letter word is offered whatever the Chinese");
    }
    // The same Chinese strength no longer vetoes once the input is long
    // enough to have declared itself.
    check(plan_english_completion("bucolic", {true, false, 500601}, lexicon, settings)
              .start_position == 2U,
        "bucolic is offered although its 不错 scores just as much as bus's 不是");
}

void test_the_front_slot_needs_all_three_conditions() {
    // Nothing to compete with.
    check(english_start_position({false, false, 0}, 4U, true, true) == 1U,
        "book decodes to no Chinese at all");
    check(english_start_position({false, false, 0}, 6U, true, true) == 1U,
        "so do pallad and quixotic");

    // Finished, long, and the Chinese yields: women over 我么那 at 14,230.
    check(english_start_position({true, true, 14230}, 5U, true, true) == 1U,
        "women leads where its Chinese scores only 14230");
    // Same Chinese, one letter shorter, and only a prefix.
    check(english_start_position({true, true, 14230}, 4U, false, true) == 2U,
        "wome is offered second though its Chinese scores the same");
    // Finished and long, but the Chinese is established.
    check(english_start_position({true, false, 500601}, 7U, true, true) == 2U,
        "bucolic yields the lead to 不错 at 500601");
    // Finished and long, and the Chinese is not.
    check(english_start_position({true, false, 4444}, 7U, true, true) == 1U,
        "paladin leads where its Chinese scores 4444");
}

void test_full_pinyin_shares_the_same_rules() {
    check(english_start_position({false, false, 0}, 4U, true, false) == 1U,
        "no Chinese candidate puts English first in either schema");
    check(english_start_position({true, true, 14230}, 5U, true, false) == 1U,
        "a finished long word leads in either schema");
    check(english_start_position({true, true, 500601}, 7U, true, false) == 2U,
        "established Chinese holds the lead in either schema");
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

    // max_items caps the row only where Chinese has to share it. `belie`
    // decodes to nothing, so there is nothing to protect and the row fills.
    check(plan_english_completion("belie", none, lexicon, settings).words.size() > 3U,
        "with no Chinese to crowd, the row fills past max_items");
    check(plan_english_completion("belie", {true, false, 500000}, lexicon, settings)
              .words.size() <= 3U,
        "and holds to max_items as soon as there is Chinese in the row");

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

    // Strong Chinese moves English down the row rather than removing it.
    // Observed in another input method: bucolic is still offered behind 不错
    // at 500,601. What removes English is the input not looking like a word,
    // never the Chinese being good.
    const ChineseCandidateSummary dominant{true, true, 6252031};
    const auto behind = plan_english_completion("belie", dominant, lexicon, settings);
    check(behind.start_position == 2U,
        "a dominant Chinese candidate takes the lead but not the whole row");

    std::filesystem::remove(path);
}

void test_plan_reports_where_to_insert() {
    piinput::EnglishLexicon lexicon;
    const auto path = write_english_fixture();
    check(lexicon.load_builtin_tsv(path) > 0U, "fixture loads");

    piinput::EnglishCompletionSettings settings;
    settings.enabled = true;
    settings.max_items = 3U;

    // Four letters is a word but not yet a commitment, so it follows the
    // Chinese however weak that is.
    const auto four = plan_english_completion(
        "book", {true, false, 500}, lexicon, settings);
    check(four.start_position == 2U && four.words.front() == "book",
        "a four-letter word is offered second even against negligible Chinese");
    check(plan_english_completion("book", {true, true, 501795}, lexicon, settings)
              .start_position == 2U,
        "and stays second against strong Chinese too");

    // Finishing a longer word earns the lead, provided the Chinese is not
    // already established.
    check(plan_english_completion("believe", {true, false, 500}, lexicon, settings)
              .start_position == 1U,
        "a finished long word takes the lead against negligible Chinese");
    check(plan_english_completion("believe", {true, false, 501795}, lexicon, settings)
              .start_position == 2U,
        "the same word yields to Chinese that is actually established");
    // A prefix never leads, however long it grows.
    check(plan_english_completion("belie", {true, false, 500}, lexicon, settings)
              .start_position == 2U,
        "belie only prefixes believe, so it follows the Chinese");

    const auto capitalised = plan_english_completion(
        "Book", {false, false, 0}, lexicon, settings);
    check(!capitalised.words.empty() && capitalised.words.front() == "Book",
        "the plan carries the input case through to the words");

    std::filesystem::remove(path);
}

void test_english_never_renumbers_a_shortcut() {
    using piinput::english_insert_index;

    // calc: Chinese at 1, the calculator shortcut at 2. English wants the
    // first slot, but taking it would push the calculator to three.
    const std::vector<bool> calculator_at_two{false, true, false, false};
    check(english_insert_index(1U, calculator_at_two) == 2U,
        "English starts after the shortcut rather than renumbering it");
    check(english_insert_index(2U, calculator_at_two) == 2U,
        "a request for the shortcut's own slot lands just past it");

    // Two shortcuts below the requested position: clearing only the first
    // would still renumber the second.
    const std::vector<bool> two_shortcuts{false, true, true, false};
    check(english_insert_index(1U, two_shortcuts) == 3U,
        "English clears every shortcut, not just the first");

    const std::vector<bool> none{false, false, false};
    check(english_insert_index(1U, none) == 0U,
        "with no shortcuts English lands exactly where it asked");
    check(english_insert_index(2U, none) == 1U,
        "the second slot is index one");
    check(english_insert_index(9U, none) == 3U,
        "a position past the end appends rather than overruns");
    check(english_insert_index(0U, none) == 0U,
        "a suppressed plan has nothing to place");
}

// The shipped dictionary is layered: the original 24,323 word table sits
// above a large open word list. Coverage is the whole reason the large list
// is there -- spelling help is most needed for words nobody types often, and
// the original table had none of them.
void test_shipped_dictionary_layers_do_not_overlap() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    const auto loaded = lexicon.load_builtin_tsv(shipped);
    // Far past the 24,323 the original table held, which is what says the
    // layered build is in place rather than the old one.
    check(loaded > 200000U, "the shipped dictionary carries the layered word list");

    const auto believe = lexicon.query("believ", 4U);
    check(!believe.empty() && believe.front().word == "believe",
        "an everyday word still wins its own prefix");
    check(!believe.empty() && believe.front().base_weight > 1000000U,
        "everyday words live in the band the large list cannot reach");

    // Only three candidates are ever shown, so being present in the file is
    // not enough -- the word has to survive the ranking. An earlier build put
    // palladia and palladic ahead of it purely because they are shorter,
    // which is exactly the failure a frequency signal exists to prevent.
    const auto palladium = lexicon.query("pallad", 3U);
    const bool covered = std::any_of(palladium.begin(), palladium.end(),
        [](const piinput::EnglishCandidate& candidate) {
            return candidate.word == "palladium";
        });
    check(covered, "a word the original table lacked is reachable within three");
    // It used to be asserted that palladium stayed below the everyday band.
    // The curated common-word list carries it, at 5,258, so it is in that band
    // now -- and that is the list doing its job, not a regression. What the
    // ranking still has to get right is the part that mattered: palladium
    // ahead of palladia and palladic, which are shorter and have no usage data
    // at all.
    check(!palladium.empty() && palladium.front().word == "palladium",
        "and it leads the shorter prefix-mates the original build put first");
}

// The reported words were not near-misses; they had nothing to do with what
// was typed. Two separate causes, both fixed here, both worth pinning down.
void test_the_reported_junk_words_stay_out() {
    piinput::EnglishLexicon lexicon;
    const std::filesystem::path shipped =
        std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "english_lexicon.tsv";
    check(lexicon.load_builtin_tsv(shipped) > 200000U, "shipped dictionary loads");

    piinput::EnglishQueryOptions strict;
    strict.limit = 3U;
    strict.allow_subsequence = false;
    strict.minimum_weight = 200000U;

    const auto contains = [](const std::vector<piinput::EnglishCandidate>& list,
                             const std::string& word) {
        return std::any_of(list.begin(), list.end(),
            [&](const piinput::EnglishCandidate& c) { return c.word == word; });
    };

    // Subsequence matching reached these. The letters are pinyin, so
    // completing them as an English abbreviation is meaningless: tzn is not
    // an abbreviation of tarzan, it is 头脑.
    check(!contains(lexicon.query("tzn", strict), "tarzan"),
        "tzn no longer reaches tarzan");
    check(!contains(lexicon.query("jiy", strict), "jimmy"),
        "jiy no longer reaches jimmy");
    check(!contains(lexicon.query("jiy", strict), "juicy"),
        "jiy no longer reaches juicy");
    check(!contains(lexicon.query("buhc", strict), "buddhic"),
        "buhc no longer reaches buddhic");

    // bucolic used to be here too, on the grounds that it carried no usage
    // data. The common-word list carries it -- at 16,841 of 17,030, but
    // carries it -- so it now sits in the curated band and the query returns
    // it. Keeping it out of a four-letter prefix is the completion policy's
    // job instead, and test_short_prefixes_never_reach_the_row holds that.
    check(!contains(lexicon.query("buco", strict), "bucorvus"),
        "buco no longer offers bucorvus");
    check(!contains(lexicon.query("niz", strict), "nizy"),
        "niz no longer offers nizy");
    check(!contains(lexicon.query("niz", strict), "nizey"),
        "niz no longer offers nizey");

    // What the feature is actually for must still work under the same rules.
    check(contains(lexicon.query("belie", strict), "believe"),
        "belie still completes to believe");
    check(contains(lexicon.query("book", strict), "book"),
        "book still completes to book");
    check(contains(lexicon.query("pallad", strict), "palladium"),
        "pallad still completes to palladium");
}

}  // namespace

int main() {
    test_short_prefixes_never_reach_the_row();
    test_ordinary_pinyin_never_interrupts();
    test_english_is_there_when_it_is_wanted();
    test_a_finished_word_is_offered_however_obscure();
    test_the_front_slot_needs_all_three_conditions();
    test_full_pinyin_shares_the_same_rules();
    test_the_reported_junk_words_stay_out();
    test_shipped_dictionary_layers_do_not_overlap();
    test_english_never_renumbers_a_shortcut();
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
