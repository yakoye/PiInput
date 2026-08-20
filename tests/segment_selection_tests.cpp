#include "piinput/engine.h"
#include "piinput/segment_selection.h"
#include "piinput/session.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;

[[nodiscard]] std::size_t utf8_length(const std::string& text) {
    std::size_t count = 0U;
    for (const unsigned char ch : text) {
        if ((ch & 0xC0U) != 0x80U) ++count;
    }
    return count;
}

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_staging_undo_and_single_final_text() {
    piinput::SegmentSelection selection;
    selection.begin({{"huang", "he", "ru", "hai", "liu"}, {}, "huang'he'ru'hai'liu"});
    check(selection.stage("黄河", "huang'he", 2U), "first phrase can be staged");
    check(selection.staged_text() == "黄河", "selected prefix remains staged");
    check(selection.remaining_pinyin() == "ru'hai'liu", "only unresolved suffix remains");
    check(selection.stage("入海", "ru'hai", 2U), "second phrase can be staged");
    check(selection.stage("流", "liu", 1U), "last character can be staged");
    check(selection.complete(), "all original syllables are resolved");
    check(selection.finish() == "黄河入海流", "all staged segments form one final string");
    check(selection.undo(), "last segment can be undone");
    check(!selection.complete() && selection.remaining_pinyin() == "liu",
        "undo restores the last unresolved syllable");
}

void test_engine_segment_query_never_fabricates_the_remaining_sentence() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-segment-query.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "黄\thuang\t900\n"
               << "黄河\thuang'he\t1800\n"
               << "河\the\t900\n"
               << "如\tru\t900\n"
               << "入\tru\t850\n"
               << "海\thai\t900\n"
               << "流\tliu\t900\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const piinput::ParsedComposition parsed{
        {"huang", "he", "ru", "hai", "liu"}, {}, "huang'he'ru'hai'liu"};
    const auto flypy = engine.parse_composition("hlheruhdlq", "flypy");
    check(flypy.has_value() && flypy->canonical == "huang'he'ru'hai'liu",
        "Xiaohe long input resolves to the same segmented canonical pinyin");
    const auto first = engine.query_segment(parsed, 0U, 20U);
    check(!first.empty() && first.front().word == "黄河" &&
            first.front().consumed_syllables == 2U,
        "segment query prefers the longest exact phrase at the current offset");
    check(std::none_of(first.begin(), first.end(), [](const auto& candidate) {
        return candidate.word == "黄河如海流";
    }), "segment query does not append guesses for unresolved input");
    const auto third = engine.query_segment(parsed, 2U, 20U);
    check(!third.empty() && (third.front().word == "如" || third.front().word == "入"),
        "segment query continues exactly at the unresolved syllable offset");
    std::filesystem::remove(path);
}

void test_session_stages_segments_and_commits_only_when_complete() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-segment-session.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "黄\thuang\t900\n"
               << "黄河\thuang'he\t1800\n"
               << "河\the\t900\n"
               << "入\tru\t950\n"
               << "如\tru\t900\n"
               << "海\thai\t900\n"
               << "流\tliu\t900\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::ImeSession session(engine, "full", 30U);
    session.set_input("huangheruhailiu");
    check(session.enter_segment_selection(), "session can enter segmented selection");
    check(session.snapshot().view_mode == piinput::CandidateViewMode::segment_selection,
        "session snapshot exposes segmented view mode");
    check(!session.snapshot().candidates.empty() &&
            session.snapshot().candidates.front().candidate.word == "黄河",
        "session exposes only candidates for the first unresolved segment");

    const auto stale_id = session.snapshot().candidates.front().id;
    auto staged = session.stage_candidate(stale_id);
    check(staged.accepted && !staged.commit_text.has_value(),
        "selecting a non-final segment does not request application commit");
    check(session.snapshot().staged_text == "黄河" &&
            session.snapshot().remaining_pinyin == "ru'hai'liu",
        "staged text remains in composition while the suffix stays unresolved");
    check(!session.stage_candidate(stale_id).accepted,
        "old generation candidate IDs are rejected after staging");

    const auto ru = session.snapshot().candidates.front().id;
    staged = session.stage_candidate(ru);
    check(staged.accepted && !staged.commit_text.has_value(), "second segment remains staged");
    check(session.undo_segment(), "Backspace-style undo restores the preceding segment boundary");
    check(session.snapshot().staged_text == "黄河", "undo removes only the latest segment");
    staged = session.stage_candidate(session.snapshot().candidates.front().id);
    check(staged.accepted, "restored segment can be selected again");
    staged = session.stage_candidate(session.snapshot().candidates.front().id);
    check(staged.accepted && !staged.commit_text.has_value(), "penultimate segment remains staged");
    staged = session.stage_candidate(session.snapshot().candidates.front().id);
    check(staged.accepted && staged.commit_text == std::optional<std::string>{"黄河入海流"},
        "last segment returns one final commit string");
    check(session.snapshot().input.empty(), "session clears only after final staged commit is prepared");
    std::filesystem::remove(path);
}

void test_unfinished_trailing_syllable_can_be_completed_and_committed() {
    piinput::SegmentSelection selection;
    selection.begin({{"wo", "xian", "zai"}, "b", "wo'xian'zai'b"});
    check(selection.stage("我现在", "wo'xian'zai", 3U), "the parsed prefix can be staged");
    check(!selection.complete(), "an unfinished trailing syllable is not resolved yet");
    check(selection.remaining_pinyin() == "b", "the unfinished syllable stays visible");
    check(!selection.stage("不了", "bu'le", 2U),
        "completing the trailing syllable cannot consume more than one syllable");
    check(selection.stage("不", "bu", 1U), "the trailing syllable can be completed");
    check(selection.complete(), "completing the trailing syllable resolves the composition");
    check(selection.finish() == "我现在不", "the completed trailing syllable joins one commit");
    check(!selection.stage("把", "ba", 1U), "the trailing syllable is only consumed once");
    check(selection.undo(), "completing the trailing syllable can be undone");
    check(!selection.complete() && selection.remaining_pinyin() == "b",
        "undo restores the unfinished trailing syllable");
}

void test_session_commits_a_prefix_word_plus_a_completed_trailing_syllable() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-trailing-segment.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "我\two\t900\n"
               << "我现在\two'xian'zai\t1800\n"
               << "现在\txian'zai\t1500\n"
               << "不\tbu\t950\n"
               << "把\tba\t900\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);

    const piinput::ParsedComposition parsed{{"wo", "xian", "zai"}, "b", "wo'xian'zai'b"};
    const auto trailing = engine.query_segment(parsed, 3U, 20U);
    check(!trailing.empty(), "the unfinished trailing syllable still offers candidates");
    check(std::all_of(trailing.begin(), trailing.end(), [](const auto& candidate) {
        return candidate.consumed_syllables == 1U;
    }), "completing the trailing syllable never spans more than one syllable");
    check(std::any_of(trailing.begin(), trailing.end(), [](const auto& candidate) {
        return candidate.word == "不";
    }), "the trailing prefix resolves through dictionary completion");

    // The user's own report: Xiaohe "woxmzd" is 我现在 and the final "b" is
    // half of "bu".
    piinput::ImeSession session(engine, "flypy", 30U);
    session.set_input("woxmzdb");
    const auto phrase = std::find_if(
        session.snapshot().candidates.begin(), session.snapshot().candidates.end(),
        [](const auto& item) { return item.candidate.word == "我现在"; });
    check(phrase != session.snapshot().candidates.end(),
        "the resolved prefix is offered while the last syllable is unfinished");
    auto staged = session.choose(phrase->id);
    check(staged.accepted && !staged.commit_text.has_value(),
        "choosing the prefix keeps composing instead of committing early");
    check(session.snapshot().staged_text == "我现在" &&
            session.snapshot().remaining_pinyin == "b",
        "only the unfinished syllable is left to resolve");
    check(!session.snapshot().candidates.empty(),
        "the unfinished syllable is not a dead end");
    const auto completion = std::find_if(
        session.snapshot().candidates.begin(), session.snapshot().candidates.end(),
        [](const auto& item) { return item.candidate.word == "不"; });
    check(completion != session.snapshot().candidates.end(),
        "the unfinished syllable offers its dictionary completions");
    staged = session.stage_candidate(completion->id);
    check(staged.accepted && staged.commit_text == std::optional<std::string>{"我现在不"},
        "completing the last syllable commits the whole composition at once");
    std::filesystem::remove(path);
}

void test_completion_never_spans_past_the_syllable_being_typed() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-completion-span.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "中\tzhong\t900\n"
               << "中国\tzhong'guo\t500\n"
               << "中国人民银行\tzhong'guo'ren'min'yin'hang\t9000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto candidates = engine.query("zhongg", "full", 20U);
    check(std::any_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
        return candidate.word == "中国";
    }), "completing the syllable being typed is still offered");
    check(std::none_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
        return candidate.word == "中国人民银行";
    }), "completion does not guess syllables the user has not typed towards");
    std::filesystem::remove(path);
}

void test_full_coverage_joins_real_words() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-full-coverage.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "备份\tbei'fen\t900\n"
               << "用户\tyong'hu\t800\n"
               << "拥护\tyong'hu\t700\n"
               << "备\tbei\t5000\n"
               << "份\tfen\t5000\n"
               << "用\tyong\t5000\n"
               << "户\thu\t5000\n"
               << "甲\tjia\t5000\n"
               << "乙\tyi\t5000\n"
               << "丙\tbing\t5000\n"
               << "丁\tding\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);

    const auto joined = engine.query("beifenyonghu", "full", 20U);
    check(!joined.empty() && joined.front().word == "备份用户",
        "two real words are joined to cover every typed syllable");
    check(!joined.empty() && joined.front().consumed_syllables == 4U &&
            joined.front().word_count == 2U &&
            joined.front().evidence.covers_all_input,
        "the joined candidate reports full coverage and its component count");
    // 备份 alone covers half the input; the join covering all of it wins.
    const auto prefix_only = std::find_if(joined.begin(), joined.end(),
        [](const auto& candidate) { return candidate.word == "备份"; });
    check(prefix_only != joined.end() && prefix_only > joined.begin(),
        "a word covering only the first half ranks below the full join");
    // Sharing the same weak first word, the stronger second word decides.
    const auto users = std::find_if(joined.begin(), joined.end(),
        [](const auto& candidate) { return candidate.word == "备份用户"; });
    const auto support = std::find_if(joined.begin(), joined.end(),
        [](const auto& candidate) { return candidate.word == "备份拥护"; });
    check(users != joined.end() && (support == joined.end() || users < support),
        "the join built from the more common second word ranks first");

    // Only single characters could span this input, so nothing is assembled.
    const auto characters = engine.query("jiayibingding", "full", 20U);
    check(std::none_of(characters.begin(), characters.end(), [](const auto& candidate) {
        return utf8_length(candidate.word) > 1U;
    }), "single characters are never chained into a fabricated phrase");

    std::filesystem::remove(path);
}

void test_a_real_entry_outranks_a_join_over_the_same_syllables() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-join-vs-entry.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "中华人民\tzhong'hua'ren'min\t500\n"
               << "种花\tzhong'hua\t9000\n"
               << "人民\tren'min\t9000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto candidates = engine.query("zhonghuarenmin", "full", 20U);
    check(!candidates.empty() && candidates.front().word == "中华人民",
        "one dictionary entry covering the input beats a join, even at lower weight");
    check(!candidates.empty() && candidates.front().word_count == 1U,
        "the winning candidate is a single entry, not an assembled one");
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_staging_undo_and_single_final_text();
    test_engine_segment_query_never_fabricates_the_remaining_sentence();
    test_session_stages_segments_and_commits_only_when_complete();
    test_unfinished_trailing_syllable_can_be_completed_and_committed();
    test_session_commits_a_prefix_word_plus_a_completed_trailing_syllable();
    test_completion_never_spans_past_the_syllable_being_typed();
    test_full_coverage_joins_real_words();
    test_a_real_entry_outranks_a_join_over_the_same_syllables();
    if (failures != 0) {
        std::cerr << failures << " segment selection test(s) failed\n";
        return 1;
    }
    std::cout << "All segment selection tests passed\n";
    return 0;
}
