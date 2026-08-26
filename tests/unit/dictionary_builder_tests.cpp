#include "piinput/dictionary_builder.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool contains(
    const std::vector<piinput::LexiconCandidate>& entries,
    const std::string& word,
    const std::string& pinyin) {
    for (const auto& entry : entries) {
        if (entry.word == word && entry.pinyin == pinyin) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const piinput::LexiconCandidate* find_entry(
    const std::vector<piinput::LexiconCandidate>& entries,
    const std::string& word,
    const std::string& pinyin) {
    for (const auto& entry : entries) {
        if (entry.word == word && entry.pinyin == pinyin) return &entry;
    }
    return nullptr;
}

}  // namespace

int main() {
    try {
        const auto fixture = std::filesystem::path(PIINPUT_SOURCE_DIR) /
            "tests/data/dictionary_builder/thuocl_poem.txt";
        const auto terms = piinput::read_thuocl_terms(fixture);
        check(terms.size() == 3U, "all valid THUOCL rows are parsed");
        check(terms[0].word == "黄河入海流", "THUOCL word is preserved");
        check(terms[0].frequency == 67U, "THUOCL frequency is parsed");

        const std::vector<piinput::LexiconCandidate> pronunciations = {
            {"更上一层楼", "geng'shang'yi'ceng'lou", 1U},
            {"黄", "huang", 1U},
            {"河", "he", 1U},
            {"入", "ru", 1U},
            {"海", "hai", 1U},
            {"流", "liu", 1U},
        };
        piinput::DictionaryBuildReport report;
        const auto resolved = piinput::resolve_dictionary_terms(
            terms, pronunciations, 1'500U, report);

        check(contains(resolved, "黄河入海流", "huang'he'ru'hai'liu"),
              "missing phrase pronunciation is derived from character readings");
        check(contains(resolved, "更上一层楼", "geng'shang'yi'ceng'lou"),
              "exact phrase pronunciation is preferred");
        check(report.exact_phrase_count == 1U,
              "exact phrase pronunciation count is reported");
        check(report.character_derived_count == 1U,
              "character-derived pronunciation count is reported");
        check(report.unresolved_count == 1U,
              "unresolved terms are reported instead of guessed");
        check(resolved.size() == 2U, "unresolved terms are not emitted");

        const auto rime_master = std::filesystem::path(PIINPUT_SOURCE_DIR) /
            "tests/data/dictionary_builder/rime_master.dict.yaml";
        piinput::RimeDictionaryImportReport rime_report;
        const auto rime = piinput::read_rime_dictionary(
            rime_master, 1U, rime_report);
        check(rime_report.sources.size() == 2U,
            "only active Rime imports are resolved");
        check(rime_report.raw_entries == 4U &&
                rime_report.accepted_entries == 3U &&
                rime_report.duplicate_entries == 1U,
            "Rime import counts distinguish accepted duplicates");
        const auto* border = find_entry(rime, "边框", "bian'kuang");
        check(border != nullptr && border->weight == 100U,
            "the first imported Rime table wins duplicate entries");
        check(contains(rime, "编筐", "bian'kuang"),
            "Rime space-separated pinyin is canonicalized");
        check(contains(rime, "地名", "di'ming"),
            "later unique Rime entries remain available");

        std::cout << "Dictionary builder tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Dictionary builder test failure: " << error.what() << '\n';
        return 1;
    }
}
