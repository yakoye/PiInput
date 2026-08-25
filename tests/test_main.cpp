#include "piinput/binary_lexicon.h"
#include "piinput/candidate_paging.h"
#include "piinput/dictionary_builder.h"
#include "piinput/datetime_candidates.h"
#include "piinput/engine.h"
#include "piinput/input_mode.h"
#include "piinput/lexicon.h"
#include "piinput/pinyin.h"
#include "piinput/punctuation.h"
#include "piinput/smart_punctuation.h"
#include "piinput/scel_parser.h"
#include "piinput/session.h"
#include "piinput/shuangpin.h"
#include "piinput/symbols.h"
#include "piinput/utf.h"
#include "piinput/user_model.h"
#include "piinput/windows_compat.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains_canonical(
    const std::vector<piinput::PinyinSegmentation>& segmentations,
    const std::string& expected) {
    return std::any_of(segmentations.begin(), segmentations.end(), [&](const auto& item) {
        return item.canonical == expected;
    });
}

void write_sample_tsv(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "word\tpinyin\tweight\n"
           << "计算机\tji'suan'ji\t100\n"
           << "计算器\tji'suan'qi\t80\n"
           << "计蒜机\tji'suan'ji\t20\n"
           << "西安\txi'an\t90\n"
           << "先\txian\t70\n"
           << "输入法\tshu'ru'fa\t120\n"
           << "我\two\t100\n"
           << "想\txiang\t100\n"
           << "我想\two'xiang\t180\n"
           << "学习\txue'xi\t160\n"
           << "协议\txie'yi\t150\n"
           << "学习协议\txue'xi'xie'yi\t260\n";
}

[[nodiscard]] std::vector<std::vector<std::string>> read_test_table(const std::string& name) {
    const auto path = std::filesystem::path(PIINPUT_SOURCE_DIR) / "tests" / "data" / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open test data: " + path.string());
    }
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string> columns;
        std::stringstream stream(line);
        std::string column;
        while (std::getline(stream, column, '\t')) {
            columns.push_back(column);
        }
        rows.push_back(std::move(columns));
    }
    return rows;
}

void verify_candidate_table(
    piinput::Engine& engine,
    const std::string& table,
    const std::string& schema) {
    for (const auto& row : read_test_table(table)) {
        check(row.size() == 4U, table + " row has four columns");
        if (row.size() != 4U) {
            continue;
        }
        const std::size_t max_rank = static_cast<std::size_t>(std::stoul(row[3]));
        const std::size_t limit = max_rank == 0U ? 64U : max_rank;
        const auto decoded = engine.decode(row[0], schema, 32U);
        check(contains_canonical(decoded, row[1]), schema + " decodes " + row[0] + " -> " + row[1]);
        const auto candidates = engine.query(row[0], schema, limit);
        const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.word == row[2];
        });
        check(found != candidates.end(), schema + " candidate " + row[0] + " contains " + row[2]);
        if (max_rank > 0U && found != candidates.end()) {
            check(static_cast<std::size_t>(std::distance(candidates.begin(), found)) < max_rank,
                schema + " candidate rank for " + row[2]);
        }
    }
}

void test_lexicon() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-lexicon.tsv";
    write_sample_tsv(path);

    piinput::DevLexicon lexicon;
    lexicon.load_tsv(path);
    check(lexicon.entry_count() == 12U, "TSV lexicon entry count");
    const auto results = lexicon.query_exact("ji'suan'ji", 10U);
    check(results.size() == 2U, "Exact pinyin query result count");
    check(!results.empty() && results.front().word == "计算机", "Candidate ordering by weight");
    const auto prefix_results = lexicon.query_prefix("ji's", 10U, 32U);
    check(prefix_results.size() == 3U, "Pinyin prefix query result count");
    check(!prefix_results.empty() && prefix_results.front().word == "计算机",
        "Pinyin prefix query keeps deterministic weight order");
    std::filesystem::remove(path);
}

void test_binary_lexicon() {
    const auto temp = std::filesystem::temp_directory_path();
    const auto tsv = temp / "piinput-test-binary-source.tsv";
    const auto lex = temp / "piinput-test-binary.lex";
    write_sample_tsv(tsv);
    {
        std::ofstream append(tsv, std::ios::binary | std::ios::app);
        append << "计算机\tji'suan'ji\t20000\n";
    }
    piinput::compile_tsv_to_binary(tsv, lex);
    check(piinput::is_binary_lexicon(lex), "Binary lexicon magic detection");

    piinput::BinaryLexicon binary;
    binary.load(lex);
    check(binary.entry_count() == 12U, "Binary lexicon entry count");
    check(binary.memory_mapped(), "Binary lexicon uses a read-only file mapping");
    check(binary.mapped_bytes() == std::filesystem::file_size(lex),
        "Binary lexicon reports the complete mapped byte count");
    const auto results = binary.query_exact("shu'ru'fa", 5U);
    check(results.size() == 1U && results.front().word == "输入法", "Binary lexicon query");
    const auto deduplicated = binary.query_exact("ji'suan'ji", 5U);
    check(deduplicated.size() == 2U, "Binary lexicon de-duplicates identical word+pinyin pairs");
    check(!deduplicated.empty() && deduplicated.front().word == "计算机" && deduplicated.front().weight == 20000U,
        "Binary lexicon keeps the highest duplicate weight");
    const auto prefix_results = binary.query_prefix("ji's", 5U, 32U);
    check(prefix_results.size() == 3U, "Binary lexicon supports bounded prefix query");
    std::filesystem::remove(tsv);
    std::filesystem::remove(lex);
}

void test_pinyin() {
    piinput::PinyinSegmenter segmenter;
    check(segmenter.is_syllable("xian"), "xian is a valid syllable");
    check(segmenter.is_valid_prefix("zhua"), "zhua is a valid prefix");
    check(!segmenter.is_valid_prefix("qqq"), "qqq is not a valid prefix");
    check(piinput::PinyinSegmenter::normalize("nü'e") == "nv'e", "Normalize ü to v");

    const auto computer = segmenter.segment("jisuanji", 8U);
    check(contains_canonical(computer, "ji'suan'ji"), "Continuous full pinyin segmentation");

    const auto ambiguous = segmenter.segment("xian", 8U);
    check(contains_canonical(ambiguous, "xian"), "Ambiguous pinyin includes xian");
    check(contains_canonical(ambiguous, "xi'an"), "Ambiguous pinyin includes xi'an");

    const auto manual = segmenter.segment("xi'an", 8U);
    check(manual.size() == 1U && manual.front().canonical == "xi'an", "Manual apostrophe boundary");

    for (const auto& syllable : piinput::PinyinSegmenter::standard_syllables()) {
        const auto decoded = segmenter.segment(syllable, 32U);
        check(contains_canonical(decoded, syllable), "Full pinyin accepts standard syllable " + syllable);
    }
}

void test_shuangpin() {
    piinput::ShuangpinDecoder decoder;
    check(decoder.has_scheme("flypy"), "Flypy scheme exists");
    check(decoder.has_scheme("natural"), "Natural scheme exists");
    check(decoder.has_scheme("mspy"), "Microsoft scheme exists");
    check(decoder.has_scheme("abc"), "ABC scheme exists");

    const auto flypy = decoder.decode("flypy", "jisrji", 8U);
    check(contains_canonical(flypy, "ji'suan'ji"), "Flypy decodes 计算机 pinyin");

    const auto flypy_input_method = decoder.decode("flypy", "uurufa", 8U);
    check(contains_canonical(flypy_input_method, "shu'ru'fa"), "Flypy decodes 输入法 pinyin");

    check(contains_canonical(decoder.decode("flypy", "yuwh", 8U), "yu'wang"),
        "Flypy accepts u as a safe umlaut-key alias in yuwh");
    check(contains_canonical(decoder.decode("flypy", "yvwh", 8U), "yu'wang"),
        "Flypy retains canonical v-key spelling for yvwh");
    for (const auto* alias : {"ju", "qu", "xu", "yu"}) {
        check(!decoder.decode("flypy", alias, 8U).empty(),
            std::string("Flypy safe u/v alias decodes: ") + alias);
    }
    check(!contains_canonical(decoder.decode("flypy", "lu", 8U), "lv") &&
            !contains_canonical(decoder.decode("flypy", "nu", 8U), "nv"),
        "Flypy u/v compatibility never merges ambiguous lu/lv or nu/nv syllables");
    check(contains_canonical(decoder.decode("flypy", "og", 8U), "eng"),
        "Flypy accepts the user-verified og compatibility spelling for eng");

    std::unordered_set<std::string> user_verified_codes;
    for (const auto& row : read_test_table("xiaohe_legal_codes.tsv")) {
        check(row.size() == 1U, "Xiaohe legal-code row has one column");
        if (row.size() != 1U) {
            continue;
        }
        check(user_verified_codes.insert(row[0]).second,
            "Xiaohe legal-code inventory contains no duplicate: " + row[0]);
        check(!decoder.syllables_for_code("flypy", row[0], false).empty(),
            "Xiaohe user-verified code is decodable: " + row[0]);
    }
    check(user_verified_codes.size() == 406U,
        "Xiaohe user-verified inventory contains exactly 406 unique codes");

    const auto natural = decoder.decode("natural", "jisrji", 8U);
    check(contains_canonical(natural, "ji'suan'ji"), "Natural code path works");

    const auto microsoft = decoder.decode("mspy", "jisrji", 8U);
    check(contains_canonical(microsoft, "ji'suan'ji"), "Microsoft code path works");

    const auto abc = decoder.decode("abc", "jispji", 8U);
    check(contains_canonical(abc, "ji'suan'ji"), "ABC code path works");

    for (const auto& row : read_test_table("xiaohe_mapping.tsv")) {
        check(row.size() == 3U, "Xiaohe mapping row has three columns");
        if (row.size() != 3U) {
            continue;
        }
        const auto decoded = decoder.decode("flypy", row[0], 32U);
        check(contains_canonical(decoded, row[1]),
            "Flypy mapping " + row[0] + " -> " + row[1] + " (" + row[2] + ")");
    }

    std::unordered_set<std::string> reachable;
    for (char first = 'a'; first <= 'z'; ++first) {
        for (char second = 'a'; second <= 'z'; ++second) {
            std::string code{first, second};
            for (const auto& syllable : decoder.syllables_for_code("flypy", code)) {
                reachable.insert(syllable);
            }
        }
    }
    for (const auto& syllable : piinput::PinyinSegmenter::standard_syllables()) {
        check(reachable.contains(syllable), "Flypy can encode standard syllable " + syllable);
    }
    check(decoder.decode("flypy", "gjj", 8U).empty(), "Incomplete Flypy pair has no premature candidate");
    check(decoder.decode("flypy", "qz", 8U).empty(), "Invalid Flypy code is rejected");
}

void test_dictionary_builder() {
    const auto root = std::filesystem::path(PIINPUT_SOURCE_DIR) / "tests" / "data" / "dictionary_builder";
    const auto characters = piinput::read_dictionary_source(
        root / "pinyin_data.txt", piinput::DictionarySourceFormat::pinyin_data, 1000U);
    check(characters.size() == 2U, "Dictionary builder reads pinyin-data characters");
    check(characters.size() >= 2U && characters[0].pinyin == "wo" && characters[1].pinyin == "ai",
        "Dictionary builder removes character tones");

    const auto phrases = piinput::read_dictionary_source(
        root / "phrases.txt", piinput::DictionarySourceFormat::phrase_pinyin_data, 2000U);
    check(!phrases.empty() && phrases.front().word == "感觉" && phrases.front().pinyin == "gan'jue",
        "Dictionary builder reads phrase-pinyin-data");

    const auto rime = piinput::read_dictionary_source(
        root / "rime.dict.yaml", piinput::DictionarySourceFormat::rime_yaml, 3000U);
    check(rime.size() == 2U && rime.front().word == "感觉" && rime.front().weight == 200U,
        "Dictionary builder reads Rime YAML entries and weights");

    const auto output = std::filesystem::temp_directory_path() / "piinput-dictionary-builder-test.tsv";
    std::vector<piinput::LexiconCandidate> combined = characters;
    combined.insert(combined.end(), phrases.begin(), phrases.end());
    combined.insert(combined.end(), rime.begin(), rime.end());
    piinput::write_dictionary_tsv(output, std::move(combined));
    piinput::DevLexicon lexicon;
    lexicon.load_tsv(output);
    const auto feeling = lexicon.query_exact("gan'jue", 10U);
    check(feeling.size() == 1U && feeling.front().word == "感觉" && feeling.front().weight == 200U,
        "Dictionary builder preserves authoritative frequency over pronunciation fallback");
    std::filesystem::remove(output);
}

void verify_incremental_candidates(
    piinput::Engine& engine,
    const std::string& table = "incremental_candidates.tsv") {
    for (const auto& row : read_test_table(table)) {
        check(row.size() == 5U, "incremental candidate row has five columns");
        if (row.size() != 5U) {
            continue;
        }
        const std::size_t max_rank = static_cast<std::size_t>(std::stoul(row[4]));
        const auto candidates = engine.query(row[1], row[0], max_rank);
        const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.word == row[3] && candidate.pinyin.starts_with(row[2]);
        });
        check(found != candidates.end(),
            row[0] + " incremental candidate " + row[1] + " contains " + row[3]);
        if (found != candidates.end()) {
            check(static_cast<std::size_t>(std::distance(candidates.begin(), found)) < max_rank,
                row[0] + " incremental candidate rank for " + row[3]);
        }
    }
}

void verify_core_input_cases(piinput::Engine& engine) {
    for (const auto& row : read_test_table("core_input_cases.tsv")) {
        check(row.size() == 7U, "core_input_cases.tsv row has seven columns");
        if (row.size() != 7U || row[0] != "required") {
            continue;
        }
        const std::size_t max_rank = static_cast<std::size_t>(std::stoul(row[5]));
        const auto decoded = engine.decode(row[2], row[1], 32U);
        check(contains_canonical(decoded, row[3]), row[1] + " decodes required case " + row[2]);
        const auto candidates = engine.query(row[2], row[1], max_rank);
        if (row[6].ends_with("sentence") || row[6] == "disambiguation") {
            check(!candidates.empty(), row[1] + " long input keeps lexical candidates available");
            // A bounded number of non-adjacent high-frequency characters may
            // connect real words (促使我们去思考, for example). The evidence
            // count remains below the number of lexical tokens, which excludes
            // an unrestricted character-by-character sentence fallback.
            check(std::all_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
                    return candidate.evidence.kind != piinput::CandidateKind::decoded_sentence ||
                        (candidate.evidence.single_character_tokens <= 3U &&
                            candidate.evidence.single_character_tokens <
                                candidate.evidence.word_count &&
                            candidate.evidence.word_count >= 2U &&
                            candidate.evidence.covers_all_input);
                }),
                row[1] + " long input uses bounded word composition covering all input");
            continue;
        }
        const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.word == row[4];
        });
        check(found != candidates.end(), row[1] + " required target " + row[4] + " for " + row[2]);
    }
}

void test_candidate_paging() {
    const piinput::CandidatePageSettings defaults;
    check(defaults.single_syllable == 9U, "Single-syllable page defaults to nine candidates");
    check(defaults.multi_syllable == 6U, "Phrase page defaults to six candidates");
    check(piinput::candidate_page_size(defaults, 1U, false) == 9U, "One syllable uses single-character page size");
    check(piinput::candidate_page_size(defaults, 2U, false) == 6U, "Multiple syllables use phrase page size");
    check(piinput::candidate_page_size(defaults, 0U, true) == 6U, "Symbols use phrase page size");
    check(piinput::move_candidate_page(0U, 20U, 9U, -1) == 18U, "Previous page wraps to final page");
    check(piinput::move_candidate_page(18U, 20U, 9U, 1) == 0U, "Next page wraps to first page");
    check(piinput::move_candidate_page(0U, 13U, 6U, 1) == 6U, "Phrase next page advances by six");
    check(piinput::align_candidate_page(6U, 20U, 9U) == 0U,
        "Changing from six to nine candidates realigns the page boundary");

    const auto settings_path = std::filesystem::temp_directory_path() / "piinput-page-settings.ini";
    {
        std::ofstream output(settings_path, std::ios::trunc);
        output << "single_syllable_page_size=8\nphrase_page_size=5\n";
    }
    const auto configured = piinput::load_candidate_page_settings(settings_path);
    check(configured.single_syllable == 8U && configured.multi_syllable == 5U,
        "Candidate page sizes can be configured");
    std::filesystem::remove(settings_path);
}

// Typing a symbol name as pinyin offers the symbol among the ordinary
// candidates. The lookup is on the decoded reading, not the raw keys, which is
// what makes it work in every double-pinyin schema without a second table.
// riqi and shijian spell out the current date and time. The clock is pinned so
// the expected strings can be written down; without that the test could only
// check shapes, which is exactly where a wrong format would hide.
// A finished syllable used to match only words whose whole reading was that
// syllable, so ri offered two characters and the row looked like the dictionary
// had run out. Words that start with it are appended to fill the gap.
// A copied engine has to keep answering the same way. Symbol and date shortcuts
// are configuration rather than cache, and leaving them out of the copy loses
// the features silently -- the copy still returns candidates, just without them.
// query() promises at most `limit` candidates. The shortcuts used to be spliced
// in after the list had already been trimmed to that limit, so the caller got
// more than it asked for -- a request for 89 came back with 96.
void test_query_never_exceeds_the_requested_limit() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-limit.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        for (int index = 0; index < 30; ++index) {
            output << "词" << index << "\tri\t" << (9000 - index) << "\n";
        }
        output << "日期\tri'qi\t9000\n";
    }
    std::tm fixed{};
    fixed.tm_year = 2026 - 1900;
    fixed.tm_mon = 8 - 1;
    fixed.tm_mday = 21;

    piinput::Engine engine;
    engine.load_lexicon(path);
    engine.set_clock([fixed] { return fixed; });
    engine.set_symbol_shortcuts({{"ri", {"日", "☀"}}});

    // Including limits smaller than the number of generated candidates, where
    // the generated ones themselves have to be cut.
    for (const std::size_t limit : {std::size_t{1}, std::size_t{2}, std::size_t{3},
             std::size_t{5}, std::size_t{8}, std::size_t{20}, std::size_t{40}}) {
        for (const char* const text : {"riqi", "ri"}) {
            const auto got = engine.query(text, "full", limit);
            check(got.size() <= limit, "候选数不得超过调用方要求的上限");
        }
    }

    // The top dictionary word keeps its place even when room has to be made.
    const auto tight = engine.query("riqi", "full", 3U);
    check(!tight.empty() && tight.front().word == "日期",
        "腾位时不应挤掉词典首选");
    std::filesystem::remove(path);
}

void test_engine_copy_keeps_its_configuration() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-engine-copy.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n派\tpai\t9000\n";
    }
    std::tm fixed{};
    fixed.tm_year = 2026 - 1900;
    fixed.tm_mon = 8 - 1;
    fixed.tm_mday = 21;

    piinput::Engine original;
    original.load_lexicon(path);
    original.set_symbol_shortcuts({{"pai", {"π"}}});
    original.set_clock([fixed] { return fixed; });

    const piinput::Engine copied(original);
    const auto from_copy = copied.query("pai", "full", 6U);
    check(from_copy.size() >= 2U && from_copy[1U].word == "π",
        "拷贝出的引擎应保留符号捷径");
    const auto dated = copied.query("riqi", "full", 6U);
    check(!dated.empty() && dated.front().word == piinput::datetime_group_label(true),
        "拷贝出的引擎应保留时钟设置");

    piinput::Engine assigned;
    assigned = original;
    const auto from_assigned = assigned.query("pai", "full", 6U);
    check(from_assigned.size() >= 2U && from_assigned[1U].word == "π",
        "赋值出的引擎同样应保留符号捷径");

    std::filesystem::remove(path);
}

void test_short_readings_are_filled_with_prefix_words() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-prefix-fill.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        output << "日\tri\t9000\n驲\tri\t10\n";
        output << "日期\tri'qi\t8000\n日本\tri'ben\t7000\n";
        output << "日子\tri'zi\t6000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);

    const auto filled = engine.query("ri", "full", 10U);
    check(filled.size() > 2U, "候选不足时应补上以该音开头的词");
    check(filled.front().word == "日", "补齐不应改变原有首选");
    const auto has = [&](const std::string& word) {
        return std::any_of(filled.begin(), filled.end(),
            [&](const piinput::EngineCandidate& one) { return one.word == word; });
    };
    check(has("日期") && has("日本") && has("日子"), "以该音开头的词都应出现");

    // Deleting a candidate has to survive this path too. It bypasses the ranking
    // step that filters suppressed words, so it has to ask on its own.
    engine.suppress_candidate("ri'qi", "日期");
    const auto after_delete = engine.query("ri", "full", 10U);
    check(std::none_of(after_delete.begin(), after_delete.end(),
              [](const piinput::EngineCandidate& one) { return one.word == "日期"; }),
        "被删除的候选不应从前缀补齐里回来");
    check(std::any_of(after_delete.begin(), after_delete.end(),
              [](const piinput::EngineCandidate& one) { return one.word == "日本"; }),
        "删除一个不应影响其他补齐结果");

    std::filesystem::remove(path);
}

void test_date_and_time_candidates() {
    std::tm fixed{};
    fixed.tm_year = 2026 - 1900;
    fixed.tm_mon = 8 - 1;
    fixed.tm_mday = 21;
    fixed.tm_hour = 11;
    fixed.tm_min = 30;
    fixed.tm_sec = 34;

    const auto dates = piinput::date_candidates(fixed);
    const std::vector<std::string> expected_dates{
        "2026年8月21日", "2026-08-21", "2026.08.21", "2026/08/21", "20260821",
        "二〇二六年八月二十一日", "丙午[马]年七月初九"};
    check(dates == expected_dates, "日期候选应覆盖全部格式且顺序固定");

    const auto times = piinput::time_candidates(fixed);
    const std::vector<std::string> expected_times{
        "11:30:34", "2026年8月21日 11:30:34", "2026-08-21 11:30:34",
        "2026.08.21 11:30:34", "20260821113034", "20260821_113034"};
    check(times == expected_times, "时间候选应覆盖全部格式且顺序固定");

    // The lunar calendar has no formula, only a table, so the conversion is
    // checked against dates whose answer is independently known -- including a
    // leap month, which is where a table-driven conversion goes wrong.
    struct LunarCase { int year; int month; int day; const char* expected; };
    const LunarCase lunar_cases[]{
        {2026, 8, 21, "丙午[马]年七月初九"},
        {2024, 2, 10, "甲辰[龙]年正月初一"},
        {2025, 1, 29, "乙巳[蛇]年正月初一"},
        {2023, 3, 22, "癸卯[兔]年闰二月初一"},
        {2000, 1, 1, "己卯[兔]年冬月廿五"},
    };
    for (const auto& one : lunar_cases) {
        piinput::LunarDate lunar{};
        check(piinput::gregorian_to_lunar(one.year, one.month, one.day, lunar),
            "表覆盖范围内的日期都应能换算");
        check(piinput::format_lunar_date(lunar) == one.expected, "农历换算结果应正确");
    }

    // Outside the table the lunar line is left out rather than guessed at.
    piinput::LunarDate out_of_range{};
    check(!piinput::gregorian_to_lunar(1899, 12, 31, out_of_range),
        "表范围之外不应给出农历");
    check(!piinput::gregorian_to_lunar(2101, 1, 1, out_of_range),
        "表范围之外不应给出农历");

    // Through the engine, where they have to land after the dictionary word and
    // reach every schema by the decoded reading.
    const auto path = std::filesystem::temp_directory_path() / "piinput-datetime.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        output << "日期\tri\'qi\t9000\n时间\tshi\'jian\t9000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    engine.set_clock([fixed] { return fixed; });

    const auto full = engine.query("riqi", "full", 8U);
    check(!full.empty() && full.front().word == "日期", "词典首选不应被日期顶掉");
    // The entry is labelled rather than showing a format: it opens the list, so
    // it must not look like one of the answers.
    check(full.size() >= 2U && full[1U].word == piinput::datetime_group_label(true),
        "第二位应是日期入口");

    const auto timed = engine.query("shijian", "full", 8U);
    check(!timed.empty() && timed.front().word == "时间", "词典首选不应被时间顶掉");
    check(timed.size() >= 2U && timed[1U].word == piinput::datetime_group_label(false),
        "第二位应是时间入口");

    struct ShortcutCase {
        const char* input;
        bool date;
    };
    for (const ShortcutCase shortcut : {
             ShortcutCase{"sj", false},
             ShortcutCase{"shij", false},
             ShortcutCase{"shijian", false},
             ShortcutCase{"riq", true},
             ShortcutCase{"riqi", true}}) {
        const auto candidates = engine.query(shortcut.input, "full", 8U);
        const std::string expected_label = piinput::datetime_group_label(shortcut.date);
        check(std::any_of(candidates.begin(), candidates.end(),
                  [&](const piinput::EngineCandidate& candidate) {
                      return candidate.word == expected_label &&
                          candidate.pinyin == shortcut.input &&
                          candidate.evidence.kind == piinput::CandidateKind::datetime_group;
                  }),
            std::string("全拼日期时间快捷码应生成对应入口：") + shortcut.input);
        const auto formats = engine.datetime_formats(shortcut.input);
        check(formats == (shortcut.date ? expected_dates : expected_times),
            std::string("日期时间快捷码应展开完整格式列表：") + shortcut.input);
    }
    const auto flypy_short_alias = engine.query("sj", "flypy", 8U);
    check(std::none_of(flypy_short_alias.begin(), flypy_short_alias.end(),
              [](const piinput::EngineCandidate& candidate) {
                  return candidate.evidence.kind == piinput::CandidateKind::datetime_group;
              }),
        "全拼专用的 sj 快捷码不应覆盖双拼键位");

    // 小鹤双拼: shi is ui, jian is jm. Nothing in the date code knows that.
    const auto shuangpin = engine.query("uijm", "flypy", 8U);
    check(!shuangpin.empty() && shuangpin.front().word == "时间",
        "双拼下也应先给词典结果");
    check(shuangpin.size() >= 2U &&
            shuangpin[1U].word == piinput::datetime_group_label(false),
        "双拼下时间入口同样排第二");

    std::filesystem::remove(path);
}

void test_symbol_shortcuts_reach_candidates_in_every_schema() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-symbol-shortcut.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        output << "派\tpai\t9000\n排\tpai\t8000\n拍\tpai\t7000\n";
        output << "上\tshang\t9000\n伤\tshang\t8000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    engine.set_symbol_shortcuts({
        {"pai", {"π"}},
        {"pi", {"π"}},
        {"shang", {"↑"}},
        {"up", {"↑"}},
    });

    const auto full = engine.query("pai", "full", 6U);
    check(full.size() >= 2U, "全拼 pai 应有候选");
    check(full.front().word == "派", "词典首选不应被符号顶掉");
    check(full[1U].word == "π", "符号应排在第二位");

    // The same symbol, reached through a schema where the keys spell nothing
    // like the reading. Nothing in the shortcut table knows about 双拼.
    const auto shuangpin = engine.query("pl", "mspy", 6U);
    check(shuangpin.size() >= 2U, "双拼 pl 应解出 pai 的候选");
    check(shuangpin.front().word == "派", "双拼下词典首选同样不被顶掉");
    check(shuangpin[1U].word == "π", "双拼下符号同样排第二");

    // An English name is not pinyin and never appears as a syllable, so it can
    // only be matched against the raw input.
    const auto english_name = engine.query("up", "full", 6U);
    check(!english_name.empty() && english_name.front().word == "↑",
        "英文名 up 无拼音候选时符号排第一");

    // A reading with no shortcut is left exactly as the dictionary ranked it.
    const auto untouched = engine.query("shang", "full", 6U);
    check(untouched.size() >= 2U && untouched.front().word == "上",
        "有捷径时首选仍是词典结果");
    check(untouched[1U].word == "↑", "shang 的符号也排第二");

    std::filesystem::remove(path);
}

void test_tool_shortcuts_are_always_candidate_two() {
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-tool-shortcuts.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        output << "符号\tfu'hao\t9000\n";
        output << "表情\tbiao'qing\t9000\n";
        output << "设置\tshe'zhi\t9000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);

    struct ShortcutCase {
        const char* input;
        const char* schema;
        const char* label;
        piinput::CandidateKind kind;
    };
    const ShortcutCase cases[]{
        {"fh", "full", "Ω符号", piinput::CandidateKind::symbol_tool_action},
        {"fuhao", "full", "Ω符号", piinput::CandidateKind::symbol_tool_action},
        {"fuh", "full", "Ω符号", piinput::CandidateKind::symbol_tool_action},
        {"fuhc", "flypy", "Ω符号", piinput::CandidateKind::symbol_tool_action},
        {"bq", "full", "😜表情", piinput::CandidateKind::emoji_tool_action},
        {"biaoqing", "full", "😜表情", piinput::CandidateKind::emoji_tool_action},
        {"biaoq", "full", "😜表情", piinput::CandidateKind::emoji_tool_action},
        {"bnqk", "flypy", "😜表情", piinput::CandidateKind::emoji_tool_action},
        {"bnq", "flypy", "😜表情", piinput::CandidateKind::emoji_tool_action},
        {"shizhi", "full", "⚙️设置", piinput::CandidateKind::settings_action},
        {"uevi", "flypy", "⚙️设置", piinput::CandidateKind::settings_action},
        {"sz", "full", "⚙️设置", piinput::CandidateKind::settings_action},
        {"shiz", "full", "⚙️设置", piinput::CandidateKind::settings_action},
        {"uev", "flypy", "⚙️设置", piinput::CandidateKind::settings_action},
    };
    for (const auto& shortcut : cases) {
        const auto candidates = engine.query(shortcut.input, shortcut.schema, 8U);
        check(candidates.size() >= 2U && candidates[1U].word == shortcut.label &&
                candidates[1U].evidence.kind == shortcut.kind,
            std::string("工具快捷码应固定在候选 2：") + shortcut.input);
    }

    const auto cross_schema = engine.query("uevi", "full", 8U);
    check(cross_schema.size() >= 2U && cross_schema.front().word == "设置" &&
            cross_schema[1U].word == "⚙️设置",
        "原始快捷码无法按当前方案解析时仍应保留候选 2 的设置入口");
    const auto one_slot = engine.query("uevi", "full", 1U);
    check(one_slot.empty() || one_slot.front().word != "⚙️设置",
        "只有一个候选槽位时功能入口不能悄悄移到候选 1");
    std::filesystem::remove(path);
}

void test_shift_toggle_state() {
    piinput::ShiftToggleState state;
    state.on_shift_down();
    check(state.on_shift_up(), "Standalone Shift toggles input mode");
    state.on_shift_down();
    (void)state.on_other_key_down();
    check(!state.on_shift_up(), "Shift used as a modifier does not toggle input mode");
    check(!state.on_shift_up(), "Unmatched Shift release does not toggle input mode");
    state.on_shift_down(true);
    check(!state.on_shift_up(), "Shift pressed after Ctrl Alt or Win does not toggle input mode");

    state.on_shift_down();
    check(state.on_other_key_down(false),
        "missing Shift KeyUp is recovered before the next unshifted key");
    check(!state.on_shift_up(),
        "a delayed Shift KeyUp cannot toggle twice after recovery");

    state.on_shift_down();
    check(!state.on_other_key_down(true),
        "a following key while Shift remains physically down marks modifier use");
    check(!state.on_shift_up(),
        "Shift used as a physical modifier does not toggle on release");

    piinput::ShiftToggleState missing_down;
    check(missing_down.on_shift_up(),
        "a standalone Shift release recovers when the host omitted KeyDown");
    check(!missing_down.on_shift_up(),
        "the recovered Shift release cannot toggle twice");

    piinput::ShiftToggleState missing_down_modifier;
    check(!missing_down_modifier.on_other_key_down(true),
        "a modified key can arrive even when the host omitted Shift KeyDown");
    check(!missing_down_modifier.on_shift_up(),
        "the matching Shift release after modifier use cannot toggle input mode");

    // Ctrl+Shift is how Windows switches input methods. The switch lands between
    // the Shift press and its release, so the method being switched into sees a
    // release with no press of its own -- indistinguishable from a bare tap
    // except that Ctrl is still held.
    piinput::ShiftToggleState switched_into;
    check(!switched_into.on_shift_up(true),
        "a Shift release with Ctrl still held is a chord, not an input mode toggle");

    // The same, arriving after this method was activated mid-chord.
    piinput::ShiftToggleState activated_midchord;
    activated_midchord.reset();
    check(!activated_midchord.on_shift_up(),
        "the first Shift release after activation cannot toggle input mode");
    activated_midchord.on_shift_down();
    check(activated_midchord.on_shift_up(),
        "a real Shift tap after that still toggles");

    // Ctrl held down through a complete Shift press and release.
    piinput::ShiftToggleState full_chord;
    full_chord.on_shift_down(true);
    check(!full_chord.on_shift_up(true), "a complete Ctrl+Shift chord never toggles");
    full_chord.on_shift_down();
    check(full_chord.on_shift_up(), "a plain Shift tap after a chord still toggles");
}

void test_engine() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-engine.tsv";
    write_sample_tsv(path);
    piinput::Engine engine;
    engine.load_lexicon(path);

    const auto full = engine.query("jisuanji", "full", 10U);
    check(!full.empty() && full.front().word == "计算机", "Engine full pinyin query");

    const auto flypy = engine.query("jisrji", "flypy", 10U);
    check(!flypy.empty() && flypy.front().word == "计算机", "Engine Flypy query");

    const auto ambiguous = engine.query("xian", "full", 10U);
    check(std::any_of(ambiguous.begin(), ambiguous.end(), [](const auto& candidate) {
        return candidate.word == "西安";
    }), "Engine queries alternate pinyin segmentation");

    // 我想 and 学习协议 are both real entries, so the whole input is covered by
    // joining them rather than by offering only the first two syllables.
    const auto sentence = engine.query("woxiangxuexixieyi", "full", 10U);
    check(!sentence.empty() && sentence.front().word == "我想学习协议",
        "Long input is covered by joining the real words it contains");
    check(std::any_of(sentence.begin(), sentence.end(), [](const auto& candidate) {
        return candidate.word == "我想";
    }), "The shorter real prefix word stays available below the join");
    check(std::all_of(sentence.begin(), sentence.end(), [](const auto& candidate) {
        return candidate.evidence.kind != piinput::CandidateKind::decoded_sentence ||
            (candidate.evidence.single_character_tokens == 0U &&
                candidate.evidence.word_count >= 2U &&
                candidate.evidence.covers_all_input);
    }), "Joins use only real multi-character words and always cover the input");

    std::filesystem::remove(path);
}

void test_long_sentence_composition_uses_canonical_pinyin_for_every_schema() {
    const auto path =
        std::filesystem::temp_directory_path() / "piinput-long-sentence-composition.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "促使\tcu'shi\t305195\n"
               << "我们\two'men\t509405\n"
               << "去\tqu\t7182400\n"
               << "去死\tqu'si\t25955\n"
               << "思考\tsi'kao\t500379\n"
               << "靠\tkao\t100000\n"
               << "考\tkao\t90000\n"
               << "看见\tkan'jian\t500334\n"
               << "和尚\the'shang\t208485\n"
               << "合上\the'shang\t30635\n"
               << "河上\the'shang\t26930\n"
               << "的\tde\t76938354\n"
               << "那艘\tna'sou\t2222\n"
               << "传\tchuan\t1034638\n"
               << "穿\tchuan\t634742\n"
               << "船\tchuan\t345686\n"
               << "我们就\two'men'jiu\t393235\n"
               << "把这个\tba'zhe'ge\t105570\n"
               << "体验\tti'yan\t500407\n"
               << "作为\tzuo'wei\t502525\n"
               << "作为家\tzuo'wei'jia\t236\n"
               << "记忆\tji'yi\t500274\n"
               << "储存起来\tchu'cun'qi'lai\t4040\n";
    }

    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto verify = [&](const std::string& full, const std::string& flypy,
                            const std::string& canonical, const std::string& target) {
        check(contains_canonical(engine.decode(full, "full", 32U), canonical),
            "standard full pinyin retains the declared sentence reading");
        check(contains_canonical(engine.decode(flypy, "flypy", 32U), canonical),
            "Flypy converts to the same canonical sentence reading");
        for (const auto& [input, schema] :
             {std::pair{full, std::string{"full"}},
              std::pair{flypy, std::string{"flypy"}}}) {
            const auto candidates = engine.query(input, schema, 30U);
            check(std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                    return candidate.word == target && candidate.evidence.covers_all_input;
                }),
                schema + " keeps the complete sentence candidate: " + target);
        }
    };

    verify("cu'shi'wo'men'qu'si'kao", "cuuiwomfqusikc",
        "cu'shi'wo'men'qu'si'kao", "促使我们去思考");
    verify("kan'jian'he'shang'de'na'sou'chuan", "kjjmheuhdenaszir",
        "kan'jian'he'shang'de'na'sou'chuan", "看见河上的那艘船");
    verify("wo'men'jiu'ba'zhe'ge'ti'yan'zuo'wei'ji'yi'chu'cun'qi'lai",
        "womfjqbavegetiyjzowwjiyiiucyqild",
        "wo'men'jiu'ba'zhe'ge'ti'yan'zuo'wei'ji'yi'chu'cun'qi'lai",
        "我们就把这个体验作为记忆储存起来");

    std::filesystem::remove(path);
}


void test_builtin_base_lexicon() {
    const auto path = std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "base_lexicon.tsv";
    piinput::Engine engine;
    engine.load_lexicon(path);
    check(engine.entry_count() >= 250U, "Built-in starter lexicon size");

    const auto full = engine.query("jisuanji", "full", 10U);
    check(!full.empty() && full.front().word == "计算机",
        "Built-in lexicon supports full-pinyin 计算机");

    const auto flypy = engine.query("jisrji", "flypy", 10U);
    check(!flypy.empty() && flypy.front().word == "计算机",
        "Built-in lexicon supports Flypy 计算机");

    const auto input_method = engine.query("uurufa", "flypy", 10U);
    check(!input_method.empty() && input_method.front().word == "输入法",
        "Built-in lexicon supports Flypy 输入法");

    verify_candidate_table(engine, "xiaohe_candidates.tsv", "flypy");
    verify_candidate_table(engine, "full_pinyin_candidates.tsv", "full");
    verify_incremental_candidates(engine);
}

void test_external_dictionary(const std::filesystem::path& path) {
    piinput::Engine engine;
    engine.load_lexicon(path);
    check(engine.entry_count() >= 10000U, "External dictionary has useful coverage");
    verify_candidate_table(engine, "xiaohe_candidates.tsv", "flypy");
    verify_candidate_table(engine, "full_pinyin_candidates.tsv", "full");
    verify_incremental_candidates(engine);
    verify_incremental_candidates(engine, "incremental_join_cases.tsv");
    verify_incremental_candidates(engine, "simplified_pinyin_candidates.tsv");
    verify_core_input_cases(engine);

    for (const auto* input : {"yuwh", "yvwh"}) {
        const auto candidates = engine.query(input, "flypy", 10U);
        check(!candidates.empty() && candidates.front().word == "欲望",
            std::string("External dictionary resolves ") + input + " to 欲望");
    }
}

void test_candidate_order_is_deterministic() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-stable-ranking.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "先\txian\t100\n"
               << "先\txi'an\t100\n"
               << "西安\txi'an\t100\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto baseline = engine.query("xian", "full", 10U);
    check(baseline.size() == 2U,
        "Stable ranking deduplicates identical display text across pronunciations");
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto repeated = engine.query("xian", "full", 10U);
        check(repeated.size() == baseline.size(), "Repeated candidate count is stable");
        for (std::size_t index = 0U; index < (std::min)(repeated.size(), baseline.size()); ++index) {
            check(repeated[index].word == baseline[index].word && repeated[index].pinyin == baseline[index].pinyin,
                "Repeated candidate order is stable");
        }
    }
    std::filesystem::remove(path);
}

void test_exact_phrase_beats_character_composition() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-exact-phrase.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "感\tgan\t40000\n"
               << "干\tgan\t50000\n"
               << "觉\tjue\t40000\n"
               << "感觉\tgan'jue\t60000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto candidates = engine.query("gjjt", "flypy", 5U);
    check(!candidates.empty() && candidates.front().word == "感觉",
        "Exact phrase ranks above higher aggregate character composition");
    std::filesystem::remove(path);
}

void test_symbols() {
    piinput::SymbolIndex index;
    const auto path = std::filesystem::path(PIINPUT_SOURCE_DIR) / "data" / "symbols.tsv";
    index.load_tsv(path);
    check(index.entry_count() >= 100U, "Built-in symbol table size");
    const auto celsius = index.search("sheshidu", 10U);
    check(!celsius.empty() && celsius.front().symbol == "℃", "Pinyin symbol search");
    const auto copyright = index.search("copyright", 10U);
    check(!copyright.empty() && copyright.front().symbol == "©", "English symbol search");

    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::size_t checked = 0U;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#' || line.starts_with("symbol\tcategory")) continue;
        std::stringstream stream(line);
        std::vector<std::string> fields;
        for (std::string field; std::getline(stream, field, '\t');) fields.push_back(field);
        check(fields.size() >= 4U, "Every symbol row has four searchable fields");
        if (fields.size() < 4U) continue;
        const auto results = index.search(fields[0], index.entry_count());
        check(std::any_of(results.begin(), results.end(), [&](const auto& candidate) {
            return candidate.symbol == fields[0];
        }), "Every built-in symbol is searchable by the symbol itself");
        ++checked;
    }
    check(checked == index.entry_count(), "Every loaded symbol row is covered by search tests");
}

void test_punctuation() {
    piinput::PunctuationTransformer transformer;
    for (const auto& row : read_test_table("punctuation_cases.tsv")) {
        check(row.size() == 4U, "punctuation row has four columns");
        if (row.size() != 4U || row[0].size() != 1U) {
            continue;
        }
        const char key = row[0].front();
        const bool shift = row[1] == "1";
        transformer.reset_quotes();
        check(transformer.transform(key, piinput::PunctuationMode::chinese, shift) == row[2],
            "Chinese punctuation mapping for " + row[0] + (shift ? " shifted" : ""));
        transformer.reset_quotes();
        check(transformer.transform(key, piinput::PunctuationMode::english, shift) == row[3],
            "English punctuation passthrough for " + row[0] + (shift ? " shifted" : ""));
    }
    transformer.reset_quotes();
    check(transformer.transform('\'', piinput::PunctuationMode::chinese, true) == "“", "Opening double quote");
    check(transformer.transform('\'', piinput::PunctuationMode::chinese, true) == "”", "Closing double quote");
    check(transformer.transform('\'', piinput::PunctuationMode::chinese, false) == "‘", "Opening single quote");
    check(transformer.transform('\'', piinput::PunctuationMode::chinese, false) == "’", "Closing single quote");
    check(transformer.transform('[', piinput::PunctuationMode::chinese, true,
              piinput::PunctuationBracketStyle::sogou) == "{",
        "Sogou shifted left bracket stays ASCII brace");
    check(transformer.transform(']', piinput::PunctuationMode::chinese, true,
              piinput::PunctuationBracketStyle::wechat) == "」",
        "WeChat shifted right bracket uses a corner quote");
}

void test_smart_punctuation() {
    const piinput::SmartPunctuationEngine engine;
    const auto decide = [&](const char symbol, const std::string_view left,
                             const std::string_view right = {},
                             const bool composing = false) {
        return engine.decide({symbol, left, right, composing});
    };

    const auto expect = [&](const char symbol,
                            const std::string_view left,
                            const std::string_view right,
                            const piinput::SmartPunctuationAction action,
                            const std::string_view rule,
                            const std::string_view context,
                            const std::string& message) {
        const auto decision = decide(symbol, left, right);
        check(decision.action == action && decision.rule_id == rule &&
                decision.context_type == context,
            message);
    };

    check(decide('/', "2").action == piinput::SmartPunctuationAction::literal,
        "Physical slash stays ASCII for fractions");

    expect('.', "1", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-DECIMAL-LIST", "SEQUENCE", "Line-leading 1. is an ASCII list marker");
    expect('.', "  12", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-DECIMAL-LIST", "SEQUENCE", "Indented numeric list marker stays ASCII");
    expect('.', "版本v1.0", "1", piinput::SmartPunctuationAction::literal,
        "PUNC-DOT-VERSION", "VERSION", "Version segments keep an ASCII dot");
    expect('.', "服务器192.168.1", "1", piinput::SmartPunctuationAction::literal,
        "PUNC-DOT-IPV4", "IPV4", "A valid IPv4 separator stays ASCII");
    expect('.', "价格3", "14", piinput::SmartPunctuationAction::literal,
        "PUNC-DOT-DECIMAL", "DECIMAL", "A decimal point stays ASCII");
    expect('.', "文件README", "md", piinput::SmartPunctuationAction::literal,
        "PUNC-FILENAME", "FILENAME", "A filename extension separator stays ASCII");
    expect('.', "访问https://example", "com?a=1", piinput::SmartPunctuationAction::literal,
        "PUNC-URL", "URL", "A URL dot stays ASCII");
    expect('.', "邮箱abc.test+dev@example", "com", piinput::SmartPunctuationAction::literal,
        "PUNC-EMAIL", "EMAIL", "An email domain dot stays ASCII");
    expect('.', "路径C:/folder/file", "txt", piinput::SmartPunctuationAction::literal,
        "PUNC-PATH", "PATH", "A path dot stays ASCII");
    expect('.', "版本v1.0.1", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-DOT-AFTER-DIGIT", "SEQUENCE",
        "The first period after a digit remains ASCII immediately");
    expect('.', "1.", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT",
        "The second period after a numeric ASCII dot becomes a Chinese full stop");
    expect('.', "已有1.", "2", piinput::SmartPunctuationAction::transform,
        "PUNC-NUMERIC-INVALID", "CHINESE_TEXT", "Malformed numeric punctuation is not protected");

    expect(':', "12", "23", piinput::SmartPunctuationAction::literal,
        "PUNC-COLON-TIME", "TIME", "A valid time colon stays ASCII");
    expect(':', "24", "99", piinput::SmartPunctuationAction::literal,
        "PUNC-COLON-RATIO", "RATIO", "Out-of-range time syntax remains a valid ratio");
    expect(':', "BIT[31", "16]", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-INFIX", "TECHNICAL", "A bit-field colon stays ASCII");
    expect(':', "key", "value", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-INFIX", "TECHNICAL", "A configuration colon stays ASCII");
    expect(':', "https", "//example.com", piinput::SmartPunctuationAction::literal,
        "PUNC-URL", "URL", "A URL scheme colon stays ASCII");
    expect(':', "路径C", "/Windows", piinput::SmartPunctuationAction::literal,
        "PUNC-PATH", "PATH", "A Windows drive colon stays ASCII");
    expect(':', "共有12项", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A Chinese lead-in colon stays Chinese");

    expect(',', "价格1", "299.50", piinput::SmartPunctuationAction::literal,
        "PUNC-COMMA-THOUSANDS", "NUMBER", "A valid thousands separator stays ASCII");
    expect(',', "数字1", "2", piinput::SmartPunctuationAction::transform,
        "PUNC-NUMERIC-INVALID", "CHINESE_TEXT", "An invalid one-digit group becomes Chinese");
    expect(',', "a", "b", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-INFIX", "TECHNICAL", "A CSV/code comma stays ASCII");
    expect(',', "金额1", {}, piinput::SmartPunctuationAction::provisional,
        "PUNC-COMMA-GROUP-PENDING", "AMBIGUOUS",
        "A trailing numeric comma waits for a complete three-digit group");
    expect(',', "你好", "明天", piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A Chinese prose comma stays Chinese");

    expect('?', "https://example.com", "a=1&b=2", piinput::SmartPunctuationAction::literal,
        "PUNC-URL", "URL", "A URL query marker stays ASCII");
    expect('?', "https://example.com 你去吗", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "Leaving a URL immediately restores Chinese punctuation");
    expect('.', "联系abc@example.com 已完成", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "Leaving an email immediately restores Chinese punctuation");
    expect('.', "路径C:/folder/file.txt 已完成", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "Leaving a path immediately restores Chinese punctuation");
    expect('!', "func(x) 已完成", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "Leaving a code token immediately restores Chinese punctuation");
    expect('?', "你去吗", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A Chinese question mark stays Chinese");
    expect('[', "BIT", "31:16]", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-BOUNDARY", "TECHNICAL", "A technical opening bracket stays ASCII");
    expect(']', "BIT[31:16", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-BOUNDARY", "TECHNICAL", "A technical closing bracket stays ASCII");
    expect('(', "func", "x)", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-BOUNDARY", "TECHNICAL", "A function parenthesis stays ASCII");
    expect('_', "file", "name", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-BOUNDARY", "TECHNICAL", "An identifier underscore stays ASCII");
    expect('(', "中文", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A Chinese opening parenthesis remains Chinese");
    expect('(', "第1", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A digit alone does not make a parenthesis technical");
    expect('[', "第1", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A digit alone does not make a bracket technical");
    expect('!', "https://example.com/path", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-URL", "URL", "A URL exclamation mark stays ASCII");
    expect('!', "完成了", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A Chinese exclamation mark remains Chinese");
    expect('!', "", "flag", piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-PREFIX", "TECHNICAL", "A command-style bang prefix stays ASCII");
    expect('!', "", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A standalone leading exclamation stays Chinese");
    expect('!', "版本1", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A digit does not make exclamation numeric punctuation");
    expect('?', "版本1", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-CHINESE", "CHINESE_TEXT", "A digit does not make question mark numeric punctuation");
    expect('"', "key=", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-QUOTE", "TECHNICAL", "A quote after assignment stays ASCII");
    expect('"', "key=\"value", {}, piinput::SmartPunctuationAction::literal,
        "PUNC-TECHNICAL-QUOTE", "TECHNICAL", "A matching technical closing quote stays ASCII");
    expect('"', "他说", {}, piinput::SmartPunctuationAction::transform,
        "PUNC-DEFAULT", "CHINESE_TEXT", "A Chinese prose quote remains owned by quote mapping");

    check(decide('.', "1", {}, true).action == piinput::SmartPunctuationAction::transform,
        "Active pinyin composition remains owned by the Host punctuation path");
    check(decide('.', "版本v1").action == piinput::SmartPunctuationAction::literal,
        "A trailing digit makes the first period immediately ASCII");
    check(decide(':', "12").chinese_text == "：",
        "Provisional colon carries its Chinese resolution");
    check(engine.resolve_provisional('.', '0', "PUNC-NUMERIC-PENDING").keep_ascii,
        "A digit resolves a provisional version dot to ASCII");
    check(!engine.resolve_provisional('.', 'a', "PUNC-NUMERIC-PENDING").keep_ascii &&
            engine.resolve_provisional('.', 'a', "PUNC-NUMERIC-PENDING").chinese_text == "。",
        "A prose character resolves a provisional period to Chinese");
    check(engine.resolve_provisional(':', '2', "PUNC-NUMERIC-PENDING").keep_ascii,
        "A digit resolves a provisional time colon to ASCII");
    check(!engine.resolve_provisional(':', '\0', "PUNC-NUMERIC-PENDING").keep_ascii &&
            engine.resolve_provisional(':', '\0', "PUNC-NUMERIC-PENDING").chinese_text == "：",
        "A boundary resolves a provisional colon to Chinese");
    check(engine.resolve_provisional(',', '2', "PUNC-COMMA-GROUP-PENDING").continue_provisional,
        "A grouped comma waits after the first following digit");
    check(engine.resolve_provisional(',', '9', "PUNC-COMMA-GROUP-PENDING", "2").continue_provisional,
        "A grouped comma waits after the second following digit");
    const auto grouped_resolution =
        engine.resolve_provisional(',', '9', "PUNC-COMMA-GROUP-PENDING", "29");
    check(grouped_resolution.keep_ascii && !grouped_resolution.continue_provisional,
        "A grouped comma commits only after three following digits");
    check(!engine.resolve_provisional(',', 'a', "PUNC-COMMA-GROUP-PENDING", "29").keep_ascii,
        "An incomplete grouped comma resolves to Chinese before a non-digit");
}



void test_user_model() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-user-model.tsv";
    piinput::UserModel model;
    model.record_selection("ji'suan'ji", "计蒜机");
    check(model.score_adjustment("ji'suan'ji", "计蒜机") > 0, "User model score adjustment");
    model.save(path);

    piinput::UserModel loaded;
    loaded.load(path);
    check(loaded.entry_count() == 1U, "User model persistence");
    check(loaded.score_adjustment("ji'suan'ji", "计蒜机") > 0, "Loaded user model adjustment");
    loaded.pin("hou'xuan'kuang", "候选框");
    check(loaded.is_pinned("hou'xuan'kuang", "候选框"), "Pinned candidate state");
    check(loaded.score_adjustment("hou'xuan'kuang", "候选框") >= 1'000'000'000,
        "Pinned candidate has deterministic first-place adjustment");
    loaded.suppress("hou'xuan'kuang", "候选矿");
    check(loaded.is_suppressed("hou'xuan'kuang", "候选矿"), "Suppressed candidate state");
    loaded.save(path);
    piinput::UserModel roundtrip;
    roundtrip.load(path);
    check(roundtrip.is_pinned("hou'xuan'kuang", "候选框"), "Pinned state persists");
    check(roundtrip.is_suppressed("hou'xuan'kuang", "候选矿"), "Suppressed state persists");
    loaded.remove("ji'suan'ji", "计蒜机");
    check(loaded.entry_count() == 2U, "User model remove preserves unrelated controls");
    std::filesystem::remove(path);
}

void test_session() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-test-session.tsv";
    write_sample_tsv(path);
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::ImeSession session(engine, "full");
    session.set_input("jisuanji");
    const auto first_generation = session.snapshot().generation;
    check(!session.snapshot().candidates.empty(), "Session candidate snapshot");
    const auto first_id = session.snapshot().candidates.front().id;

    session.set_input("jisuanji");
    check(session.snapshot().generation == first_generation, "Unchanged input keeps candidate generation stable");

    session.move_left();
    const auto before_failed_edit = session.snapshot();
    session.backspace();
    check(session.snapshot().generation > first_generation, "Middle edit creates a new snapshot generation");
    check(!session.choose(first_id).accepted, "Stale candidate ID is rejected");
    session.restore(before_failed_edit);
    check(session.snapshot().input == before_failed_edit.input &&
            session.snapshot().caret == before_failed_edit.caret &&
            session.snapshot().generation == before_failed_edit.generation,
        "A failed host edit can restore the exact Chinese composition snapshot");

    session.set_input("jisuanji");
    const auto valid_id = session.snapshot().candidates.front().id;
    const auto selected = session.choose(valid_id);
    check(selected.accepted && selected.commit_text == std::optional<std::string>("计算机"),
        "Current candidate ID selects expected word");
    check(session.snapshot().input.empty(), "Choosing a candidate clears composition");
    std::filesystem::remove(path);

    const auto paging_path = std::filesystem::temp_directory_path() / "piinput-test-session-paging.tsv";
    {
        std::ofstream output(paging_path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n";
        for (int index = 0; index < 12; ++index) {
            output << "候选" << index << "\thou\t" << (100 - index) << '\n';
        }
    }
    piinput::Engine paging_engine;
    paging_engine.load_lexicon(paging_path);
    piinput::ImeSession paging_session(paging_engine, "full");
    paging_session.set_input("hou");
    check(paging_session.snapshot().candidates.size() == 12U,
        "Session retains more than ten candidates for paging");
    std::filesystem::remove(paging_path);
}

void test_invalid_scel() {
    piinput::ScelParser parser;
    bool threw = false;
    try {
        (void)parser.parse_bytes({0U, 1U, 2U});
    } catch (const piinput::ScelError&) {
        threw = true;
    }
    check(threw, "Invalid SCEL must throw ScelError");
}

void test_uploaded_scel(const std::filesystem::path& electronics, const std::filesystem::path& computer) {
    piinput::ScelParser parser;

    const auto electronic_dictionary = parser.parse_file(electronics);
    check(electronic_dictionary.metadata.title == "电子词汇大全【官方推荐】", "Electronics title");
    check(electronic_dictionary.metadata.category == "电子工程", "Electronics category");
    check(electronic_dictionary.pinyin_table.size() == 413U, "Electronics pinyin count");
    check(electronic_dictionary.entries.size() == 5596U, "Electronics entry count");

    const auto computer_dictionary = parser.parse_file(computer);
    check(computer_dictionary.metadata.title == "计算机词汇大全【官方推荐】", "Computer title");
    check(computer_dictionary.metadata.category == "计算机科技", "Computer category");
    check(computer_dictionary.pinyin_table.size() == 413U, "Computer pinyin count");
    check(computer_dictionary.entries.size() == 10300U, "Computer entry count");
}

int run(const std::vector<std::string>& arguments) {
    try {
        test_lexicon();
        test_binary_lexicon();
        test_dictionary_builder();
        test_candidate_paging();
        test_query_never_exceeds_the_requested_limit();
    test_engine_copy_keeps_its_configuration();
    test_short_readings_are_filled_with_prefix_words();
    test_date_and_time_candidates();
    test_symbol_shortcuts_reach_candidates_in_every_schema();
    test_tool_shortcuts_are_always_candidate_two();
    test_shift_toggle_state();
        test_pinyin();
        test_shuangpin();
        test_engine();
        test_long_sentence_composition_uses_canonical_pinyin_for_every_schema();
        test_candidate_order_is_deterministic();
        test_exact_phrase_beats_character_composition();
        test_builtin_base_lexicon();
        test_symbols();
        test_punctuation();
        test_smart_punctuation();
        test_user_model();
        test_session();
        test_invalid_scel();
        if (arguments.size() == 3U && arguments[1] != "--lexicon") {
            test_uploaded_scel(
                piinput::path_from_utf8(arguments[1]),
                piinput::path_from_utf8(arguments[2]));
        } else if (arguments.size() == 3U && arguments[1] == "--lexicon") {
            test_external_dictionary(piinput::path_from_utf8(arguments[2]));
        }
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }

    if (failures == 0) {
        std::cout << "All PiInput core tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.push_back(piinput::wide_to_utf8(argv[index]));
    }
    return run(arguments);
}
#else
int main(const int argc, char* argv[]) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
