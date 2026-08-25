#include "piinput/engine.h"
#include "piinput/english_lexicon.h"
#include "piinput/datetime_candidates.h"
#include "piinput/host_session.h"
#include "piinput/settings.h"
#include "piinput/symbols.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path write_chinese_lexicon() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-host-session.tsv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "word\tpinyin\tweight\n"
           << "我\two\t5000\n"
           << "窝\two\t4900\n"
           << "握\two\t4800\n"
           << "沃\two\t4700\n"
           << "卧\two\t4600\n"
           << "中国\tzhong'guo\t4900\n"
           << "中\tzhong\t3000\n"
           << "国\tguo\t3000\n"
           << "明天\tming'tian\t4800\n"
           << "名天\tming'tian\t4700\n"
           << "命天\tming'tian\t4600\n"
           << "明田\tming'tian\t4500\n"
           << "名田\tming'tian\t4400\n"
           << "铭天\tming'tian\t4300\n"
           << "鸣天\tming'tian\t4200\n"
           << "命题\tming'ti\t3000\n"
           << "明\tming\t4200\n"
           << "天\ttian\t4100\n"
           << "非常\tfei'chang\t9000\n"
           << "飞常\tfei'chang\t8900\n"
           << "费常\tfei'chang\t8800\n"
           << "非长\tfei'chang\t8700\n"
           << "飞长\tfei'chang\t8600\n"
           << "费长\tfei'chang\t8500\n"
           << "非场\tfei'chang\t8400\n"
           << "飞场\tfei'chang\t8300\n"
           << "费场\tfei'chang\t8200\n"
           << "非\tfei\t5000\n"
           << "飞\tfei\t4900\n"
           << "费\tfei\t4800\n"
           << "常\tchang\t5000\n"
           << "长\tchang\t4900\n"
           << "场\tchang\t4800\n"
           << "快\tkuai\t5000\n"
           << "块\tkuai\t4900\n"
           << "筷\tkuai\t4800\n"
           << "你\tni\t6000\n"
           << "吧\tba\t9000\n"
           << "把\tba\t4000\n"
           << "我的\two'de\t6500\n"
           << "图标\ttu'biao\t6500\n"
           << "候选框\thou'xuan'kuang\t4200\n"
           << "候选矿\thou'xuan'kuang\t9000\n"
           << "开\tkai\t5000\n"
           << "凯\tkai\t4900\n"
           << "悟\twu\t5000\n"
           << "误\twu\t4900\n"
           // One real two-syllable word whose homophone row is otherwise filled
           // by decoded single-character pairs. This is the reported kady case:
           // only 卡顿 is a trusted dictionary hit, yet a full row is on screen.
           << "卡顿\tka'dun\t4200\n"
           << "卡\tka\t5000\n"
           << "喀\tka\t4100\n"
           << "咖\tka\t4000\n"
           << "顿\tdun\t4000\n"
           << "吨\tdun\t3900\n"
           << "炖\tdun\t3800\n"
           << "蹲\tdun\t3700\n"
           << "敦\tdun\t3600\n";
    return path;
}

void type(piinput::HostSession& session, const std::string& text);

void test_space_and_digits_are_resolved_by_current_host_state() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "wo");
    const auto first = session.apply({.kind = piinput::HostKeyKind::space});
    check(first.accepted && first.action == piinput::HostAction::commit && first.text == "我",
        "Host resolves Space against its current candidate generation");

    type(session, "wo");
    const auto expanded = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
    check(expanded.snapshot.view.active_row == 1U && expanded.snapshot.candidates.size() >= 4U,
        "fixture exposes a deterministic second candidate row");
    const std::string expected = expanded.snapshot.candidates[3U].text;
    const auto digit = session.apply({
        .kind = piinput::HostKeyKind::select_digit,
        .character = '2',
    });
    check(digit.accepted && digit.action == piinput::HostAction::commit && digit.text == expected,
        "Host resolves digit selection against the active row without a Shim snapshot");
    std::filesystem::remove(lexicon_path);
}

void test_configured_default_input_language_applies_to_new_sessions() {
    piinput::Engine engine;
    auto english_default = piinput::default_settings();
    english_default.general.default_language = piinput::DefaultInputLanguage::english;
    piinput::HostSession english(engine, nullptr, english_default, "full");
    check(english.snapshot().mode == piinput::HostInputMode::english,
        "a new session starts in configured English mode");
    const auto direct = english.apply({
        .kind = piinput::HostKeyKind::text,
        .character = 'b',
    });
    check(direct.action == piinput::HostAction::commit && direct.text == "b",
        "default English mode enters direct English when candidates are disabled");

    auto chinese_default = piinput::default_settings();
    chinese_default.general.default_language = piinput::DefaultInputLanguage::chinese;
    piinput::HostSession chinese(engine, nullptr, chinese_default, "full");
    check(chinese.snapshot().mode == piinput::HostInputMode::chinese,
        "a new session starts in configured Chinese mode");
}

void test_punctuation_is_transformed_and_committed_by_host() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.punctuation = piinput::PunctuationMode::chinese;
    piinput::HostSession session(engine, nullptr, settings, "full");

    const auto idle = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = '.',
    });
    check(idle.accepted && idle.action == piinput::HostAction::commit && idle.text == "。",
        "idle Chinese punctuation is committed by Host");

    const auto numeric_dot = session.apply({
        .kind = piinput::HostKeyKind::literal_punctuation,
        .character = '.',
    });
    check(numeric_dot.accepted && numeric_dot.action == piinput::HostAction::commit &&
            numeric_dot.text == ".",
        "a decimal/list dot after a direct digit remains ASCII in Chinese mode");

    const auto fraction_slash = session.apply({
        .kind = piinput::HostKeyKind::literal_punctuation,
        .character = '/',
    });
    check(fraction_slash.accepted && fraction_slash.action == piinput::HostAction::commit &&
            fraction_slash.text == "/",
        "the physical slash key remains ASCII in Chinese mode for fractions");

    const auto enumeration_comma = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = '\\',
    });
    check(enumeration_comma.accepted &&
            enumeration_comma.action == piinput::HostAction::commit &&
            enumeration_comma.text == "、",
        "the physical backslash key remains the Chinese enumeration comma");

    type(session, "wo");
    const auto composed = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = ',',
    });
    check(composed.accepted && composed.action == piinput::HostAction::commit &&
            composed.text == "我，",
        "punctuation commits the current candidate and Chinese symbol atomically");
    std::filesystem::remove(lexicon_path);
}

std::filesystem::path write_english_lexicon() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-host-english.tsv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "right\t5000\t1\n"
           << "really\t4900\t1\n"
           << "remember\t4800\t1\n";
    return path;
}

std::filesystem::path write_symbol_lexicon() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-host-symbols.tsv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "symbol\tcategory\tname\taliases\n"
           << "℃\tunit\t摄氏度\tsheshidu|celsius\n"
           << "©\tmark\t版权\tbanquan|copyright\n";
    return path;
}

piinput::HostKeyEvent text_key(const char value) {
    return {.kind = piinput::HostKeyKind::text, .character = value};
}

void type(piinput::HostSession& session, const std::string& text) {
    for (const char value : text) {
        const auto reply = session.apply(text_key(value));
        check(reply.accepted, "typed character is accepted");
    }
}

void test_chinese_session_generations_stale_selection_and_view_reset() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "wo");
    const auto initial = session.snapshot();
    check(initial.raw == "wo" && initial.caret == 2U, "host owns Chinese raw input and caret");
    check(!initial.candidates.empty() && initial.candidates.front().text == "我",
        "full pinyin query is exposed by the host snapshot");
    check(initial.view.visible_rows == 1U && !initial.view.expanded,
        "new input snapshots start with one candidate row");
    const auto stale_id = initial.candidates.front().id;

    const auto expanded = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
    check(expanded.accepted && expanded.snapshot.view.expanded,
        "formal next-row action expands the candidate view");
    const auto edited = session.apply(text_key('m'));
    check(edited.snapshot.view.visible_rows == 1U && !edited.snapshot.view.expanded,
        "editing creates a new generation and collapses the candidate view");
    const auto stale = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = stale_id,
    });
    check(!stale.accepted && stale.action == piinput::HostAction::none,
        "candidate ids from an older generation are rejected");

    (void)session.apply({.kind = piinput::HostKeyKind::escape});
    type(session, "wo");
    const auto selected_id = session.snapshot().candidates.front().id;
    const auto committed = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = selected_id,
    });
    check(committed.accepted && committed.action == piinput::HostAction::commit &&
            committed.text == "我" && committed.snapshot.raw.empty(),
        "current-generation selection commits once and clears the host session");

    std::filesystem::remove(lexicon_path);
}

void test_xiaohe_and_restart_resume_recompute_candidates() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    const auto settings = piinput::default_settings();
    piinput::HostSession original(engine, nullptr, settings, "flypy");
    type(original, "vsgo");
    check(!original.snapshot().candidates.empty() &&
            original.snapshot().candidates.front().text == "中国",
        "Xiaohe input uses the same host session abstraction");

    const auto resume = original.resume_state();
    piinput::HostSession restarted(engine, nullptr, settings, "flypy");
    restarted.restore(resume);
    check(restarted.snapshot().raw == "vsgo" &&
            !restarted.snapshot().candidates.empty() &&
            restarted.snapshot().candidates.front().text == "中国",
        "host restart restores raw state and recomputes candidates");
    check(restarted.snapshot().generation > resume.generation,
        "recomputed restart state advances generation instead of trusting old candidates");

    std::filesystem::remove(lexicon_path);
}

void test_repeated_equals_enters_segment_selection_and_stages_until_final_commit() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 2U;
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "mingtian");
    const auto normal = session.snapshot();
    const auto first_character = std::find_if(
        normal.candidates.begin(), normal.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "明"; });
    check(first_character != normal.candidates.end(),
        "single-character fallback remains browseable after all real words");

    auto first = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = first_character->id,
    });
    check(first.accepted && first.action == piinput::HostAction::update &&
            first.snapshot.view.mode == piinput::HostCandidateMode::segment_selection &&
            first.snapshot.composition_text.starts_with("明"),
        "choosing a partial normal candidate stages it and preserves the suffix");
    const auto remaining = first.snapshot;
    const auto final = std::find_if(remaining.candidates.begin(), remaining.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "天"; });
    check(final != remaining.candidates.end(), "the remaining syllable exposes 天");
    const auto committed = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = final->id,
    });
    check(committed.accepted && committed.action == piinput::HostAction::commit &&
            committed.text == "明天",
        "two staged characters commit the completed phrase exactly once");
    std::filesystem::remove(lexicon_path);
}

void test_real_words_fill_multiple_rows_before_single_characters() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 30U;
    piinput::HostSession session(engine, nullptr, settings, "flypy");

    type(session, "fwihkk");
    const auto initial = session.snapshot();
    // 非常快 extends the real word 非常 with the one syllable left over, which
    // is what the user typed and what nothing in the dictionary spans on its
    // own. A chain with no real word under it -- 非/常/快 assembled character
    // by character -- is still refused; the rule is that a multi-character
    // entry must anchor the join and at most one character may close this
    // three-syllable input.
    check(initial.candidates.size() > 11U && initial.candidates.front().text == "非常快",
        "the join covering every typed syllable begins the normal candidate list");
    const auto longest_prefix = std::find_if(initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "非常"; });
    const auto first_character = std::find_if(initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "非"; });
    check(longest_prefix != initial.candidates.end() &&
            first_character != initial.candidates.end() && longest_prefix < first_character,
        "the longest real prefix follows complete joins and precedes single characters");
    const auto first_character_index = first_character == initial.candidates.end()
        ? std::size_t{0U}
        : static_cast<std::size_t>(std::distance(initial.candidates.begin(), first_character));
    for (std::size_t index = 0U; index < first_character_index; ++index) {
        check(initial.candidates[index].text.size() > std::string("非").size(),
            "all real multi-character words remain ahead of single characters");
    }
    const auto expanded = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
    check(expanded.accepted && expanded.snapshot.view.active_row == 1U &&
            expanded.snapshot.view.mode == piinput::HostCandidateMode::normal,
        "the second row continues browsing real words in normal mode");
    std::filesystem::remove(lexicon_path);
}

void test_mktm_keeps_all_words_before_ming_character_selection() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 30U;
    piinput::HostSession session(engine, nullptr, settings, "flypy");

    type(session, "mktm");
    check(!session.snapshot().candidates.empty() &&
            session.snapshot().candidates.front().text == "明天",
        "mktm keeps the real dictionary phrase 明天 as the collapsed first choice");
    const auto normal = session.snapshot();
    const auto first_character = std::find_if(
        normal.candidates.begin(), normal.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "明"; });
    check(first_character != normal.candidates.end() &&
            std::distance(normal.candidates.begin(), first_character) == 7,
        "all seven matching words precede the first unresolved character");
    const auto staged = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = first_character->id,
    });
    check(staged.accepted && staged.action == piinput::HostAction::update &&
            staged.snapshot.composition_text.starts_with("明"),
        "selecting 明 directly preserves the remaining tian syllable");
    std::filesystem::remove(lexicon_path);
}

// Reported case: kady shows a full row of real words but only the first is a
// trusted dictionary hit. Pressing '=' used to replace that row with the
// per-syllable choices, so the words the user could still pick vanished.
void test_dash_at_the_top_keeps_the_candidate_row_on_screen() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 3U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    // Arrow-up carries no character and must stay a pure navigation key even
    // when there is nothing to navigate.
    const auto arrow = session.apply({.kind = piinput::HostKeyKind::previous_row});
    check(!arrow.accepted && arrow.action == piinput::HostAction::none,
        "arrow up on an empty session stays unhandled");

    type(session, "wo");
    check(!session.snapshot().candidates.empty(), "wo produces candidates");
    const std::string expected_word = session.snapshot().candidates.front().text;

    // Paging up with nowhere to go leaves the row exactly where it is. It used
    // to commit the selected word and insert a literal dash, which cleared the
    // candidates off the screen while the user was still reading them.
    const auto dash = session.apply(
        {.kind = piinput::HostKeyKind::previous_row, .character = '-'});
    check(dash.accepted, "- 应被吃掉，不能落进正文");
    check(dash.action != piinput::HostAction::commit, "- 不应上屏");
    check(dash.text.empty(), "- 不应产生任何文字");
    check(session.snapshot().raw == "wo", "候选和输入内容都应保留");
    check(session.snapshot().candidates.front().text == expected_word,
        "那一排候选仍在原处");
    (void)expected_word;
}

// Choosing the date or time entry opens the formats as a list instead of
// committing. They cannot all sit in the candidate row -- six or seven entries,
// several twenty characters wide -- so one labelled entry stands for them.
// Enter commits the letters as typed while the selection is untouched -- that
// is what it is for. Once the user has moved the selection they have chosen
// something, and Enter takes that rather than discarding it. Inside the date
// and time list there is no raw text worth committing, so it always does.
void test_enter_commits_the_selection_once_it_has_been_moved() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "wo");
    const auto untouched = session.apply({.kind = piinput::HostKeyKind::enter});
    check(untouched.accepted && untouched.action == piinput::HostAction::commit,
        "未移动选择时回车仍上屏");
    check(untouched.text == "wo", "未移动选择时回车上屏的是字母本身");

    type(session, "wo");
    const auto listed = session.snapshot();
    check(listed.candidates.size() >= 2U, "wo 应有多个候选");
    const std::string second = listed.candidates[1].text;
    (void)session.apply({.kind = piinput::HostKeyKind::next_candidate});
    const auto moved = session.apply({.kind = piinput::HostKeyKind::enter});
    check(moved.accepted && moved.action == piinput::HostAction::commit,
        "移动过选择后回车应上屏");
    check(moved.text == second, "上屏的应是选中的那个候选，而不是字母");

    std::filesystem::remove(lexicon_path);
}

void test_datetime_entry_opens_a_second_level_list() {
    piinput::Engine engine;
    const auto lexicon_path = std::filesystem::temp_directory_path() /
        "piinput-datetime-menu.tsv";
    {
        std::ofstream output(lexicon_path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n日期\tri'qi\t9000\n日前\tri'qian\t100\n"
               << "时间\tshi'jian\t9000\n事件\tshi'jian\t100\n";
    }
    engine.load_lexicon(lexicon_path);
    std::tm fixed{};
    fixed.tm_year = 2026 - 1900;
    fixed.tm_mon = 8 - 1;
    fixed.tm_mday = 21;
    engine.set_clock([fixed] { return fixed; });

    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    piinput::HostSession session(engine, nullptr, settings, "full");
    type(session, "riqi");

    const auto before = session.snapshot();
    check(before.candidates.size() >= 2U, "riqi 应有词典结果和日期入口");
    check(before.candidates[0].text == "日期", "词典首选仍排第一");
    // Labelled, not showing a format: a bare date there reads as the answer and
    // gives no sign that more spellings are behind it.
    check(before.candidates[1].text == piinput::datetime_group_label(true),
        "第二位应是带标记的入口，而不是某一种格式");

    const auto opened = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = before.candidates[1].id});
    check(opened.accepted && opened.action == piinput::HostAction::update,
        "选中入口应展开列表而不是上屏");
    check(opened.text.empty(), "展开列表不应提交任何文字");

    const auto listed = session.snapshot();
    const std::vector<std::string> expected{
        "2026年8月21日", "2026-08-21", "2026.08.21", "2026/08/21", "20260821",
        "二〇二六年八月二十一日", "丙午[马]年七月初九"};
    check(listed.candidates.size() == expected.size(), "列表应含全部日期格式");
    // Read down the screen, not across. Whole timestamps side by side neither
    // fit in a row nor compare against each other.
    check(listed.view.items_per_row == 1U, "展开的列表应是竖排的一列");
    check(listed.view.expanded, "展开的列表应直接可见，不必再翻页");
    check(listed.view.visible_rows >= expected.size(),
        "所有格式应一次显示完，不需要滚动");
    for (std::size_t index = 0; index < expected.size() && index < listed.candidates.size();
         ++index) {
        check(listed.candidates[index].text == expected[index], "格式顺序应固定");
    }

    const auto committed = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = listed.candidates[5].id});
    check(committed.accepted && committed.action == piinput::HostAction::commit,
        "从列表中选一项应上屏");
    check(committed.text == "二〇二六年八月二十一日", "上屏的应是选中的那一项");
    check(session.snapshot().raw.empty(), "上屏之后输入应清空");

    // Enter inside the list takes what is highlighted, first entry included.
    type(session, "riqi");
    const auto entry = session.snapshot();
    (void)session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = entry.candidates[1].id});
    const auto first_of_list = session.snapshot().candidates.front().text;
    const auto entered = session.apply({.kind = piinput::HostKeyKind::enter});
    check(entered.accepted && entered.action == piinput::HostAction::commit,
        "列表中回车应上屏");
    check(entered.text == first_of_list,
        "列表第一项也应能用回车上屏，而不是上屏拼音字母");

    // Escape backs out of the list to the words, keeping the composition.
    type(session, "riqi");
    const auto reopened = session.snapshot();
    (void)session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = reopened.candidates[1].id});
    const auto escaped = session.apply({.kind = piinput::HostKeyKind::escape});
    check(escaped.accepted, "列表中按 Esc 应被接受");
    check(session.snapshot().raw == "riqi", "Esc 只退出列表，不取消输入");
    check(session.snapshot().candidates.front().text == "日期",
        "退出列表后回到词典候选");

    // Every requested full-pinyin alias must reach the second-level list and
    // commit a real current value, not merely show a decorative entry.
    struct ShortcutCase {
        const char* input;
        bool date;
        const char* first_format;
    };
    for (const ShortcutCase shortcut : {
             ShortcutCase{"sj", false, "00:00:00"},
             ShortcutCase{"shij", false, "00:00:00"},
             ShortcutCase{"shijian", false, "00:00:00"},
             ShortcutCase{"riq", true, "2026年8月21日"},
             ShortcutCase{"riqi", true, "2026年8月21日"}}) {
        piinput::HostSession alias_session(engine, nullptr, settings, "full");
        type(alias_session, shortcut.input);
        const auto aliases = alias_session.snapshot();
        const std::string expected_label = piinput::datetime_group_label(shortcut.date);
        const auto alias_entry = std::find_if(aliases.candidates.begin(), aliases.candidates.end(),
            [&](const piinput::HostCandidate& candidate) {
                return candidate.text == expected_label;
            });
        const std::string entry_message =
            std::string("全拼快捷码应显示日期时间入口：") + shortcut.input;
        check(alias_entry != aliases.candidates.end(), entry_message.c_str());
        if (alias_entry == aliases.candidates.end()) continue;
        const auto opened_alias = alias_session.apply({
            .kind = piinput::HostKeyKind::select_candidate,
            .candidate_id = alias_entry->id});
        const std::string open_message =
            std::string("全拼快捷码应能展开格式列表：") + shortcut.input;
        check(opened_alias.accepted && opened_alias.action == piinput::HostAction::update,
            open_message.c_str());
        const auto formats = alias_session.snapshot();
        const std::string format_message =
            std::string("全拼快捷码的第一种格式应正确：") + shortcut.input;
        check(!formats.candidates.empty() &&
                formats.candidates.front().text == shortcut.first_format,
            format_message.c_str());
        if (formats.candidates.empty()) continue;
        const auto committed_alias = alias_session.apply({
            .kind = piinput::HostKeyKind::select_candidate,
            .candidate_id = formats.candidates.front().id});
        const std::string commit_message =
            std::string("全拼快捷码应能上屏实际日期时间：") + shortcut.input;
        check(committed_alias.accepted && committed_alias.action == piinput::HostAction::commit &&
                committed_alias.text == shortcut.first_format,
            commit_message.c_str());
    }

    std::filesystem::remove(lexicon_path);
}

// Holding = must reach the bottom and stay there. It used to appear to loop:
// at the last row the per-syllable view was entered, that entry reported success
// on a list made only of the ordinary candidates it had kept, and the generation
// change that followed folded the rows and jumped back to the top -- with the
// same candidates underneath, so the whole list looked like it started over.
void test_equals_reaches_the_bottom_and_stays_there() {
    // Built so the per-syllable step has nothing of its own to offer: every
    // character it could suggest is already among the ordinary candidates, so
    // deduplication leaves it empty. That is the shape that made entering the
    // view report success on a list of nothing but the candidates it kept.
    const auto lexicon_path = std::filesystem::temp_directory_path() /
        "piinput-equals-bottom.tsv";
    {
        std::ofstream output(lexicon_path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        output << "你好\tni'hao\t9000\n";
        output << "你\tni\t8000\n拟\tni\t700\n泥\tni\t600\n";
        output << "好\thao\t8000\n号\thao\t700\n毫\thao\t600\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    // More than one visible row, or the snapshot can never report the view as
    // expanded and the assertion below would pass on a technicality.
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 40U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "nihao");
    check(session.snapshot().candidates.size() > 2U, "nihao 应有不止一行候选");

    // Page down until it stops moving, then keep pressing.
    std::size_t settled = 0U;
    for (int step = 0; step < 60; ++step) {
        const auto down = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
        check(down.accepted, "= 应始终被吃掉");
        check(down.text.empty(), "翻页不应上屏任何文字");
        settled = session.snapshot().view.active_row;
    }
    const std::size_t bottom = settled;
    check(bottom != 0U, "应该真的翻下去过，而不是停在第一行");

    for (int step = 0; step < 20; ++step) {
        (void)session.apply({.kind = piinput::HostKeyKind::expand_next_row});
        check(session.snapshot().view.active_row == bottom,
            "到底之后再按 = 必须原地不动，不能回到开头");
        check(session.snapshot().view.expanded,
            "到底之后候选不应被收起");
    }
    check(session.snapshot().raw == "nihao", "一路按到底之后输入内容仍在");

    std::filesystem::remove(lexicon_path);
}

// Paging keys browse candidates and are never typed. Going down stops at the
// last row; going up folds the rows back to the single one they started as.
// Before this, either end let the key through and a literal = or - landed in
// the middle of the composition.
void test_paging_keys_stop_at_the_ends_without_typing_themselves() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 1U;
    settings.candidates.max_items = 40U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "kadun");
    check(session.snapshot().candidates.size() > 2U, "kadun 应有不止一行候选");
    check(!session.snapshot().view.expanded, "初始应是收起的一行");

    // Down as far as it goes.
    for (int step = 0; step < 40; ++step) {
        const auto down = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
        check(down.accepted, "= 应始终被吃掉，不能打进正文");
        check(down.text.empty(), "翻页不应上屏任何文字");
    }
    check(session.snapshot().raw == "kadun", "一路向下之后输入内容仍在");

    // Up as far as it goes, ending folded back to one row.
    for (int step = 0; step < 40; ++step) {
        const auto up = session.apply(
            {.kind = piinput::HostKeyKind::previous_row, .character = '-'});
        if (!up.accepted) break;
        check(up.text.empty() || up.action == piinput::HostAction::commit,
            "向上翻页本身不应上屏");
        if (up.action == piinput::HostAction::commit) break;
    }
    check(!session.snapshot().view.expanded, "一路向上之后应收回成一行");
}

void test_dash_still_pages_the_candidate_rows_when_a_row_exists() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 1U;
    settings.candidates.max_items = 40U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "kadun");
    const auto down = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
    check(down.accepted, "the candidate rows can be expanded");
    const auto up = session.apply(
        {.kind = piinput::HostKeyKind::previous_row, .character = '-'});
    check(up.accepted && up.action == piinput::HostAction::update,
        "the dash still pages back while a row is available");
    check(!up.snapshot.raw.empty(), "paging back keeps the composition alive");
}

void test_kadun_word_then_character_fallback_stages_normally() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 40U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "kadun");
    const auto initial = session.snapshot();
    check(!initial.candidates.empty() && initial.candidates.front().text == "卡顿",
        "kadun keeps the real dictionary word 卡顿 first");
    // Nothing anchors a join here -- 卡 and 吨 are both single characters -- so
    // no join is offered at all. Ranking two characters by their own weights is
    // what once put 卡吨 ahead of the real word.
    check(std::none_of(initial.candidates.begin(), initial.candidates.end(),
            [](const piinput::HostCandidate& candidate) {
                return candidate.text == "卡吨" || candidate.text == "卡炖" ||
                    candidate.text == "卡蹲";
            }),
        "characters are never chained into a candidate without a real word");
    const auto character = std::find_if(
        initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "卡"; });
    check(character != initial.candidates.end() && character != initial.candidates.begin(),
        "single-character fallback follows the real word in normal candidates");
    const auto staged = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = character->id,
    });
    check(staged.accepted && staged.action == piinput::HostAction::update &&
            staged.snapshot.composition_text.starts_with("卡"),
        "selecting 卡 stages it and leaves dun unresolved");
    std::filesystem::remove(lexicon_path);
}

// Choosing a single-character fallback from the normal lexical list stages it
// into the composition and must continue on the syllable it left behind.
void test_staging_a_character_advances_to_the_next_syllable() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 5U;
    settings.candidates.max_items = 40U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "kadun");
    const auto initial = session.snapshot();
    const auto first = std::find_if(initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "卡"; });
    check(first != initial.candidates.end(), "the lexical list offers 卡 after 卡顿");

    const auto staged = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = first->id,
    });
    check(staged.accepted && staged.action == piinput::HostAction::update,
        "staging one character keeps composing instead of committing");
    check(staged.snapshot.view.mode == piinput::HostCandidateMode::segment_selection,
        "staging stays in segment selection");
    check(staged.snapshot.composition_text.starts_with("卡"),
        "the chosen character moves into the composition");
    const std::size_t next_choice = staged.snapshot.view.active_row *
        staged.snapshot.view.items_per_row + staged.snapshot.view.active_column;
    if (!(next_choice < staged.snapshot.candidates.size() &&
            staged.snapshot.candidates[next_choice].text == "顿")) {
        std::cerr << "after staging 卡: row=" << staged.snapshot.view.active_row
                  << " col=" << staged.snapshot.view.active_column << " list:";
        for (const auto& candidate : staged.snapshot.candidates) {
            std::cerr << ' ' << (candidate.text.empty() ? "<pad>" : candidate.text);
        }
        std::cerr << '\n';
    }
    check(next_choice < staged.snapshot.candidates.size() &&
            staged.snapshot.candidates[next_choice].text == "顿",
        "the selection advances to the next unresolved syllable");
    std::filesystem::remove(lexicon_path);
}

void test_normal_word_pages_include_single_character_fallbacks() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 30U;
    piinput::HostSession session(engine, nullptr, settings, "full");

    type(session, "mingtian");
    const auto initial = session.snapshot();
    check(initial.candidates.size() >= 7U,
        "fixture provides several rows of real full-pinyin words");
    const auto character = std::find_if(initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "明"; });
    check(character != initial.candidates.end() &&
            static_cast<std::size_t>(std::distance(initial.candidates.begin(), character)) >= 7U,
        "the first-syllable character follows all real full-pinyin words");

    auto right = session.apply({.kind = piinput::HostKeyKind::next_candidate});
    check(right.snapshot.view.active_column == 1U,
        "precondition selects a nonzero column before paging");
    for (std::size_t expected_row = 1U; expected_row <= 3U; ++expected_row) {
        const auto next = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
        check(next.accepted && next.snapshot.view.mode == piinput::HostCandidateMode::normal &&
                next.snapshot.view.active_row == expected_row &&
                next.snapshot.view.active_column == 0U,
            "each equals press visits the next real-word row at its first candidate");
    }
    check(session.snapshot().view.first_visible_row == 1U,
        "continuous equals scrolls the candidate queue so the oldest row leaves view");
    const auto after_paging = session.snapshot();
    const auto current_character = std::find_if(
        after_paging.candidates.begin(), after_paging.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "明"; });
    check(current_character != after_paging.candidates.end(),
        "paging keeps the single-character fallback in the normal list");
    const auto staged = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = current_character->id,
    });
    check(staged.accepted && staged.action == piinput::HostAction::update &&
            staged.snapshot.composition_text.starts_with("明"),
        "selecting the normal single-character fallback stages it and keeps tian unresolved");
    std::filesystem::remove(lexicon_path);
}

void test_expanded_row_count_is_configurable_and_starts_on_row_two() {
    for (const std::uint32_t visible_rows : {3U, 4U, 5U, 6U}) {
        piinput::Engine engine;
        const auto lexicon_path = write_chinese_lexicon();
        engine.load_lexicon(lexicon_path);
        auto settings = piinput::default_settings();
        settings.candidates.items_per_row = 2U;
        settings.candidates.visible_rows = visible_rows;
        settings.candidates.max_items = 30U;
        piinput::HostSession session(engine, nullptr, settings, "full");
        type(session, "mingtian");
        const auto expanded = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
        check(expanded.accepted && expanded.snapshot.view.active_row == 1U &&
                expanded.snapshot.view.active_column == 0U &&
                expanded.snapshot.view.visible_rows == visible_rows,
            "first equals selects row two and honors the configured expanded row count");
        std::filesystem::remove(lexicon_path);
    }
}

void test_one_successful_segment_composition_creates_a_first_row_phrase() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 30U;
    piinput::HostSession source(engine, nullptr, settings, "full");
    type(source, "kaiwu");
    auto select_text = [&](const std::string& text) {
        const auto current = source.snapshot();
        const auto found = std::find_if(current.candidates.begin(), current.candidates.end(),
            [&](const piinput::HostCandidate& candidate) { return candidate.text == text; });
        check(found != current.candidates.end(), "requested segment character exists");
        return source.apply({
            .kind = piinput::HostKeyKind::select_candidate,
            .candidate_id = found->id,
        });
    };
    const auto first = select_text("开");
    check(first.accepted && first.action == piinput::HostAction::update,
        "choosing the first character keeps the phrase in composition");
    const auto committed = select_text("悟");
    check(committed.accepted && committed.action == piinput::HostAction::commit &&
            committed.text == "开悟",
        "the final character commits the composed phrase exactly once");
    check(source.confirm_commit(committed.snapshot.generation, true),
        "successful TSF confirmation records the newly composed phrase");

    piinput::HostSession next(engine, nullptr, settings, "full");
    type(next, "kaiwu");
    const auto learned = next.snapshot();
    const auto found = std::find_if(learned.candidates.begin(), learned.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "开悟"; });
    check(found != learned.candidates.end() && static_cast<std::size_t>(
            std::distance(learned.candidates.begin(), found)) < settings.candidates.items_per_row,
        "one successful composed entry is remembered in the first row immediately");
    std::filesystem::remove(lexicon_path);
}

void test_partial_word_selection_preserves_remainder_and_promotes_once_confirmed() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.max_items = 40U;
    piinput::HostSession source(engine, nullptr, settings, "full");

    type(source, "feichangkuai");
    const auto initial = source.snapshot();
    const auto word = std::find_if(initial.candidates.begin(), initial.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "飞常"; });
    check(word != initial.candidates.end(),
        "a real prefix word is offered for a longer pinyin composition");
    const auto staged = source.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = word->id,
    });
    check(staged.accepted && staged.action == piinput::HostAction::update &&
            staged.snapshot.composition_text == "飞常kuai" &&
            staged.snapshot.raw == "feichangkuai",
        "choosing a real prefix word stages it and preserves the remaining pinyin");

    const auto suffix = std::find_if(
        staged.snapshot.candidates.begin(), staged.snapshot.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "快"; });
    check(suffix != staged.snapshot.candidates.end(),
        "the unresolved suffix immediately offers its real lexical candidates");
    const auto committed = source.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = suffix->id,
    });
    check(committed.accepted && committed.action == piinput::HostAction::commit &&
            committed.text == "飞常快",
        "the final segment commits the staged phrase exactly once");
    check(source.confirm_commit(committed.snapshot.generation, true),
        "successful application confirmation activates phrase and component learning");

    piinput::HostSession full_query(engine, nullptr, settings, "full");
    type(full_query, "feichangkuai");
    check(!full_query.snapshot().candidates.empty() &&
            full_query.snapshot().candidates.front().text == "飞常快",
        "the confirmed whole phrase becomes the first full-input candidate");

    piinput::HostSession component_query(engine, nullptr, settings, "full");
    type(component_query, "feichang");
    check(!component_query.snapshot().candidates.empty() &&
            component_query.snapshot().candidates.front().text == "飞常",
        "one confirmed selection promotes the chosen real word in its group");
    std::filesystem::remove(lexicon_path);
}

void test_host_candidate_arrows_move_in_all_four_directions() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.visible_rows = 3U;
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");
    type(session, "wo");

    auto right = session.apply({.kind = piinput::HostKeyKind::next_candidate});
    check(right.accepted && right.snapshot.view.active_column == 1U,
        "right arrow moves to the next candidate");
    auto down = session.apply({.kind = piinput::HostKeyKind::expand_next_row});
    check(down.accepted && down.snapshot.view.active_row == 1U &&
            down.snapshot.view.active_column == 0U,
        "down arrow moves to the first candidate in the next row");
    right = session.apply({.kind = piinput::HostKeyKind::next_candidate});
    check(right.accepted && right.snapshot.view.active_column == 1U,
        "right arrow still moves within the newly selected row");
    auto left = session.apply({.kind = piinput::HostKeyKind::previous_candidate});
    check(left.accepted && left.snapshot.view.active_column == 0U,
        "left arrow moves to the previous candidate");
    auto up = session.apply({.kind = piinput::HostKeyKind::previous_row});
    check(up.accepted && up.snapshot.view.active_row == 0U,
        "up arrow returns to the previous candidate row");
    std::filesystem::remove(lexicon_path);
}

void test_host_can_pin_and_suppress_current_generation_candidates() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");
    type(session, "houxuankuang");
    auto current = session.snapshot();
    const auto desired = std::find_if(current.candidates.begin(), current.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "候选框"; });
    check(desired != current.candidates.end(), "fixture exposes the candidate to pin");
    const auto pinned = session.manage_candidate(
        desired->id, piinput::CandidateManagementAction::pin_first);
    check(pinned.accepted && !pinned.snapshot.candidates.empty() &&
            pinned.snapshot.candidates.front().text == "候选框",
        "pinning refreshes the current generation with that candidate first");

    const auto unpinned = session.manage_candidate(
        pinned.snapshot.candidates.front().id,
        piinput::CandidateManagementAction::unpin);
    check(unpinned.accepted && !unpinned.snapshot.candidates.empty() &&
            unpinned.snapshot.candidates.front().text == "候选矿",
        "canceling fixed-first restores learning/base ordering without deleting the word");

    const auto candidate_to_delete = std::find_if(
        unpinned.snapshot.candidates.begin(), unpinned.snapshot.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "候选框"; });
    check(candidate_to_delete != unpinned.snapshot.candidates.end(),
        "unpinned candidate remains available for deletion");
    const std::size_t deleted_index = static_cast<std::size_t>(
        std::distance(unpinned.snapshot.candidates.begin(), candidate_to_delete));
    const std::string follower = deleted_index + 1U < unpinned.snapshot.candidates.size()
        ? unpinned.snapshot.candidates[deleted_index + 1U].text
        : std::string{};
    const auto deleted = session.manage_candidate(
        candidate_to_delete->id,
        piinput::CandidateManagementAction::delete_candidate);
    check(deleted.accepted && std::none_of(
            deleted.snapshot.candidates.begin(), deleted.snapshot.candidates.end(),
            [](const piinput::HostCandidate& candidate) { return candidate.text == "候选框"; }),
        "deleting refreshes the current generation without that exact candidate");
    if (!follower.empty()) {
        check(deleted_index < deleted.snapshot.candidates.size() &&
                deleted.snapshot.candidates[deleted_index].text == follower,
            "deletion shifts the following candidate left instead of leaving a gap");
    }
    const auto stale = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = candidate_to_delete->id,
    });
    check(!stale.accepted, "deletion invalidates the old candidate generation");
    std::filesystem::remove(lexicon_path);
}

void test_host_learns_only_after_confirmed_commit() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.max_items = 20U;
    piinput::HostSession session(engine, nullptr, settings, "full");
    type(session, "wo");
    const auto first = session.snapshot();
    check(!first.candidates.empty(), "confirmation test has a Chinese candidate");
    const auto committed = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = first.candidates.front().id,
    });
    check(committed.accepted && committed.action == piinput::HostAction::commit,
        "candidate selection creates a pending commit");

    const auto before_path = std::filesystem::temp_directory_path() /
        "piinput-host-confirm-before.tsv";
    engine.save_user_model(before_path);
    piinput::UserModel before;
    before.load(before_path);
    check(before.query_exact("wo").empty(),
        "Host does not learn before TSF confirms the edit session");

    check(session.confirm_commit(committed.snapshot.generation, false),
        "failed TSF result consumes the matching pending commit");
    engine.save_user_model(before_path);
    before.load(before_path);
    check(before.query_exact("wo").empty(),
        "failed TSF commit never changes user learning");

    type(session, "wo");
    const auto retry = session.snapshot();
    const auto succeeded = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = retry.candidates.front().id,
    });
    check(session.confirm_commit(succeeded.snapshot.generation, true),
        "successful TSF result confirms the matching pending commit");
    check(!session.confirm_commit(succeeded.snapshot.generation, true),
        "duplicate commit confirmation is idempotently rejected");
    engine.save_user_model(before_path);
    before.load(before_path);
    const auto learned = before.query_exact("wo");
    const auto learned_word = std::find_if(learned.begin(), learned.end(), [&](const auto& item) {
        return item.word == succeeded.text;
    });
    check(learned_word != learned.end() && learned_word->selection_count == 1U,
        "one successful commit records exactly one selection");

    std::filesystem::remove(before_path);
    std::filesystem::remove(lexicon_path);
}

void test_confirmed_learning_is_shared_without_mutating_an_old_generation() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 2U;
    settings.candidates.max_items = 20U;

    piinput::HostSession source(engine, nullptr, settings, "full");
    piinput::HostSession already_visible(engine, nullptr, settings, "full");
    type(source, "wo");
    type(already_visible, "wo");
    const auto old_snapshot = already_visible.snapshot();
    const auto source_snapshot = source.snapshot();

    const auto target = std::find_if(
        source_snapshot.candidates.begin(), source_snapshot.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "卧"; });
    check(target != source_snapshot.candidates.end(),
        "cross-session fixture exposes a candidate outside the first row");
    const auto committed = source.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = target->id,
    });
    check(committed.action == piinput::HostAction::commit &&
            source.confirm_commit(committed.snapshot.generation, true),
        "a successful TSF confirmation publishes learning to the shared Engine");

    check(already_visible.snapshot().generation == old_snapshot.generation &&
            already_visible.snapshot().candidates == old_snapshot.candidates,
        "an already visible generation remains immutable after another session learns");

    piinput::HostSession new_generation(engine, nullptr, settings, "full");
    type(new_generation, "wo");
    const auto learned = new_generation.snapshot();
    const auto learned_position = std::find_if(
        learned.candidates.begin(), learned.candidates.end(),
        [](const piinput::HostCandidate& candidate) { return candidate.text == "卧"; });
    if (learned_position == learned.candidates.end() ||
        static_cast<std::size_t>(std::distance(
            learned.candidates.begin(), learned_position)) >= settings.candidates.items_per_row) {
        std::cerr << "cross-session candidates:";
        for (const auto& candidate : learned.candidates) std::cerr << ' ' << candidate.text;
        std::cerr << '\n';
    }
    check(learned_position != learned.candidates.end() &&
            static_cast<std::size_t>(std::distance(
                learned.candidates.begin(), learned_position)) < settings.candidates.items_per_row,
        "a new generation in another session immediately sees one-time learning in row one");

    std::filesystem::remove(lexicon_path);
}

void test_disabled_chinese_learning_never_stages_or_records() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    auto settings = piinput::default_settings();
    settings.pinyin.user_learning = false;
    piinput::HostSession session(engine, nullptr, settings, "full");
    type(session, "wo");
    const auto current = session.snapshot();
    const auto committed = session.apply({
        .kind = piinput::HostKeyKind::select_candidate,
        .candidate_id = current.candidates.front().id,
    });
    check(committed.accepted && committed.action == piinput::HostAction::commit,
        "turning off learning does not prevent normal candidate commit");
    check(!session.confirm_commit(committed.snapshot.generation, true),
        "a disabled-learning commit has no pending learning record");
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-host-disabled-learning.tsv";
    engine.save_user_model(path);
    piinput::UserModel model;
    (void)model.load(path);
    check(model.entry_count() == 0U,
        "disabled Chinese learning leaves the shared user model unchanged");
    std::filesystem::remove(path);
    std::filesystem::remove(lexicon_path);
}

void test_long_input_does_not_create_a_mechanical_sentence_candidate() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    piinput::HostSession session(engine, nullptr, piinput::default_settings(), "full");
    type(session, "nibawodetubiao");
    const auto snapshot = session.snapshot();
    check(!snapshot.candidates.empty() && snapshot.candidates.front().text == "你",
        "without a real whole phrase, the first-syllable character is offered first");
    check(std::none_of(snapshot.candidates.begin(), snapshot.candidates.end(),
            [](const piinput::HostCandidate& candidate) {
                return candidate.text == "你把我的图标" || candidate.text == "你吧我的图标";
            }),
        "normal candidates never synthesize a sentence by concatenating entries");
    std::filesystem::remove(lexicon_path);
}

void test_optional_english_session_uses_the_same_host_boundary() {
    piinput::Engine engine;
    piinput::EnglishLexicon english;
    const auto english_path = write_english_lexicon();
    check(english.load_builtin_tsv(english_path) == 3U, "English fixture loads");
    auto settings = piinput::default_settings();
    settings.english.enabled = true;
    piinput::HostSession session(engine, &english, settings, "full");
    check(session.apply({.kind = piinput::HostKeyKind::switch_to_english}).accepted,
        "host can enter configured English mode");
    type(session, "re");
    const auto snapshot = session.snapshot();
    check(snapshot.mode == piinput::HostInputMode::english && snapshot.raw == "re",
        "English composition is owned by the host");
    check(snapshot.candidates.size() >= 3U && snapshot.candidates[0].text == "re" &&
            snapshot.candidates[1].text == "really" &&
            snapshot.candidates[2].text == "remember",
        "English prefix candidates cross the same process-independent boundary");
    std::filesystem::remove(english_path);
}

void test_shift_enters_direct_english_when_candidates_are_disabled() {
    piinput::Engine engine;
    const auto settings = piinput::default_settings();
    check(!settings.english.enabled, "English candidates stay disabled by default");
    piinput::HostSession session(engine, nullptr, settings, "full");

    const auto english = session.apply({.kind = piinput::HostKeyKind::switch_to_english});
    check(english.accepted && english.snapshot.mode == piinput::HostInputMode::english &&
            english.snapshot.raw.empty() && english.snapshot.candidates.empty(),
        "standalone Shift enters direct English even when English candidates are disabled");

    const auto letter = session.apply({
        .kind = piinput::HostKeyKind::text,
        .character = 'b',
    });
    check(letter.accepted && letter.action == piinput::HostAction::commit &&
            letter.text == "b" && letter.snapshot.mode == piinput::HostInputMode::english &&
            letter.snapshot.raw.empty() && letter.snapshot.candidates.empty(),
        "direct English commits literal letters without opening a candidate composition");

    const auto question = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = '/',
        .shifted = true,
    });
    check(question.accepted && question.action == piinput::HostAction::commit &&
            question.text == "?" && question.snapshot.mode == piinput::HostInputMode::english,
        "direct English commits ASCII punctuation");

    const auto resume = session.resume_state();
    piinput::HostSession restarted(engine, nullptr, settings, "full");
    restarted.restore(resume);
    const auto restored = restarted.snapshot();
    check(restored.mode == piinput::HostInputMode::english && restored.raw.empty(),
        "direct English mode survives a Host restart when candidates are disabled");
    const auto restored_letter = restarted.apply({
        .kind = piinput::HostKeyKind::text,
        .character = 'r',
    });
    check(restored_letter.accepted && restored_letter.action == piinput::HostAction::commit &&
            restored_letter.text == "r" &&
            restored_letter.snapshot.mode == piinput::HostInputMode::english,
        "restored direct English mode still commits literal letters");

    const auto chinese = session.apply({.kind = piinput::HostKeyKind::switch_to_chinese});
    check(chinese.accepted && chinese.snapshot.mode == piinput::HostInputMode::chinese,
        "a second standalone Shift returns to Chinese mode");
}

void test_symbol_center_and_semicolon_routing() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    piinput::SymbolIndex symbols;
    const auto symbol_path = write_symbol_lexicon();
    symbols.load_tsv(symbol_path);
    const auto settings = piinput::default_settings();
    piinput::HostSession session(engine, nullptr, &symbols, settings, "full");

    type(session, ";sheshidu");
    const auto retired_semicolon_query = session.snapshot();
    check(retired_semicolon_query.raw == ";sheshidu" &&
            retired_semicolon_query.candidates.empty(),
        "semicolon text no longer enters symbol search");
    (void)session.apply({.kind = piinput::HostKeyKind::escape});

    type(session, "``f");
    const auto grave_menu = session.snapshot();
    check(grave_menu.raw == "``f" && grave_menu.candidates.size() == 2U &&
            grave_menu.candidates.front().text == "℃",
        "double-grave f opens the complete symbol menu");
    (void)session.apply({.kind = piinput::HostKeyKind::escape});

    type(session, "abc");
    const auto toolbar_menu = session.apply({
        .kind = piinput::HostKeyKind::open_symbol_center,
    });
    check(toolbar_menu.accepted && toolbar_menu.action == piinput::HostAction::update &&
            toolbar_menu.snapshot.raw == "``f" &&
            toolbar_menu.snapshot.candidates.size() == 2U &&
            toolbar_menu.snapshot.candidates.front().text == "℃",
        "the toolbar opens the symbol center directly without committing command text");
    (void)session.apply({.kind = piinput::HostKeyKind::escape});

    type(session, "```");
    const auto markdown = session.snapshot();
    check(markdown.raw == "```" && markdown.candidates.empty(),
        "three grave accents stay literal for Markdown code fences");
    const auto markdown_commit = session.apply({.kind = piinput::HostKeyKind::space});
    check(markdown_commit.accepted && markdown_commit.action == piinput::HostAction::commit &&
            markdown_commit.text == "```",
        "Space commits a Markdown code fence instead of trapping the command prefix");

    type(session, "`");
    const auto literal_grave = session.apply({.kind = piinput::HostKeyKind::space});
    check(literal_grave.accepted && literal_grave.action == piinput::HostAction::commit &&
            literal_grave.text == "`",
        "a single grave key stays literal for Markdown inline code");

    type(session, "`code`");
    const auto inline_code = session.apply({.kind = piinput::HostKeyKind::space});
    check(inline_code.accepted && inline_code.action == piinput::HostAction::commit &&
            inline_code.text == "`code`",
        "Markdown inline code stays literal instead of becoming a command");

    const auto chinese_semicolon = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = ';',
    });
    check(chinese_semicolon.accepted &&
            chinese_semicolon.action == piinput::HostAction::commit &&
            chinese_semicolon.text == "；",
        "a single semicolon immediately commits configured Chinese punctuation");

    type(session, "wo");
    const auto composed_semicolon = session.apply({
        .kind = piinput::HostKeyKind::punctuation,
        .character = ';',
    });
    check(composed_semicolon.accepted &&
            composed_semicolon.action == piinput::HostAction::commit &&
            composed_semicolon.text == "我；",
        "semicolon commits the active Chinese candidate and punctuation atomically");

    std::filesystem::remove(symbol_path);
    std::filesystem::remove(lexicon_path);
}

void test_candidate_two_launches_tools_without_committing_the_label() {
    piinput::Engine engine;
    const auto lexicon_path = write_chinese_lexicon();
    engine.load_lexicon(lexicon_path);
    const auto settings = piinput::default_settings();
    piinput::HostSession session(engine, nullptr, settings, "full");

    const auto choose_second = [&](const std::string_view input) {
        type(session, std::string(input));
        check(session.snapshot().candidates.size() >= 2U,
            "tool shortcut exposes a second candidate");
        return session.apply({
            .kind = piinput::HostKeyKind::select_digit,
            .character = '2',
        });
    };

    const auto symbol = choose_second("fh");
    check(symbol.accepted && symbol.action == piinput::HostAction::launch_symbol_tool &&
            symbol.text.empty() && symbol.snapshot.raw.empty(),
        "candidate 2 for fh clears composition and requests yesymbol without committing its label");

    const auto emoji = choose_second("bq");
    check(emoji.accepted && emoji.action == piinput::HostAction::launch_symbol_tool &&
            emoji.text.empty() && emoji.snapshot.raw.empty(),
        "candidate 2 for bq shares the yesymbol launch action");

    const auto settings_reply = choose_second("sz");
    check(settings_reply.accepted &&
            settings_reply.action == piinput::HostAction::launch_settings &&
            settings_reply.text.empty() && settings_reply.snapshot.raw.empty(),
        "candidate 2 for sz clears composition and requests the settings application");
    std::filesystem::remove(lexicon_path);
}

}  // namespace

int main() {
    test_configured_default_input_language_applies_to_new_sessions();
    test_chinese_session_generations_stale_selection_and_view_reset();
    test_xiaohe_and_restart_resume_recompute_candidates();
    test_repeated_equals_enters_segment_selection_and_stages_until_final_commit();
    test_real_words_fill_multiple_rows_before_single_characters();
    test_mktm_keeps_all_words_before_ming_character_selection();
    test_dash_at_the_top_keeps_the_candidate_row_on_screen();
    test_enter_commits_the_selection_once_it_has_been_moved();
    test_datetime_entry_opens_a_second_level_list();
    test_datetime_entry_opens_a_second_level_list();
    test_equals_reaches_the_bottom_and_stays_there();
    test_paging_keys_stop_at_the_ends_without_typing_themselves();
    test_dash_still_pages_the_candidate_rows_when_a_row_exists();
    test_kadun_word_then_character_fallback_stages_normally();
    test_staging_a_character_advances_to_the_next_syllable();
    test_normal_word_pages_include_single_character_fallbacks();
    test_expanded_row_count_is_configurable_and_starts_on_row_two();
    test_one_successful_segment_composition_creates_a_first_row_phrase();
    test_partial_word_selection_preserves_remainder_and_promotes_once_confirmed();
    test_host_candidate_arrows_move_in_all_four_directions();
    test_host_learns_only_after_confirmed_commit();
    test_confirmed_learning_is_shared_without_mutating_an_old_generation();
    test_disabled_chinese_learning_never_stages_or_records();
    test_host_can_pin_and_suppress_current_generation_candidates();
    test_long_input_does_not_create_a_mechanical_sentence_candidate();
    test_shift_enters_direct_english_when_candidates_are_disabled();
    test_optional_english_session_uses_the_same_host_boundary();
    test_space_and_digits_are_resolved_by_current_host_state();
    test_punctuation_is_transformed_and_committed_by_host();
    test_symbol_center_and_semicolon_routing();
    test_candidate_two_launches_tools_without_committing_the_label();
    std::cout << "PiInput host session tests passed.\n";
    return 0;
}
