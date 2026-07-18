#include "liteime/lexicon.h"

#include <algorithm>
#include <charconv>
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

}  // namespace

void DevLexicon::load_tsv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open TSV lexicon: " + path.string());
    }

    std::vector<LexiconCandidate> entries;
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
        if (fields.size() < 3U) {
            throw std::runtime_error("Invalid TSV lexicon line " + std::to_string(line_number));
        }
        if (fields[0] == "word" && fields[1] == "pinyin") {
            continue;
        }

        std::uint32_t weight = 0U;
        const auto* begin = fields[2].data();
        const auto* end = begin + fields[2].size();
        const auto parse_result = std::from_chars(begin, end, weight);
        if (parse_result.ec != std::errc{} || parse_result.ptr != end) {
            throw std::runtime_error("Invalid weight at TSV line " + std::to_string(line_number));
        }
        entries.push_back({std::string(fields[0]), std::string(fields[1]), weight});
    }
    load_entries(std::move(entries));
}

void DevLexicon::load_entries(std::vector<LexiconCandidate> entries) {
    entries_by_pinyin_.clear();
    entry_count_ = entries.size();
    for (auto& candidate : entries) {
        entries_by_pinyin_[candidate.pinyin].push_back(std::move(candidate));
    }
    for (auto& [pinyin, candidates] : entries_by_pinyin_) {
        (void)pinyin;
        std::stable_sort(candidates.begin(), candidates.end(), [](const LexiconCandidate& left, const LexiconCandidate& right) {
            if (left.weight != right.weight) {
                return left.weight > right.weight;
            }
            return left.word < right.word;
        });
    }
}

std::vector<LexiconCandidate> DevLexicon::query_exact(
    const std::string& pinyin,
    const std::size_t limit) const {
    const auto found = entries_by_pinyin_.find(pinyin);
    if (found == entries_by_pinyin_.end() || limit == 0U) {
        return {};
    }
    const std::size_t result_size = (std::min)(limit, found->second.size());
    return std::vector<LexiconCandidate>(
        found->second.begin(),
        found->second.begin() + static_cast<std::ptrdiff_t>(result_size));
}

std::size_t DevLexicon::entry_count() const noexcept {
    return entry_count_;
}

}  // namespace liteime
