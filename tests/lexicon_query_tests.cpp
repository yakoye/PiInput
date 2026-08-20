#include "piinput/binary_lexicon.h"
#include "piinput/engine.h"
#include "piinput/lexicon.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    try {
        const auto fixture = std::filesystem::path(PIINPUT_SOURCE_DIR) /
            "tests/data/dictionary_builder/lookup.tsv";

        piinput::DevLexicon tsv;
        tsv.load_tsv(fixture);
        const auto tsv_word = tsv.query_word("黄河入海流", 10U);
        check(tsv_word.size() == 1U, "TSV reverse lookup returns exact word only");
        check(tsv_word[0].pinyin == "huang'he'ru'hai'liu",
              "TSV reverse lookup returns pronunciation");

        const auto binary_path = std::filesystem::temp_directory_path() /
            "piinput-lexicon-query-test.lex";
        piinput::compile_tsv_to_binary(fixture, binary_path);
        piinput::BinaryLexicon binary;
        binary.load(binary_path);
        const auto binary_word = binary.query_word("黄河入海流", 10U);
        check(binary_word.size() == tsv_word.size(), "binary and TSV lookup sizes match");
        check(binary_word[0].pinyin == tsv_word[0].pinyin,
              "binary and TSV lookup pronunciations match");

        piinput::Engine engine;
        engine.load_lexicon(binary_path);
        const auto engine_word = engine.lookup_word("黄河入海流", 10U);
        check(engine_word.size() == 1U, "engine exposes indexed word lookup");
        check(engine_word[0].word == "黄河入海流", "engine lookup preserves word");
        std::error_code ignored;

        const auto lexical_path = std::filesystem::temp_directory_path() /
            "piinput-lexical-candidate-test.tsv";
        {
            std::ofstream output(lexical_path, std::ios::binary | std::ios::trunc);
            output << "word\tpinyin\tweight\n"
                   << "边框\tbian'kuang\t100\n"
                   << "编筐\tbian'kuang\t10\n"
                   << "边\tbian\t90\n"
                   << "便\tbian\t80\n"
                   << "变\tbian\t70\n"
                   << "狂\tkuang\t60\n"
                   << "需要\txu'yao\t100\n"
                   << "加进去\tjia'jin'qu\t100\n"
                   << "加紧\tjia'jin\t90\n"
                   << "加进\tjia'jin\t80\n"
                   << "家\tjia\t70\n";
        }
        piinput::Engine lexical_engine;
        lexical_engine.load_lexicon(lexical_path);
        const auto border = lexical_engine.query("biankuang", "full", 20U);
        check(border.size() >= 5U && border[0].word == "边框" &&
                border[1].word == "编筐",
            "all exact real words precede single-character fallback");
        check(border[2].evidence.kind == piinput::CandidateKind::single_character,
            "the first single character follows all matching words");
        check(std::none_of(border.begin(), border.end(), [](const auto& candidate) {
                return candidate.word == "便狂" || candidate.word == "边狂" ||
                    candidate.evidence.kind == piinput::CandidateKind::decoded_sentence;
            }),
            "normal queries never manufacture non-lexical words");

        const auto add = lexical_engine.query("jiajinqu", "full", 20U);
        check(add.size() >= 4U && add[0].word == "加进去" &&
                add[1].word == "加紧" && add[2].word == "加进" &&
                add[3].evidence.kind == piinput::CandidateKind::single_character,
            "full real words, shorter real prefixes, then first-syllable characters are layered");

        const auto long_input = lexical_engine.query(
            "biankuangxuyao", "full", 20U);
        check(!long_input.empty() && long_input.front().word == "边框需要",
            "two real entries are joined to cover the whole input");
        check(std::any_of(long_input.begin(), long_input.end(), [](const auto& candidate) {
                return candidate.word == "边框";
            }),
            "the longest real lexical prefix stays available below the join");
        check(std::all_of(long_input.begin(), long_input.end(), [](const auto& candidate) {
                return candidate.evidence.kind !=
                        piinput::CandidateKind::decoded_sentence ||
                    candidate.evidence.single_character_tokens == 0U;
            }),
            "a join never links single characters");
        std::filesystem::remove(lexical_path, ignored);
        std::filesystem::remove(binary_path, ignored);

        std::cout << "Lexicon query tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Lexicon query test failure: " << error.what() << '\n';
        return 1;
    }
}
