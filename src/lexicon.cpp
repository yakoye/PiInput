#include "piinput/lexicon.h"
#include "piinput/pinyin.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>

namespace piinput {
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

[[nodiscard]] std::size_t canonical_syllable_count(const std::string_view canonical) {
    if (canonical.empty()) return 0U;
    return static_cast<std::size_t>(
        std::count(canonical.begin(), canonical.end(), '\'')) + 1U;
}

}  // namespace

struct DevLexicon::ReverseWordIndex {
    std::shared_mutex mutex;
    bool built{};
    std::unordered_map<std::string, std::vector<LexiconCandidate>> entries;
};

struct DevLexicon::SimplifiedIndex {
    std::shared_mutex mutex;
    bool built{};
    // (simplified key, pinyin key), sorted by simplified key.
    std::vector<std::pair<std::string, std::string>> keys;
};

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
    pinyin_keys_.clear();
    entry_count_ = entries.size();
    reverse_word_index_ = std::make_shared<ReverseWordIndex>();
    for (auto& candidate : entries) {
        entries_by_pinyin_[candidate.pinyin].push_back(std::move(candidate));
    }
    for (auto& [pinyin, candidates] : entries_by_pinyin_) {
        pinyin_keys_.push_back(pinyin);
        std::stable_sort(candidates.begin(), candidates.end(), [](const LexiconCandidate& left, const LexiconCandidate& right) {
            if (left.weight != right.weight) {
                return left.weight > right.weight;
            }
            return left.word < right.word;
        });
    }
    std::sort(pinyin_keys_.begin(), pinyin_keys_.end());
    simplified_index_ = std::make_shared<SimplifiedIndex>();
}

std::vector<LexiconCandidate> DevLexicon::query_simplified(
    const std::string& key,
    const std::vector<std::string>& syllable_filter,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    if (key.empty() || limit == 0U || scan_limit == 0U || !simplified_index_) {
        return {};
    }
    {
        std::shared_lock read_lock(simplified_index_->mutex);
        if (!simplified_index_->built) {
            read_lock.unlock();
            std::unique_lock write_lock(simplified_index_->mutex);
            if (!simplified_index_->built) {
                simplified_index_->keys.reserve(pinyin_keys_.size());
                for (const auto& pinyin : pinyin_keys_) {
                    auto entry_key = simplified_pinyin_key(pinyin);
                    if (entry_key.empty()) {
                        continue;
                    }
                    simplified_index_->keys.emplace_back(std::move(entry_key), pinyin);
                }
                std::sort(simplified_index_->keys.begin(), simplified_index_->keys.end());
                simplified_index_->built = true;
            }
        }
    }
    const auto& simplified_keys_ = simplified_index_->keys;
    // Every syllable the user spelled out has to match exactly. Without this,
    // sruf would offer everything that srf offers, and typing more letters
    // would stop narrowing anything down.
    const auto matches_filter = [&syllable_filter](const std::string& pinyin) {
        if (syllable_filter.empty()) {
            return true;
        }
        std::size_t index = 0U;
        std::size_t start = 0U;
        while (start <= pinyin.size() && index < syllable_filter.size()) {
            const std::size_t separator = pinyin.find('\'', start);
            const std::size_t stop = separator == std::string::npos ? pinyin.size() : separator;
            const std::string_view syllable(pinyin.data() + start, stop - start);
            const auto& required = syllable_filter[index];
            if (!required.empty() && syllable != required) {
                return false;
            }
            ++index;
            if (separator == std::string::npos) {
                break;
            }
            start = separator + 1U;
        }
        return index == syllable_filter.size();
    };

    // Exact key length only, never a prefix. Typing three initials means a
    // three-syllable word: letting srf also reach 输入法软件 would be guessing
    // syllables the user has not typed towards, which is the same rule the
    // unfinished-syllable completion follows.
    std::vector<LexiconCandidate> results;
    std::size_t scanned = 0U;
    auto position = std::lower_bound(simplified_keys_.begin(), simplified_keys_.end(), key,
        [](const std::pair<std::string, std::string>& entry, const std::string& value) {
            return entry.first < value;
        });
    for (; position != simplified_keys_.end() && position->first == key &&
           scanned < scan_limit; ++position) {
        ++scanned;
        if (!matches_filter(position->second)) {
            continue;
        }
        const auto found = entries_by_pinyin_.find(position->second);
        if (found == entries_by_pinyin_.end()) {
            continue;
        }
        for (const auto& candidate : found->second) {
            results.push_back(candidate);
        }
    }
    // A word with exactly as many syllables as the user typed initials is one
    // they finished spelling; the longer ones only matched because the key is
    // also a prefix. Without this, sruf offered 收入分配 ahead of 输入法 --
    // the extra syllable the user never typed won on raw frequency.
    const std::size_t typed_syllables = key.size();
    const auto syllables_of = [](const std::string& pinyin) {
        return static_cast<std::size_t>(
            1 + std::count(pinyin.begin(), pinyin.end(), '\''));
    };
    std::stable_sort(results.begin(), results.end(),
        [&](const LexiconCandidate& left, const LexiconCandidate& right) {
            const bool left_complete = syllables_of(left.pinyin) == typed_syllables;
            const bool right_complete = syllables_of(right.pinyin) == typed_syllables;
            if (left_complete != right_complete) {
                return left_complete;
            }
            if (left.weight != right.weight) {
                return left.weight > right.weight;
            }
            if (left.pinyin.size() != right.pinyin.size()) {
                return left.pinyin.size() < right.pinyin.size();
            }
            return left.word < right.word;
        });
    if (results.size() > limit) {
        results.resize(limit);
    }
    return results;
}

std::vector<LexiconCandidate> DevLexicon::query_word(
    const std::string_view word,
    const std::size_t limit) const {
    if (word.empty() || limit == 0U) {
        return {};
    }
    if (!reverse_word_index_) {
        return {};
    }
    {
        std::shared_lock read_lock(reverse_word_index_->mutex);
        if (reverse_word_index_->built) {
            const auto found = reverse_word_index_->entries.find(std::string(word));
            if (found == reverse_word_index_->entries.end()) {
                return {};
            }
            const std::size_t count = (std::min)(limit, found->second.size());
            return {found->second.begin(),
                    found->second.begin() + static_cast<std::ptrdiff_t>(count)};
        }
    }
    std::unique_lock write_lock(reverse_word_index_->mutex);
    if (!reverse_word_index_->built) {
        for (const auto& [pinyin, candidates] : entries_by_pinyin_) {
            (void)pinyin;
            for (const auto& candidate : candidates) {
                reverse_word_index_->entries[candidate.word].push_back(candidate);
            }
        }
        for (auto& [indexed_word, candidates] : reverse_word_index_->entries) {
            (void)indexed_word;
            std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                if (left.weight != right.weight) return left.weight > right.weight;
                if (left.pinyin != right.pinyin) return left.pinyin < right.pinyin;
                return left.word < right.word;
            });
            candidates.erase(std::unique(
                candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                    return left.word == right.word && left.pinyin == right.pinyin;
                }), candidates.end());
        }
        reverse_word_index_->built = true;
    }
    const auto found = reverse_word_index_->entries.find(std::string(word));
    if (found == reverse_word_index_->entries.end()) {
        return {};
    }
    const std::size_t count = (std::min)(limit, found->second.size());
    return {found->second.begin(),
            found->second.begin() + static_cast<std::ptrdiff_t>(count)};
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

std::vector<LexiconCandidate> DevLexicon::query_prefix(
    const std::string& pinyin_prefix,
    const std::size_t limit,
    const std::size_t scan_limit,
    const std::size_t max_syllables) const {
    if (pinyin_prefix.empty() || limit == 0U || scan_limit == 0U) {
        return {};
    }
    std::vector<LexiconCandidate> results;
    results.reserve((std::min)(limit, scan_limit));
    std::size_t scanned = 0U;
    auto key = std::lower_bound(pinyin_keys_.begin(), pinyin_keys_.end(), pinyin_prefix);
    while (key != pinyin_keys_.end() && key->starts_with(pinyin_prefix) && scanned < scan_limit) {
        // Rejecting the key costs one scan slot instead of one per candidate
        // behind it, so the bound only ever shortens the walk.
        if (max_syllables != 0U && canonical_syllable_count(*key) > max_syllables) {
            ++scanned;
            ++key;
            continue;
        }
        const auto found = entries_by_pinyin_.find(*key);
        if (found != entries_by_pinyin_.end()) {
            for (const auto& candidate : found->second) {
                if (scanned++ >= scan_limit) {
                    break;
                }
                results.push_back(candidate);
            }
        }
        ++key;
    }
    std::stable_sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        if (left.pinyin.size() != right.pinyin.size()) {
            return left.pinyin.size() < right.pinyin.size();
        }
        if (left.pinyin != right.pinyin) {
            return left.pinyin < right.pinyin;
        }
        return left.word < right.word;
    });
    if (results.size() > limit) {
        results.resize(limit);
    }
    return results;
}

std::size_t DevLexicon::entry_count() const noexcept {
    return entry_count_;
}

}  // namespace piinput
