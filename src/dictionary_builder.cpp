#include "piinput/dictionary_builder.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace piinput {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void replace_all(std::string& value, const std::string_view from, const char to) {
    std::size_t position = 0U;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), 1U, to);
        ++position;
    }
}

[[nodiscard]] std::string normalize_pinyin(std::string value) {
    static constexpr std::pair<std::string_view, char> accents[] = {
        {"ā", 'a'}, {"á", 'a'}, {"ǎ", 'a'}, {"à", 'a'},
        {"ē", 'e'}, {"é", 'e'}, {"ě", 'e'}, {"è", 'e'},
        {"ī", 'i'}, {"í", 'i'}, {"ǐ", 'i'}, {"ì", 'i'},
        {"ō", 'o'}, {"ó", 'o'}, {"ǒ", 'o'}, {"ò", 'o'},
        {"ū", 'u'}, {"ú", 'u'}, {"ǔ", 'u'}, {"ù", 'u'},
        {"ǖ", 'v'}, {"ǘ", 'v'}, {"ǚ", 'v'}, {"ǜ", 'v'}, {"ü", 'v'},
        {"ń", 'n'}, {"ň", 'n'}, {"ǹ", 'n'}, {"ḿ", 'm'},
    };
    for (const auto& [accent, plain] : accents) {
        replace_all(value, accent, plain);
    }
    std::string output;
    output.reserve(value.size());
    bool separator = false;
    for (const unsigned char ch : value) {
        if (std::isalpha(ch) != 0) {
            if (separator && !output.empty() && output.back() != '\'') {
                output.push_back('\'');
            }
            separator = false;
            output.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '\'' || std::isspace(ch) != 0 || ch == '-') {
            separator = true;
        } else if (std::isdigit(ch) != 0) {
            continue;
        }
    }
    while (!output.empty() && output.back() == '\'') {
        output.pop_back();
    }
    return output;
}

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> columns;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        columns.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return columns;
}

[[nodiscard]] std::uint32_t parse_weight(const std::string& value, const std::uint32_t fallback) {
    try {
        const auto parsed = std::stoull(value);
        return parsed > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(parsed);
    } catch (const std::exception&) {
        return fallback;
    }
}

[[nodiscard]] std::vector<std::string> split_utf8_characters(const std::string& value) {
    std::vector<std::string> result;
    for (std::size_t offset = 0U; offset < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[offset]);
        std::size_t length = 0U;
        if (first < 0x80U) {
            length = 1U;
        } else if ((first & 0xE0U) == 0xC0U) {
            length = 2U;
        } else if ((first & 0xF0U) == 0xE0U) {
            length = 3U;
        } else if ((first & 0xF8U) == 0xF0U) {
            length = 4U;
        } else {
            throw std::runtime_error("Invalid UTF-8 in dictionary term");
        }
        if (offset + length > value.size()) {
            throw std::runtime_error("Truncated UTF-8 in dictionary term");
        }
        for (std::size_t index = 1U; index < length; ++index) {
            const unsigned char continuation = static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::runtime_error("Invalid UTF-8 continuation in dictionary term");
            }
        }
        result.emplace_back(value.substr(offset, length));
        offset += length;
    }
    return result;
}

[[nodiscard]] std::uint32_t normalized_thuocl_weight(
    const std::uint64_t frequency,
    const std::uint32_t base_weight) {
    const long double scaled = std::log1p(static_cast<long double>(frequency)) * 1000.0L;
    const std::uint64_t bonus = static_cast<std::uint64_t>(std::llround(scaled));
    const std::uint64_t total = static_cast<std::uint64_t>(base_weight) + bonus;
    return static_cast<std::uint32_t>((std::min)(
        total, static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
}

}  // namespace

std::vector<DictionaryTerm> read_thuocl_terms(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open THUOCL source: " + path.string());
    }
    std::vector<DictionaryTerm> terms;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto columns = split_tabs(line);
        if (columns.size() != 2U) {
            throw std::runtime_error("Invalid THUOCL line " + std::to_string(line_number));
        }
        DictionaryTerm term;
        term.word = trim(columns[0]);
        const std::string frequency_text = trim(columns[1]);
        const auto parsed = std::from_chars(
            frequency_text.data(), frequency_text.data() + frequency_text.size(), term.frequency);
        if (term.word.empty() || frequency_text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != frequency_text.data() + frequency_text.size() || term.frequency == 0U) {
            throw std::runtime_error("Invalid THUOCL value at line " + std::to_string(line_number));
        }
        terms.push_back(std::move(term));
    }
    return terms;
}

std::vector<LexiconCandidate> resolve_dictionary_terms(
    const std::vector<DictionaryTerm>& terms,
    const std::vector<LexiconCandidate>& pronunciations,
    const std::uint32_t base_weight,
    DictionaryBuildReport& report) {
    report = {};
    std::unordered_map<std::string, const LexiconCandidate*> exact;
    std::unordered_map<std::string, const LexiconCandidate*> characters;
    for (const auto& pronunciation : pronunciations) {
        auto [found, inserted] = exact.emplace(pronunciation.word, &pronunciation);
        if (!inserted && (pronunciation.weight > found->second->weight ||
            (pronunciation.weight == found->second->weight &&
             pronunciation.pinyin < found->second->pinyin))) {
            found->second = &pronunciation;
        }
        if (split_utf8_characters(pronunciation.word).size() == 1U) {
            auto [character, character_inserted] = characters.emplace(
                pronunciation.word, &pronunciation);
            if (!character_inserted &&
                (pronunciation.weight > character->second->weight ||
                 (pronunciation.weight == character->second->weight &&
                  pronunciation.pinyin < character->second->pinyin))) {
                character->second = &pronunciation;
            }
        }
    }

    std::vector<LexiconCandidate> resolved;
    resolved.reserve(terms.size());
    for (const auto& term : terms) {
        const auto phrase = exact.find(term.word);
        if (phrase != exact.end() && split_utf8_characters(term.word).size() > 1U) {
            resolved.push_back({
                term.word,
                phrase->second->pinyin,
                normalized_thuocl_weight(term.frequency, base_weight),
                false,
            });
            ++report.exact_phrase_count;
            continue;
        }

        std::string pinyin;
        bool complete = true;
        for (const auto& character : split_utf8_characters(term.word)) {
            const auto found = characters.find(character);
            if (found == characters.end() || found->second->pinyin.empty() ||
                found->second->pinyin.find('\'') != std::string::npos) {
                complete = false;
                break;
            }
            if (!pinyin.empty()) {
                pinyin.push_back('\'');
            }
            pinyin.append(found->second->pinyin);
        }
        if (!complete) {
            ++report.unresolved_count;
            continue;
        }
        resolved.push_back({
            term.word,
            std::move(pinyin),
            normalized_thuocl_weight(term.frequency, base_weight),
            false,
        });
        ++report.character_derived_count;
    }
    return resolved;
}

std::vector<LexiconCandidate> read_dictionary_source(
    const std::filesystem::path& path,
    const DictionarySourceFormat format,
    const std::uint32_t default_weight) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open dictionary source: " + path.string());
    }
    std::vector<LexiconCandidate> entries;
    std::string line;
    if (format == DictionarySourceFormat::thuocl) {
        throw std::invalid_argument(
            "THUOCL sources require pronunciation resolution before merging");
    }
    bool rime_body = format != DictionarySourceFormat::rime_yaml;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (format == DictionarySourceFormat::rime_yaml && !rime_body) {
            if (line == "...") {
                rime_body = true;
            }
            continue;
        }

        LexiconCandidate entry;
        entry.weight = default_weight;
        entry.authoritative_weight =
            format == DictionarySourceFormat::piinput_tsv ||
            format == DictionarySourceFormat::rime_yaml;
        if (format == DictionarySourceFormat::piinput_tsv || format == DictionarySourceFormat::rime_yaml) {
            const auto columns = split_tabs(line);
            if (columns.size() < 2U || columns[0] == "word") {
                continue;
            }
            entry.word = trim(columns[0]);
            entry.pinyin = normalize_pinyin(columns[1]);
            if (columns.size() >= 3U) {
                entry.weight = parse_weight(columns[2], default_weight);
            }
        } else if (format == DictionarySourceFormat::phrase_pinyin_data) {
            const auto separator = line.find(": ");
            if (separator == std::string::npos) {
                continue;
            }
            entry.word = trim(line.substr(0U, separator));
            entry.pinyin = normalize_pinyin(line.substr(separator + 2U));
        } else {
            const auto colon = line.find(':');
            const auto comment = line.find('#', colon == std::string::npos ? 0U : colon);
            if (colon == std::string::npos || comment == std::string::npos) {
                continue;
            }
            std::string pronunciation = trim(line.substr(colon + 1U, comment - colon - 1U));
            const auto alternate = pronunciation.find_first_of(",;");
            if (alternate != std::string::npos) {
                pronunciation.resize(alternate);
            }
            entry.word = trim(line.substr(comment + 1U));
            const auto annotation = entry.word.find_first_of(" <-?");
            if (annotation != std::string::npos) {
                entry.word.resize(annotation);
            }
            entry.pinyin = normalize_pinyin(pronunciation);
        }
        if (!entry.word.empty() && !entry.pinyin.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::vector<LexiconCandidate> read_rime_dictionary(
    const std::filesystem::path& master_path,
    const std::uint32_t default_weight,
    RimeDictionaryImportReport& report) {
    std::ifstream input(master_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open Rime master dictionary: " +
            master_path.string());
    }

    std::vector<std::filesystem::path> tables;
    bool imports = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trim(line);
        if (stripped == "...") break;
        if (stripped.empty() || stripped.front() == '#') continue;
        if (stripped == "import_tables:") {
            imports = true;
            continue;
        }
        if (!imports) continue;
        if (!stripped.starts_with("- ")) {
            if (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) == 0) {
                imports = false;
            }
            continue;
        }
        std::string table = trim(stripped.substr(2U));
        if (const auto comment = table.find('#'); comment != std::string::npos) {
            table = trim(table.substr(0U, comment));
        }
        if (table.size() >= 2U &&
            ((table.front() == '"' && table.back() == '"') ||
             (table.front() == '\'' && table.back() == '\''))) {
            table = table.substr(1U, table.size() - 2U);
        }
        if (table.empty()) continue;
        std::filesystem::path path = master_path.parent_path() /
            std::filesystem::path(table);
        if (path.extension() != ".yaml") {
            path += ".dict.yaml";
        }
        tables.push_back(std::move(path));
    }
    if (tables.empty()) {
        throw std::runtime_error("Rime master dictionary has no active import_tables: " +
            master_path.string());
    }

    report = {};
    std::vector<LexiconCandidate> result;
    std::unordered_set<std::string> seen;
    for (const auto& table : tables) {
        auto entries = read_dictionary_source(
            table, DictionarySourceFormat::rime_yaml, default_weight);
        RimeDictionarySourceReport source;
        source.path = table;
        source.raw_entries = entries.size();
        report.raw_entries += entries.size();
        for (auto& entry : entries) {
            const std::string key = entry.word + "\n" + entry.pinyin;
            if (!seen.insert(key).second) {
                ++source.duplicate_entries;
                ++report.duplicate_entries;
                continue;
            }
            ++source.accepted_entries;
            ++report.accepted_entries;
            result.push_back(std::move(entry));
        }
        report.sources.push_back(std::move(source));
    }
    if (result.empty()) {
        throw std::runtime_error("Rime master dictionary produced no entries: " +
            master_path.string());
    }
    return result;
}

void write_dictionary_tsv(
    const std::filesystem::path& path,
    std::vector<LexiconCandidate> entries) {
    struct MergedWeight {
        std::uint32_t value{};
        bool authoritative{};
    };
    std::map<std::pair<std::string, std::string>, MergedWeight> unique;
    for (const auto& entry : entries) {
        auto& weight = unique[{entry.word, entry.pinyin}];
        if ((entry.authoritative_weight && !weight.authoritative) ||
            (entry.authoritative_weight == weight.authoritative && entry.weight > weight.value)) {
            weight.value = entry.weight;
            weight.authoritative = entry.authoritative_weight;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create dictionary TSV: " + path.string());
    }
    output << "word\tpinyin\tweight\n";
    for (const auto& [key, weight] : unique) {
        output << key.first << '\t' << key.second << '\t' << weight.value << '\n';
    }
    if (!output) {
        throw std::runtime_error("Failed while writing dictionary TSV");
    }
}

}  // namespace piinput
