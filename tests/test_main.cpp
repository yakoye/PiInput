#include "liteime/binary_lexicon.h"
#include "liteime/dictionary_builder.h"
#include "liteime/engine.h"
#include "liteime/lexicon.h"
#include "liteime/pinyin.h"
#include "liteime/punctuation.h"
#include "liteime/scel_parser.h"
#include "liteime/session.h"
#include "liteime/shuangpin.h"
#include "liteime/symbols.h"
#include "liteime/utf.h"
#include "liteime/user_model.h"
#include "liteime/windows_compat.h"

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
    const std::vector<liteime::PinyinSegmentation>& segmentations,
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
    const auto path = std::filesystem::path(LITEIME_SOURCE_DIR) / "tests" / "data" / name;
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
    liteime::Engine& engine,
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
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-lexicon.tsv";
    write_sample_tsv(path);

    liteime::DevLexicon lexicon;
    lexicon.load_tsv(path);
    check(lexicon.entry_count() == 12U, "TSV lexicon entry count");
    const auto results = lexicon.query_exact("ji'suan'ji", 10U);
    check(results.size() == 2U, "Exact pinyin query result count");
    check(!results.empty() && results.front().word == "计算机", "Candidate ordering by weight");
    std::filesystem::remove(path);
}

void test_binary_lexicon() {
    const auto temp = std::filesystem::temp_directory_path();
    const auto tsv = temp / "liteime-test-binary-source.tsv";
    const auto lex = temp / "liteime-test-binary.lex";
    write_sample_tsv(tsv);
    {
        std::ofstream append(tsv, std::ios::binary | std::ios::app);
        append << "计算机\tji'suan'ji\t20000\n";
    }
    liteime::compile_tsv_to_binary(tsv, lex);
    check(liteime::is_binary_lexicon(lex), "Binary lexicon magic detection");

    liteime::BinaryLexicon binary;
    binary.load(lex);
    check(binary.entry_count() == 12U, "Binary lexicon entry count");
    const auto results = binary.query_exact("shu'ru'fa", 5U);
    check(results.size() == 1U && results.front().word == "输入法", "Binary lexicon query");
    const auto deduplicated = binary.query_exact("ji'suan'ji", 5U);
    check(deduplicated.size() == 2U, "Binary lexicon de-duplicates identical word+pinyin pairs");
    check(!deduplicated.empty() && deduplicated.front().word == "计算机" && deduplicated.front().weight == 20000U,
        "Binary lexicon keeps the highest duplicate weight");
    std::filesystem::remove(tsv);
    std::filesystem::remove(lex);
}

void test_pinyin() {
    liteime::PinyinSegmenter segmenter;
    check(segmenter.is_syllable("xian"), "xian is a valid syllable");
    check(segmenter.is_valid_prefix("zhua"), "zhua is a valid prefix");
    check(!segmenter.is_valid_prefix("qqq"), "qqq is not a valid prefix");
    check(liteime::PinyinSegmenter::normalize("nü'e") == "nv'e", "Normalize ü to v");

    const auto computer = segmenter.segment("jisuanji", 8U);
    check(contains_canonical(computer, "ji'suan'ji"), "Continuous full pinyin segmentation");

    const auto ambiguous = segmenter.segment("xian", 8U);
    check(contains_canonical(ambiguous, "xian"), "Ambiguous pinyin includes xian");
    check(contains_canonical(ambiguous, "xi'an"), "Ambiguous pinyin includes xi'an");

    const auto manual = segmenter.segment("xi'an", 8U);
    check(manual.size() == 1U && manual.front().canonical == "xi'an", "Manual apostrophe boundary");

    for (const auto& syllable : liteime::PinyinSegmenter::standard_syllables()) {
        const auto decoded = segmenter.segment(syllable, 32U);
        check(contains_canonical(decoded, syllable), "Full pinyin accepts standard syllable " + syllable);
    }
}

void test_shuangpin() {
    liteime::ShuangpinDecoder decoder;
    check(decoder.has_scheme("flypy"), "Flypy scheme exists");
    check(decoder.has_scheme("natural"), "Natural scheme exists");
    check(decoder.has_scheme("mspy"), "Microsoft scheme exists");
    check(decoder.has_scheme("abc"), "ABC scheme exists");

    const auto flypy = decoder.decode("flypy", "jisrji", 8U);
    check(contains_canonical(flypy, "ji'suan'ji"), "Flypy decodes 计算机 pinyin");

    const auto flypy_input_method = decoder.decode("flypy", "uurufa", 8U);
    check(contains_canonical(flypy_input_method, "shu'ru'fa"), "Flypy decodes 输入法 pinyin");

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
    for (const auto& syllable : liteime::PinyinSegmenter::standard_syllables()) {
        check(reachable.contains(syllable), "Flypy can encode standard syllable " + syllable);
    }
    check(decoder.decode("flypy", "gjj", 8U).empty(), "Incomplete Flypy pair has no premature candidate");
    check(decoder.decode("flypy", "qz", 8U).empty(), "Invalid Flypy code is rejected");
}

void test_dictionary_builder() {
    const auto root = std::filesystem::path(LITEIME_SOURCE_DIR) / "tests" / "data" / "dictionary_builder";
    const auto characters = liteime::read_dictionary_source(
        root / "pinyin_data.txt", liteime::DictionarySourceFormat::pinyin_data, 1000U);
    check(characters.size() == 2U, "Dictionary builder reads pinyin-data characters");
    check(characters.size() >= 2U && characters[0].pinyin == "wo" && characters[1].pinyin == "ai",
        "Dictionary builder removes character tones");

    const auto phrases = liteime::read_dictionary_source(
        root / "phrases.txt", liteime::DictionarySourceFormat::phrase_pinyin_data, 2000U);
    check(!phrases.empty() && phrases.front().word == "感觉" && phrases.front().pinyin == "gan'jue",
        "Dictionary builder reads phrase-pinyin-data");

    const auto rime = liteime::read_dictionary_source(
        root / "rime.dict.yaml", liteime::DictionarySourceFormat::rime_yaml, 3000U);
    check(rime.size() == 2U && rime.front().word == "感觉" && rime.front().weight == 200U,
        "Dictionary builder reads Rime YAML entries and weights");

    const auto output = std::filesystem::temp_directory_path() / "liteime-dictionary-builder-test.tsv";
    std::vector<liteime::LexiconCandidate> combined = characters;
    combined.insert(combined.end(), phrases.begin(), phrases.end());
    combined.insert(combined.end(), rime.begin(), rime.end());
    liteime::write_dictionary_tsv(output, std::move(combined));
    liteime::DevLexicon lexicon;
    lexicon.load_tsv(output);
    const auto feeling = lexicon.query_exact("gan'jue", 10U);
    check(feeling.size() == 1U && feeling.front().word == "感觉" && feeling.front().weight == 2000U,
        "Dictionary builder de-duplicates and keeps the highest weight");
    std::filesystem::remove(output);
}

void test_engine() {
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-engine.tsv";
    write_sample_tsv(path);
    liteime::Engine engine;
    engine.load_lexicon(path);

    const auto full = engine.query("jisuanji", "full", 10U);
    check(!full.empty() && full.front().word == "计算机", "Engine full pinyin query");

    const auto flypy = engine.query("jisrji", "flypy", 10U);
    check(!flypy.empty() && flypy.front().word == "计算机", "Engine Flypy query");

    const auto ambiguous = engine.query("xian", "full", 10U);
    check(std::any_of(ambiguous.begin(), ambiguous.end(), [](const auto& candidate) {
        return candidate.word == "西安";
    }), "Engine queries alternate pinyin segmentation");

    const auto sentence = engine.query("woxiangxuexixieyi", "full", 10U);
    check(std::any_of(sentence.begin(), sentence.end(), [](const auto& candidate) {
        return candidate.word == "我想学习协议";
    }), "Sentence decoder combines multiple lexicon entries");

    std::filesystem::remove(path);
}


void test_builtin_base_lexicon() {
    const auto path = std::filesystem::path(LITEIME_SOURCE_DIR) / "data" / "base_lexicon.tsv";
    liteime::Engine engine;
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
}

void test_external_dictionary(const std::filesystem::path& path) {
    liteime::Engine engine;
    engine.load_lexicon(path);
    check(engine.entry_count() >= 10000U, "External dictionary has useful coverage");
    verify_candidate_table(engine, "xiaohe_candidates.tsv", "flypy");
    verify_candidate_table(engine, "full_pinyin_candidates.tsv", "full");
}

void test_candidate_order_is_deterministic() {
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-stable-ranking.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "先\txian\t100\n"
               << "先\txi'an\t100\n"
               << "西安\txi'an\t100\n";
    }
    liteime::Engine engine;
    engine.load_lexicon(path);
    const auto baseline = engine.query("xian", "full", 10U);
    check(baseline.size() == 3U, "Stable ranking fixture returns all candidates");
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
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-exact-phrase.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "感\tgan\t40000\n"
               << "干\tgan\t50000\n"
               << "觉\tjue\t40000\n"
               << "感觉\tgan'jue\t60000\n";
    }
    liteime::Engine engine;
    engine.load_lexicon(path);
    const auto candidates = engine.query("gjjt", "flypy", 5U);
    check(!candidates.empty() && candidates.front().word == "感觉",
        "Exact phrase ranks above higher aggregate character composition");
    std::filesystem::remove(path);
}

void test_symbols() {
    liteime::SymbolIndex index;
    index.load_tsv(std::filesystem::path(LITEIME_SOURCE_DIR) / "data" / "symbols.tsv");
    check(index.entry_count() >= 100U, "Built-in symbol table size");
    const auto celsius = index.search("sheshidu", 10U);
    check(!celsius.empty() && celsius.front().symbol == "℃", "Pinyin symbol search");
    const auto copyright = index.search("copyright", 10U);
    check(!copyright.empty() && copyright.front().symbol == "©", "English symbol search");
}

void test_punctuation() {
    liteime::PunctuationTransformer transformer;
    check(transformer.transform(',', liteime::PunctuationMode::chinese, false) == "，", "Chinese comma");
    check(transformer.transform('.', liteime::PunctuationMode::programmer, false) == ".", "Programmer period");
    check(transformer.transform('1', liteime::PunctuationMode::chinese, true) == "！", "Chinese exclamation");
    check(transformer.transform('\"', liteime::PunctuationMode::chinese, false) == "“", "Opening quote");
    check(transformer.transform('\"', liteime::PunctuationMode::chinese, false) == "”", "Closing quote");
}



void test_user_model() {
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-user-model.tsv";
    liteime::UserModel model;
    model.record_selection("ji'suan'ji", "计蒜机");
    check(model.score_adjustment("ji'suan'ji", "计蒜机") > 0, "User model score adjustment");
    model.save(path);

    liteime::UserModel loaded;
    loaded.load(path);
    check(loaded.entry_count() == 1U, "User model persistence");
    check(loaded.score_adjustment("ji'suan'ji", "计蒜机") > 0, "Loaded user model adjustment");
    loaded.remove("ji'suan'ji", "计蒜机");
    check(loaded.entry_count() == 0U, "User model remove");
    std::filesystem::remove(path);
}

void test_session() {
    const auto path = std::filesystem::temp_directory_path() / "liteime-test-session.tsv";
    write_sample_tsv(path);
    liteime::Engine engine;
    engine.load_lexicon(path);
    liteime::ImeSession session(engine, "full");
    session.set_input("jisuanji");
    const auto first_generation = session.snapshot().generation;
    check(!session.snapshot().candidates.empty(), "Session candidate snapshot");
    const auto first_id = session.snapshot().candidates.front().id;

    session.set_input("jisuanji");
    check(session.snapshot().generation == first_generation, "Unchanged input keeps candidate generation stable");

    session.move_left();
    session.backspace();
    check(session.snapshot().generation > first_generation, "Middle edit creates a new snapshot generation");
    check(!session.choose(first_id).has_value(), "Stale candidate ID is rejected");

    session.set_input("jisuanji");
    const auto valid_id = session.snapshot().candidates.front().id;
    const auto selected = session.choose(valid_id);
    check(selected.has_value() && *selected == "计算机", "Current candidate ID selects expected word");
    check(session.snapshot().input.empty(), "Choosing a candidate clears composition");
    std::filesystem::remove(path);
}

void test_invalid_scel() {
    liteime::ScelParser parser;
    bool threw = false;
    try {
        (void)parser.parse_bytes({0U, 1U, 2U});
    } catch (const liteime::ScelError&) {
        threw = true;
    }
    check(threw, "Invalid SCEL must throw ScelError");
}

void test_uploaded_scel(const std::filesystem::path& electronics, const std::filesystem::path& computer) {
    liteime::ScelParser parser;

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
        test_pinyin();
        test_shuangpin();
        test_engine();
        test_candidate_order_is_deterministic();
        test_exact_phrase_beats_character_composition();
        test_builtin_base_lexicon();
        test_symbols();
        test_punctuation();
        test_user_model();
        test_session();
        test_invalid_scel();
        if (arguments.size() == 3U && arguments[1] != "--lexicon") {
            test_uploaded_scel(
                liteime::path_from_utf8(arguments[1]),
                liteime::path_from_utf8(arguments[2]));
        } else if (arguments.size() == 3U && arguments[1] == "--lexicon") {
            test_external_dictionary(liteime::path_from_utf8(arguments[2]));
        }
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }

    if (failures == 0) {
        std::cout << "All LiteIME core tests passed.\n";
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
        arguments.push_back(liteime::wide_to_utf8(argv[index]));
    }
    return run(arguments);
}
#else
int main(const int argc, char* argv[]) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
