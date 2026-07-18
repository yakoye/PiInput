#include "liteime/dictionary_builder.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace liteime {
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

}  // namespace

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
            format == DictionarySourceFormat::liteime_tsv ||
            format == DictionarySourceFormat::rime_yaml;
        if (format == DictionarySourceFormat::liteime_tsv || format == DictionarySourceFormat::rime_yaml) {
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

}  // namespace liteime
