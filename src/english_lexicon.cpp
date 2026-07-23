#include "piinput/english_lexicon.h"

#include "piinput/windows_compat.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <system_error>

namespace piinput {
namespace {

constexpr std::size_t kMaximumLineLength = 4096U;

[[nodiscard]] bool is_ascii_word(const std::string_view word) noexcept {
    return !word.empty() &&
        std::all_of(word.begin(), word.end(), [](const char character) {
            const auto value = static_cast<unsigned char>(character);
            return (value >= static_cast<unsigned char>('A') &&
                       value <= static_cast<unsigned char>('Z')) ||
                (value >= static_cast<unsigned char>('a') &&
                    value <= static_cast<unsigned char>('z'));
        });
}

[[nodiscard]] std::string ascii_lower(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character >= 'A' && character <= 'Z') {
            result.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] bool parse_positive_integer(
    const std::string_view text,
    std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value > 0U;
}

[[nodiscard]] bool split_tsv_row(
    const std::string_view line,
    std::string_view& word,
    std::string_view& number) noexcept {
    const auto separator = line.find('\t');
    if (separator == std::string_view::npos ||
        line.find('\t', separator + 1U) != std::string_view::npos) {
        return false;
    }
    word = line.substr(0U, separator);
    number = line.substr(separator + 1U);
    return true;
}

[[nodiscard]] bool replace_file_atomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) noexcept {
#ifdef _WIN32
    return MoveFileExW(
               temporary.c_str(),
               destination.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

}  // namespace

std::size_t EnglishLexicon::load_builtin_tsv(const std::filesystem::path& path) {
    return load_dictionary_tsv(path, false);
}

std::size_t EnglishLexicon::load_user_tsv(const std::filesystem::path& path) {
    return load_dictionary_tsv(path, true);
}

std::size_t EnglishLexicon::load_learning_tsv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0U;
    }

    std::size_t accepted = 0U;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.size() > kMaximumLineLength || line.front() == '#') {
            continue;
        }
        std::string_view word;
        std::string_view count_text;
        std::uint64_t count = 0U;
        if (!split_tsv_row(line, word, count_text) || !is_ascii_word(word) ||
            !parse_positive_integer(count_text, count)) {
            continue;
        }
        const auto found = entry_by_word_.find(std::string(word));
        if (found == entry_by_word_.end()) {
            continue;
        }
        auto& candidate = entries_[found->second].candidate;
        candidate.learning_count = (std::max)(candidate.learning_count, count);
        ++accepted;
    }
    return accepted;
}

std::vector<EnglishCandidate> EnglishLexicon::query(
    const std::string_view prefix,
    const std::size_t limit) const {
    if (prefix.empty() || limit == 0U || !is_ascii_word(prefix)) {
        return {};
    }
    const std::string lowercase_prefix = ascii_lower(prefix);
    const auto first = std::lower_bound(
        prefix_index_.begin(), prefix_index_.end(), lowercase_prefix,
        [&](const std::size_t index, const std::string& value) {
            return entries_[index].lowercase_word < value;
        });

    std::vector<EnglishCandidate> result;
    for (auto current = first; current != prefix_index_.end(); ++current) {
        const auto& entry = entries_[*current];
        if (!entry.lowercase_word.starts_with(lowercase_prefix)) {
            break;
        }
        result.push_back(entry.candidate);
    }
    std::sort(result.begin(), result.end(), [](const EnglishCandidate& left, const EnglishCandidate& right) {
        if (left.learning_count != right.learning_count) {
            return left.learning_count > right.learning_count;
        }
        if (left.user_entry != right.user_entry) {
            return left.user_entry;
        }
        if (left.base_weight != right.base_weight) {
            return left.base_weight > right.base_weight;
        }
        if (left.id != right.id) {
            return left.id < right.id;
        }
        return left.word < right.word;
    });
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

bool EnglishLexicon::record_selection(const std::string_view word) noexcept {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.candidate.word == word;
    });
    if (found == entries_.end()) {
        return false;
    }
    auto& learning_count = found->candidate.learning_count;
    if (learning_count != (std::numeric_limits<std::uint64_t>::max)()) {
        ++learning_count;
    }
    return true;
}

bool EnglishLexicon::save_learning_tsv(const std::filesystem::path& path) const noexcept {
    try {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        auto temporary = path;
        temporary += ".tmp";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                return false;
            }
            std::vector<const Entry*> learned;
            for (const auto& entry : entries_) {
                if (entry.candidate.learning_count > 0U) {
                    learned.push_back(&entry);
                }
            }
            std::sort(learned.begin(), learned.end(), [](const Entry* left, const Entry* right) {
                return left->candidate.word < right->candidate.word;
            });
            for (const Entry* entry : learned) {
                output << entry->candidate.word << '\t'
                       << entry->candidate.learning_count << '\n';
            }
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }
        if (replace_file_atomically(temporary, path)) {
            return true;
        }
        std::filesystem::remove(temporary, ignored);
    } catch (...) {
        return false;
    }
    return false;
}

std::size_t EnglishLexicon::load_dictionary_tsv(
    const std::filesystem::path& path,
    const bool user_dictionary) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0U;
    }

    std::size_t accepted = 0U;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.size() > kMaximumLineLength || line.front() == '#') {
            continue;
        }
        std::string_view word;
        std::string_view weight_text;
        std::uint64_t weight = 0U;
        if (!split_tsv_row(line, word, weight_text) || !is_ascii_word(word) ||
            !parse_positive_integer(weight_text, weight)) {
            continue;
        }
        const auto found = entry_by_word_.find(std::string(word));
        if (found == entry_by_word_.end()) {
            const std::size_t index = entries_.size();
            entries_.push_back({
                {next_id_++, std::string(word), weight, 0U, user_dictionary},
                ascii_lower(word),
            });
            entry_by_word_.emplace(entries_.back().candidate.word, index);
        } else {
            auto& candidate = entries_[found->second].candidate;
            candidate.base_weight = (std::max)(candidate.base_weight, weight);
            candidate.user_entry = candidate.user_entry || user_dictionary;
        }
        ++accepted;
    }
    rebuild_index();
    return accepted;
}

void EnglishLexicon::rebuild_index() {
    prefix_index_.resize(entries_.size());
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        prefix_index_[index] = index;
    }
    std::sort(prefix_index_.begin(), prefix_index_.end(), [&](const std::size_t left, const std::size_t right) {
        if (entries_[left].lowercase_word != entries_[right].lowercase_word) {
            return entries_[left].lowercase_word < entries_[right].lowercase_word;
        }
        return entries_[left].candidate.id < entries_[right].candidate.id;
    });
}

}  // namespace piinput
