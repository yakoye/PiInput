#include "piinput/english_completion.h"

#include <algorithm>
#include <cctype>

namespace piinput {
namespace {

// English is offered only where an English word was plausibly typed.
//
// The first attempt judged that from the Chinese side, treating an incomplete
// reading as evidence the candidates were guesswork. That reasoning collapses
// in double pinyin, where a trailing half-syllable is completely ordinary:
// `wom` is wo + m, `niz` is ni + z, and the engine completes those on purpose
// -- it is how `mkt` gives 明天. The signal read "uncertain" where it meant
// "business as usual", and English shouldered aside 我们, 不错 and 你在.
//
// Two conditions carry the decision instead, and between them they account
// for every case reported from real typing:
//
//   wom niz tzn jiy     three letters and only a prefix -- refused by length
//   buco buhc           four letters, but reaching only bucolic and buddhic,
//                       words with no usage data -- refused by the floor
//   cat me wome women   offered
//   book belie pallad   offered; these decode to no Chinese at all
//
// Note what is absent: how strong the Chinese is. An earlier version gated on
// that and got `buco` right for the wrong reason -- its Chinese happens to
// score 500,601, but the actual problem is that nobody types bucolic.

// Three letters before English is offered at all. Two is exactly one syllable
// in double pinyin, and the characters reached there are the most common in
// the language -- 我 at 29,569,261, 的 at 76,938,354. Nothing English wins
// against those.
constexpr std::size_t kMinimumLength = 3U;

// The floor where nothing decodes to Chinese. One letter matches most of the
// dictionary and says nothing about what was wanted; two already narrows it,
// and there is no Chinese candidate for it to push aside.
constexpr std::size_t kMinimumLengthWithoutChinese = 2U;

// Below this, only the curated base dictionary is consulted. Those 24,180
// words were selected by frequency, so `book` and `believe` are there while
// bucolic, palladium and quixotic are not. Three and four letters are still
// mid-word in double pinyin, and reaching into the long tail there is what
// produced buco -> bucolic.
constexpr std::size_t kLengthForFullDictionary = 5U;

// How common a word has to be for three or four letters to guess at it.
//
// This used to be the base dictionary's own boundary, 1,000,000 -- "anything
// in the curated list". That stopped meaning much once the curated list grew
// to take in the whole twenty-thousand common-word table: `bucolic` sits at
// 16,841 of 17,030 there, which put it at 1,007,483, and `buco` started
// answering 不错 with it again.
//
// Measured rather than picked. Everything a four-letter prefix should reach
// bottoms out at preview 1,016,244, with the rest at 1,022,804 and above
// (people, because, through, women, information, problem, animal). Everything
// it should not tops out at ephemeral 1,011,918, then bucolic 1,007,483. The
// line sits in the gap between those two.
constexpr std::uint64_t kBaseDictionaryFloor = 1014000U;

// Below this a prefix does not count -- only a word finished. Three letters
// of pinyin are pinyin far more often than the opening of an English word:
// wom, niz, tzn and jiy each prefix a real word and none should offer one.
constexpr std::size_t kLengthAllowingPrefixes = 4U;

// Where it appears follows the length of what was typed. `wome` and `women`
// reach Chinese of identical score (14,230), yet the four-letter one is
// offered second and the five-letter one first.
constexpr std::size_t kLengthForFirstSlot = 5U;

// Where a three-letter word goes. Double pinyin spells a syllable in two
// letters, so three is always mid-word -- 我们 passes through wom on its way
// to womm, and car, men and big are likewise 擦+r, 么+n and 壁+g in passing.
// Other input methods put English second here; that interrupts the typing it
// was passing through. Offered, but far enough down to be ignorable.
constexpr std::size_t kShortInputSlot = 5U;

// Floor for entries the dictionary build gave a real frequency to. Below it
// lies the bulk word list, which supplies spellings and no usage data at all
// -- bucolic at 155,354 and buddhic at 155,349 both live there.
//
// It applies to prefixes only. Typing a word out in full is a statement about
// what you meant, however obscure the word: `bucolic` is offered, while
// `bucoli` is not, and the difference is not the word but whether it was
// finished. Guessing that four letters were headed somewhere nobody goes is a
// different matter entirely.
// Kept in step with build-english-lexicon.ps1, where the bands start at
// 200,001.
constexpr std::uint64_t kFrequencyBandFloor = 200000U;

// Above this the Chinese is established enough to hold the first slot, so
// English follows it. 我么那 at 14,230 lets `women` lead; 不错 at 500,601
// keeps `bucolic` second, and 擦 at 172,541 does the same for `cat`.
constexpr std::int64_t kChineseHoldsTheLead = 100000;

// Three letters is always mid-word in double pinyin, so the question there is
// whether the word is common enough to be worth mentioning at all.
//
// It was first asked of the Chinese -- decline when the reading scores above
// 200,000 -- and that cannot work: `dog` reaches 多个 at 500,505 and `bus`
// reaches 不是 at 501,670, near enough identical. Any line through the Chinese
// takes both or neither, and it took `dog`.
//
// Asking it of the word by weight fails differently. Any threshold cuts the
// middle out of a continuous list, and everything just below it is a word
// somebody types: at 1,020,000 the list still reads odd, sum, vol, hop.
//
// So it is asked as membership instead. The dictionary build marks every word
// carried by the twenty-thousand common-word list, which is a direct answer to
// the question rather than a proxy for it, and it separates the two groups
// cleanly: dog, egg, cat, bus and car are all in it, while tam, nim, nid, wod,
// niz, tzn, jiy, wom and ken are all absent.
[[nodiscard]] bool is_common_word(const EnglishCandidate& candidate) noexcept {
    // Learned and user words count too: typed once, they are common to you.
    if (candidate.user_entry || candidate.learning_count > 0U) return true;
    return (candidate.flags &
        static_cast<std::uint32_t>(EnglishCandidateFlag::common)) != 0U;
}

// How many words to offer where the input decodes to no Chinese at all. The
// ordinary cap of two exists to keep English out of a row that belongs to
// Chinese; with no Chinese in it there is nothing to keep out, and two leaves
// most of the row blank while the wanted spelling sits just past the cut.
// Six fills the row a candidate window shows without needing a second page.
constexpr std::size_t kItemsWithNoChinese = 6U;

[[nodiscard]] bool is_ascii_letter(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] bool is_ascii_upper(const char value) noexcept {
    return value >= 'A' && value <= 'Z';
}

[[nodiscard]] bool same_word_ignoring_case(
    const std::string_view left, const std::string_view right) noexcept {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(),
            [](const char a, const char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            });
}

}  // namespace

std::size_t english_start_position(
    const ChineseCandidateSummary& chinese,
    const std::size_t input_length,
    const bool typed_a_whole_word,
    const bool double_pinyin) noexcept {
    (void)double_pinyin;
    // Nothing to compete with: this is the spelling-help case the feature
    // exists for -- book, belie and pallad decode to no Chinese at all.
    if (!chinese.has_candidates) return 1U;

    // Three letters is mid-word in double pinyin, so English waits at the back
    // rather than interrupting.
    if (input_length < kLengthAllowingPrefixes) return kShortInputSlot;

    // The first slot asks for all three: a word finished rather than started,
    // long enough to be unlikely as pinyin, and Chinese weak enough to yield.
    // `women` clears all three; `bucolic` is finished and long but its 不错
    // scores 500,601, and `wome` is only a prefix.
    if (typed_a_whole_word && input_length >= kLengthForFirstSlot &&
        chinese.top_score < kChineseHoldsTheLead) {
        return 1U;
    }
    return 2U;
}

std::size_t english_insert_index(
    const std::size_t start_position, const std::vector<bool>& reserved) noexcept {
    if (start_position == 0U) return 0U;
    std::size_t index = start_position - 1U;
    // Walking the whole list rather than stopping at the first shortcut: two
    // shortcuts can both sit below the requested position, and clearing only
    // the first would still renumber the second.
    for (std::size_t position = 0U; position < reserved.size(); ++position) {
        if (reserved[position] && position >= index) {
            index = position + 1U;
        }
    }
    return (std::min)(index, reserved.size());
}

std::string apply_input_case(
    const std::string_view input, const std::string_view word) {
    std::string result(word);
    if (input.empty() || result.empty()) return result;

    // A single capital is the start of a capitalised word far more often than
    // it is the start of a shouted one, so it only capitalises.
    const bool shouting = input.size() > 1U &&
        std::all_of(input.begin(), input.end(), is_ascii_upper);
    if (shouting) {
        for (char& value : result) {
            value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
        }
        return result;
    }
    if (is_ascii_upper(input.front())) {
        result.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result.front())));
    }
    return result;
}

EnglishCompletionPlan plan_english_completion(
    const std::string_view input,
    const ChineseCandidateSummary& chinese,
    const EnglishLexicon& lexicon,
    const EnglishCompletionSettings& settings) {
    if (!settings.enabled || settings.max_items == 0U) return {};
    if (!std::all_of(input.begin(), input.end(), is_ascii_letter)) return {};

    // Every length rule below exists to keep English out of a row that belongs
    // to Chinese. Where the input decodes to no Chinese at all, there is no
    // such row, and applying them anyway is what made English seem to appear
    // out of nowhere: `gi`, `gir` and `girl` are not readings in double pinyin,
    // so the row stood empty for two keystrokes and then filled at the fourth.
    //
    // Two letters is still the floor. One matches most of the dictionary and
    // says nothing about what was wanted.
    const bool row_belongs_to_chinese = chinese.has_candidates;
    const std::size_t minimum_length =
        row_belongs_to_chinese ? kMinimumLength : kMinimumLengthWithoutChinese;
    // Two letters is exactly one syllable, and the characters living there are
    // the most common in the language: 我 at 29 million, 的 at 76 million.
    // Every English word that collides -- wo, ni, de, he, you -- loses to them
    // every time, so none is offered.
    if (input.size() < minimum_length) return {};

    EnglishQueryOptions options;
    // Wide enough that the obscure entries dropped below cannot crowd out the
    // usable ones before the filter runs. The query sorts by weight, so the
    // words worth keeping are at the front of this window regardless.
    options.limit = 32U;
    // Prefix only. The letters being typed are pinyin, so completing them as
    // an English abbreviation produces words unrelated to the input: tzn
    // reached tarzan, jiy reached jimmy, buhc reached buddhic.
    options.allow_subsequence = false;
    // No floor at this stage: a word typed out in full is exempt from it, and
    // filtering here would drop `bucolic` (155,354) before that exemption
    // could apply. The floor is enforced below, once completions can be told
    // from finished words.
    options.minimum_weight = 0U;
    auto matches = lexicon.query(input, options);
    if (matches.empty()) return {};

    // Short input sees only the curated base dictionary. Three and four
    // letters are still mid-word in double pinyin, and reaching into the long
    // tail there is exactly what answered `buco` with bucolic. Past that the
    // input has committed, and the rest of the dictionary opens up so a
    // forgotten spelling can actually be found.
    const std::uint64_t completion_floor = input.size() >= kLengthForFullDictionary
        ? kFrequencyBandFloor
        : kBaseDictionaryFloor;

    const bool typed_a_whole_word = std::any_of(matches.begin(), matches.end(),
        [&](const EnglishCandidate& candidate) {
            return same_word_ignoring_case(candidate.word, input);
        });

    // Below four letters only a complete word counts, and only a common one.
    // Both conditions are on the input rather than the Chinese behind it,
    // because the Chinese cannot tell `dog` from `bus` -- see is_common_word.
    //
    // Skipped where nothing decodes: three letters is mid-word only if there
    // is a word to be in the middle of, and `gir` is not.
    if (row_belongs_to_chinese && input.size() < kLengthAllowingPrefixes) {
        if (!typed_a_whole_word) return {};
        const bool common_enough = std::any_of(matches.begin(), matches.end(),
            [&](const EnglishCandidate& candidate) {
                return same_word_ignoring_case(candidate.word, input) &&
                       is_common_word(candidate);
            });
        if (!common_enough) return {};
    }

    // Obscure words are reachable by finishing them, not by starting them.
    // `bucolic` is offered; `buco` and `bucoli` are not, though all three lead
    // to the same word. The query above already applied this floor, but a word
    // typed out in full is exempt from it and has to be re-admitted here.
    matches.erase(
        std::remove_if(matches.begin(), matches.end(),
            [&](const EnglishCandidate& candidate) {
                if (same_word_ignoring_case(candidate.word, input)) return false;
                if (candidate.user_entry || candidate.learning_count > 0U) return false;
                return candidate.base_weight < completion_floor;
            }),
        matches.end());
    if (matches.empty()) return {};

    const std::size_t position = english_start_position(
        chinese, input.size(), typed_a_whole_word, settings.double_pinyin);
    if (position == 0U) return {};

    // A whole short word stands on its own. Offering catch and cathedral
    // beside cat would spend three slots guessing which longer word was meant,
    // in a row that belongs to Chinese.
    if (typed_a_whole_word && input.size() < kLengthForFirstSlot) {
        matches.erase(
            std::remove_if(matches.begin(), matches.end(),
                [&](const EnglishCandidate& candidate) {
                    return !same_word_ignoring_case(candidate.word, input);
                }),
            matches.end());
    }

    // Back down to what was asked for. The query above deliberately looked
    // further so the obscure prefixes could be dropped without leaving the
    // row short; without this the row would carry every survivor.
    //
    // The limit exists to stop English crowding a row that belongs to Chinese.
    // Where the input decodes to no Chinese at all -- `preview`, `belie`,
    // `pallad` -- there is nothing to crowd, and holding to two leaves a row
    // that is mostly empty while the word being looked for sits just past the
    // cut. So the cap only applies when there is Chinese to protect.
    const std::size_t limit = chinese.has_candidates
        ? settings.max_items
        : (std::max)(settings.max_items, kItemsWithNoChinese);
    if (matches.size() > limit) {
        matches.resize(limit);
    }

    EnglishCompletionPlan plan;
    plan.start_position = position;
    plan.words.reserve(matches.size());
    for (const auto& candidate : matches) {
        plan.words.push_back(apply_input_case(input, candidate.word));
    }
    return plan;
}

}  // namespace piinput
