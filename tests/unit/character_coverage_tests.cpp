#include "piinput/engine.h"
#include "piinput/pinyin.h"
#include "piinput/shuangpin.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        if (failures <= 80) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

[[nodiscard]] std::vector<std::string> read_nonempty_lines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open character coverage input: " + path.string());
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (lines.empty() && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0U, 3U);
        }
        if (!line.empty() && line.front() != '#') {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

[[nodiscard]] bool contains_word(
    const std::vector<piinput::EngineCandidate>& candidates,
    const std::string& word) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.word == word;
    });
}

[[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
read_character_pronunciations(
    const std::filesystem::path& dictionary,
    const std::unordered_set<std::string>& required) {
    std::ifstream input(dictionary, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open dictionary TSV: " + dictionary.string());
    }
    std::unordered_map<std::string, std::vector<std::string>> result;
    std::string line;
    bool first = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (first) {
            first = false;
            if (line == "word\tpinyin\tweight") {
                continue;
            }
        }
        const auto first_tab = line.find('\t');
        const auto second_tab = first_tab == std::string::npos
            ? std::string::npos
            : line.find('\t', first_tab + 1U);
        if (first_tab == std::string::npos || second_tab == std::string::npos) {
            continue;
        }
        const std::string word = line.substr(0U, first_tab);
        if (!required.contains(word)) {
            continue;
        }
        const std::string pinyin = line.substr(first_tab + 1U, second_tab - first_tab - 1U);
        if (!pinyin.empty() && pinyin.find('\'') == std::string::npos) {
            auto& pronunciations = result[word];
            if (std::find(pronunciations.begin(), pronunciations.end(), pinyin) == pronunciations.end()) {
                pronunciations.push_back(pinyin);
            }
        }
    }
    return result;
}

}  // namespace

int main(const int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: piinput-character-coverage-tests <combined.tsv> <base.lex> "
                     "<3500.txt> <7000.txt> <xiaohe_codes.tsv>\n";
        return 2;
    }
    try {
        const auto common = read_nonempty_lines(argv[3]);
        const auto general = read_nonempty_lines(argv[4]);
        const auto legal_codes = read_nonempty_lines(argv[5]);
        check(common.size() == 3500U, "3500-character list has exactly 3500 rows");
        check(general.size() == 7000U, "7000-character list has exactly 7000 rows");
        check(legal_codes.size() == 406U, "Xiaohe inventory has exactly 406 rows");

        const std::unordered_set<std::string> common_set(common.begin(), common.end());
        const std::unordered_set<std::string> general_set(general.begin(), general.end());
        check(common_set.size() == common.size(), "3500-character list has no duplicates");
        check(general_set.size() == general.size(), "7000-character list has no duplicates");
        check(std::all_of(common.begin(), common.end(), [&](const auto& word) {
            return general_set.contains(word);
        }), "3500 common characters are a subset of the 7000 general characters");

        const auto pronunciations = read_character_pronunciations(argv[1], general_set);
        piinput::PinyinSegmenter pinyin;
        piinput::ShuangpinDecoder shuangpin;
        std::unordered_map<std::string, std::vector<std::string>> syllable_codes;
        for (const auto& code : legal_codes) {
            const auto syllables = shuangpin.syllables_for_code("flypy", code, false);
            check(!syllables.empty(), "Xiaohe coverage code decodes: " + code);
            for (const auto& syllable : syllables) {
                syllable_codes[syllable].push_back(code);
            }
        }

        piinput::Engine engine;
        engine.load_lexicon(argv[2]);
        const piinput::PinyinSettings unrestricted_query_settings;
        std::unordered_map<std::string, std::vector<piinput::EngineCandidate>> full_results;
        std::unordered_map<std::string, std::vector<piinput::EngineCandidate>> flypy_results;
        std::unordered_set<std::string> queried_codes;
        for (const auto& [word, readings] : pronunciations) {
            (void)word;
            for (const auto& syllable : readings) {
                if (!pinyin.is_syllable(syllable) || !syllable_codes.contains(syllable)) {
                    continue;
                }
                if (!full_results.contains(syllable)) {
                    full_results.emplace(syllable, engine.query(
                        syllable, "full", 4096U, unrestricted_query_settings));
                }
                for (const auto& code : syllable_codes.at(syllable)) {
                    if (queried_codes.insert(code).second) {
                        flypy_results.emplace(code, engine.query(
                            code, "flypy", 4096U, unrestricted_query_settings));
                    }
                }
            }
        }

        std::size_t common_full_hits = 0U;
        std::size_t common_flypy_hits = 0U;
        std::size_t general_full_hits = 0U;
        std::size_t general_flypy_hits = 0U;
        for (const auto& word : general) {
            const auto found = pronunciations.find(word);
            bool full_hit = false;
            bool flypy_hit = false;
            bool has_legal_full_reading = false;
            bool has_xiaohe_reading = false;
            if (found != pronunciations.end()) {
                for (const auto& syllable : found->second) {
                    if (!pinyin.is_syllable(syllable)) {
                        continue;
                    }
                    has_legal_full_reading = true;
                    const auto codes = syllable_codes.find(syllable);
                    const auto full = full_results.find(syllable);
                    full_hit = full_hit || (full != full_results.end() && contains_word(full->second, word));
                    if (codes != syllable_codes.end()) {
                        has_xiaohe_reading = true;
                        for (const auto& code : codes->second) {
                            const auto flypy = flypy_results.find(code);
                            flypy_hit = flypy_hit ||
                                (flypy != flypy_results.end() && contains_word(flypy->second, word));
                        }
                    }
                }
            }
            (void)has_legal_full_reading;
            (void)has_xiaohe_reading;
            general_full_hits += full_hit ? 1U : 0U;
            general_flypy_hits += flypy_hit ? 1U : 0U;
            if (common_set.contains(word)) {
                common_full_hits += full_hit ? 1U : 0U;
                common_flypy_hits += flypy_hit ? 1U : 0U;
            }
        }

        std::cout << "character coverage: common_full=" << common_full_hits << '/' << common.size()
                  << " common_xiaohe=" << common_flypy_hits << '/' << common.size()
                  << " general_full=" << general_full_hits << '/' << general.size()
                  << " general_xiaohe=" << general_flypy_hits << '/' << general.size() << '\n';
        // The release dictionary deliberately follows Rime Ice's default
        // import list instead of silently mixing PiInput's old character
        // tables or enabling Rime's default-disabled 41448 table. Record its
        // near-complete coverage as a quality gate without requiring another
        // public candidate source.
        check(common_full_hits >= 3490U && common_flypy_hits >= 3490U,
            "Rime Ice default tables cover at least 3490 of the 3500 common characters");
        check(general_full_hits >= 6950U && general_flypy_hits >= 6950U,
            "Rime Ice default tables cover at least 6950 of the 7000 general characters");
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " character coverage failure(s)\n";
        return 1;
    }
    std::cout << "All character coverage tests passed.\n";
    return 0;
}
