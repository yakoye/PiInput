#include "liteime/symbols.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace liteime {
namespace {

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(line.data() + start, line.size() - start);
            break;
        }
        fields.emplace_back(line.data() + start, tab - start);
        start = tab + 1U;
    }
    return fields;
}

[[nodiscard]] std::vector<std::string> split_aliases(const std::string_view value) {
    std::vector<std::string> aliases;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index <= value.size(); ++index) {
        if (index == value.size() || value[index] == '|') {
            if (index > start) {
                aliases.emplace_back(value.substr(start, index - start));
            }
            start = index + 1U;
        }
    }
    return aliases;
}

[[nodiscard]] std::string normalize_ascii(std::string value) {
    for (char& character : value) {
        const unsigned char current = static_cast<unsigned char>(character);
        if (current < 0x80U) {
            character = static_cast<char>(std::tolower(current));
        }
    }
    value.erase(std::remove_if(value.begin(), value.end(), [](const char character) {
        return character == ' ' || character == '-' || character == '_';
    }), value.end());
    return value;
}

[[nodiscard]] int match_score(const std::string& query, const std::string& value) {
    const std::string normalized_value = normalize_ascii(value);
    if (normalized_value == query) {
        return 1000;
    }
    if (normalized_value.starts_with(query)) {
        return 700;
    }
    if (normalized_value.find(query) != std::string::npos) {
        return 400;
    }
    return 0;
}

}  // namespace

void SymbolIndex::load_tsv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open symbol table: " + path.string());
    }
    entries_.clear();
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
        const auto fields = split_tabs(line);
        if (fields.size() < 4U) {
            throw std::runtime_error("Invalid symbol table line " + std::to_string(line_number));
        }
        if (fields[0] == "symbol" && fields[1] == "category") {
            continue;
        }
        entries_.push_back({
            std::string(fields[0]),
            std::string(fields[1]),
            std::string(fields[2]),
            split_aliases(fields[3]),
            0,
        });
    }
}

std::vector<SymbolCandidate> SymbolIndex::search(
    const std::string& raw_query,
    const std::size_t limit) const {
    if (limit == 0U) {
        return {};
    }
    const std::string query = normalize_ascii(raw_query);
    if (query.empty()) {
        return {};
    }
    std::vector<SymbolCandidate> results;
    for (const auto& entry : entries_) {
        int score = match_score(query, entry.symbol);
        score = std::max(score, match_score(query, entry.name));
        score = std::max(score, match_score(query, entry.category) - 80);
        for (const auto& alias : entry.aliases) {
            score = std::max(score, match_score(query, alias) - 20);
        }
        if (score <= 0) {
            continue;
        }
        SymbolCandidate candidate = entry;
        candidate.score = score;
        results.push_back(std::move(candidate));
    }
    std::stable_sort(results.begin(), results.end(), [](const SymbolCandidate& left, const SymbolCandidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.category != right.category) {
            return left.category < right.category;
        }
        return left.symbol < right.symbol;
    });
    if (results.size() > limit) {
        results.resize(limit);
    }
    return results;
}

std::size_t SymbolIndex::entry_count() const noexcept {
    return entries_.size();
}

}  // namespace liteime
