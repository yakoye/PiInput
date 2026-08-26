#include "piinput/english_lexicon.h"

#include "piinput/windows_compat.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <system_error>

namespace piinput {
namespace {

constexpr std::size_t kMaximumLineLength = 4096U;
std::atomic<std::uint64_t> learning_temporary_counter{0U};
#ifndef _WIN32
std::mutex learning_file_mutex;
#endif

[[nodiscard]] std::uint64_t saturating_add(
    const std::uint64_t left,
    const std::uint64_t right) noexcept {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    return right > maximum - left ? maximum : left + right;
}

class LearningFileLock final {
public:
    LearningFileLock() noexcept {
#ifdef _WIN32
        handle_ = CreateMutexW(nullptr, FALSE, L"Local\\PiInput.EnglishLearning.v1");
        if (handle_ != nullptr) {
            const DWORD result = WaitForSingleObject(handle_, 1000U);
            locked_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
#else
        lock_ = std::unique_lock<std::mutex>(learning_file_mutex);
        locked_ = true;
#endif
    }

    ~LearningFileLock() {
#ifdef _WIN32
        if (locked_) {
            ReleaseMutex(handle_);
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
#endif
    }

    [[nodiscard]] bool locked() const noexcept { return locked_; }

private:
    bool locked_{};
#ifdef _WIN32
    HANDLE handle_{};
#else
    std::unique_lock<std::mutex> lock_;
#endif
};

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

[[nodiscard]] bool is_bounded_subsequence_completion(
    const std::string_view typed,
    const std::string_view word) noexcept {
    constexpr std::size_t maximum_insertions = 3U;
    if (typed.size() < 3U || word.size() <= typed.size() ||
        word.size() - typed.size() > maximum_insertions || typed.front() != word.front()) {
        return false;
    }
    std::size_t typed_index = 0U;
    for (const char character : word) {
        if (typed_index < typed.size() && character == typed[typed_index]) {
            ++typed_index;
        }
    }
    return typed_index == typed.size();
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

[[nodiscard]] bool parse_candidate_flags(
    const std::string_view text,
    std::uint32_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
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

[[nodiscard]] bool split_dictionary_row(
    const std::string_view line,
    std::string_view& word,
    std::string_view& number,
    std::uint32_t& flags) noexcept {
    const auto first = line.find('\t');
    if (first == std::string_view::npos) {
        return false;
    }
    const auto second = line.find('\t', first + 1U);
    if (second == std::string_view::npos) {
        word = line.substr(0U, first);
        number = line.substr(first + 1U);
        flags = 0U;
        return true;
    }
    if (line.find('\t', second + 1U) != std::string_view::npos) {
        return false;
    }
    word = line.substr(0U, first);
    number = line.substr(first + 1U, second - first - 1U);
    return parse_candidate_flags(line.substr(second + 1U), flags);
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

[[nodiscard]] std::unordered_map<std::string, std::uint64_t> read_learning_counts(
    const std::filesystem::path& path) {
    std::unordered_map<std::string, std::uint64_t> result;
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string_view word;
        std::string_view count_text;
        std::uint64_t count = 0U;
        if (line.empty() || line.size() > kMaximumLineLength || line.front() == '#' ||
            !split_tsv_row(line, word, count_text) || !is_ascii_word(word) ||
            !parse_positive_integer(count_text, count)) {
            continue;
        }
        auto& stored = result[std::string(word)];
        stored = (std::max)(stored, count);
    }
    return result;
}

[[nodiscard]] std::filesystem::path unique_learning_temporary(
    const std::filesystem::path& path) {
    auto temporary = path;
    temporary += ".tmp." + std::to_string(
#ifdef _WIN32
        static_cast<std::uint64_t>(GetCurrentProcessId())) + "." +
#else
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count())) + "." +
#endif
        std::to_string(++learning_temporary_counter);
    return temporary;
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
    return query(prefix, EnglishQueryOptions{.limit = limit});
}

std::vector<EnglishCandidate> EnglishLexicon::query(
    const std::string_view prefix,
    const EnglishQueryOptions& options) const {
    const std::size_t limit = options.limit;
    if (prefix.empty() || limit == 0U || !is_ascii_word(prefix)) {
        return {};
    }
    const std::string lowercase_prefix = ascii_lower(prefix);
    const auto first = std::lower_bound(
        prefix_index_.begin(), prefix_index_.end(), lowercase_prefix,
        [&](const std::size_t index, const std::string& value) {
            return entries_[index].lowercase_word < value;
        });

    // 学习到的词与用户词典不受权重下限约束：它们是用户自己打进来的，
    // 无论词库把它们排在哪一层，用户显然想要它们。
    const auto passes_weight = [&](const EnglishCandidate& candidate) {
        return candidate.user_entry || candidate.learning_count > 0U ||
               candidate.base_weight >= options.minimum_weight;
    };

    std::vector<EnglishCandidate> result;
    for (auto current = first; current != prefix_index_.end(); ++current) {
        const auto& entry = entries_[*current];
        if (!entry.lowercase_word.starts_with(lowercase_prefix)) {
            break;
        }
        if (!passes_weight(entry.candidate)) continue;
        result.push_back(entry.candidate);
    }
    const auto preference_for = [&](const EnglishCandidate& candidate) {
        const auto prefix_preferences = completion_preferences_.find(lowercase_prefix);
        if (prefix_preferences == completion_preferences_.end()) {
            return std::uint64_t{0U};
        }
        const auto found = prefix_preferences->second.find(ascii_lower(candidate.word));
        return found == prefix_preferences->second.end() ? std::uint64_t{0U} : found->second;
    };
    const auto ranked_before = [&](const EnglishCandidate& left, const EnglishCandidate& right) {
        if (left.user_entry != right.user_entry) {
            return left.user_entry;
        }
        if (left.learning_count != right.learning_count) {
            return left.learning_count > right.learning_count;
        }
        const auto left_preference = preference_for(left);
        const auto right_preference = preference_for(right);
        if (left_preference != right_preference) {
            return left_preference > right_preference;
        }
        const auto fuzzy_flag = static_cast<std::uint32_t>(EnglishCandidateFlag::fuzzy);
        const bool left_fuzzy = (left.flags & fuzzy_flag) != 0U;
        const bool right_fuzzy = (right.flags & fuzzy_flag) != 0U;
        if (left_fuzzy != right_fuzzy) {
            return !left_fuzzy;
        }
        if (left.base_weight != right.base_weight) {
            return left.base_weight > right.base_weight;
        }
        const auto left_completion = left.word.size() - lowercase_prefix.size();
        const auto right_completion = right.word.size() - lowercase_prefix.size();
        if (left_completion != right_completion) {
            return left_completion < right_completion;
        }
        if (left.id != right.id) {
            return left.id < right.id;
        }
        return left.word < right.word;
    };
    if (options.allow_subsequence && lowercase_prefix.size() >= 3U) {
        for (const std::size_t index : prefix_index_) {
            const auto& entry = entries_[index];
            if (entry.lowercase_word.starts_with(lowercase_prefix) ||
                !is_bounded_subsequence_completion(lowercase_prefix, entry.lowercase_word)) {
                continue;
            }
            if (!passes_weight(entry.candidate)) continue;
            auto candidate = entry.candidate;
            candidate.flags |= static_cast<std::uint32_t>(EnglishCandidateFlag::fuzzy);
            result.push_back(std::move(candidate));
        }
    }
    std::sort(result.begin(), result.end(), ranked_before);
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

std::size_t EnglishLexicon::load_completion_preferences_tsv(
    const std::filesystem::path& path) {
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
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1U);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('\t', second + 1U) != std::string::npos) {
            continue;
        }
        const std::string_view prefix(line.data(), first);
        const std::string_view word(line.data() + first + 1U, second - first - 1U);
        const std::string_view priority_text(line.data() + second + 1U, line.size() - second - 1U);
        std::uint64_t priority = 0U;
        if (!is_ascii_word(prefix) || !is_ascii_word(word) ||
            !parse_positive_integer(priority_text, priority)) {
            continue;
        }
        auto& stored = completion_preferences_[ascii_lower(prefix)][ascii_lower(word)];
        stored = (std::max)(stored, priority);
        ++accepted;
    }
    return accepted;
}

bool EnglishLexicon::record_selection(const std::string_view word) noexcept {
    const auto located = entry_by_word_.find(std::string(word));
    if (located == entry_by_word_.end()) {
        return false;
    }
    auto& candidate = entries_[located->second].candidate;
    auto& learning_count = candidate.learning_count;
    if (learning_count != (std::numeric_limits<std::uint64_t>::max)()) {
        ++learning_count;
        auto& pending = pending_learning_[candidate.word];
        if (pending != (std::numeric_limits<std::uint64_t>::max)()) {
            ++pending;
        }
    }
    return true;
}

bool EnglishLexicon::save_learning_tsv(const std::filesystem::path& path) noexcept {
    try {
        if (pending_learning_.empty()) {
            return true;
        }
        LearningFileLock lock;
        if (!lock.locked()) {
            return false;
        }
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        auto merged = read_learning_counts(path);
        for (const auto& [word, delta] : pending_learning_) {
            merged[word] = saturating_add(merged[word], delta);
        }
        const auto temporary = unique_learning_temporary(path);
        std::error_code ignored;
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                return false;
            }
            std::vector<std::pair<std::string, std::uint64_t>> learned(
                merged.begin(), merged.end());
            std::sort(learned.begin(), learned.end());
            for (const auto& [word, count] : learned) {
                if (count > 0U) {
                    output << word << '\t' << count << '\n';
                }
            }
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }
        if (replace_file_atomically(temporary, path)) {
            for (const auto& [word, count] : merged) {
                const auto found = entry_by_word_.find(word);
                if (found != entry_by_word_.end()) {
                    entries_[found->second].candidate.learning_count = (std::max)(
                        entries_[found->second].candidate.learning_count, count);
                }
            }
            pending_learning_.clear();
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
        std::uint32_t flags = 0U;
        std::uint64_t weight = 0U;
        if (!split_dictionary_row(line, word, weight_text, flags) || !is_ascii_word(word) ||
            !parse_positive_integer(weight_text, weight)) {
            continue;
        }
        const auto found = entry_by_word_.find(std::string(word));
        if (found == entry_by_word_.end()) {
            const std::size_t index = entries_.size();
            entries_.push_back({
                {next_id_++, std::string(word), weight, 0U, user_dictionary, flags},
                ascii_lower(word),
            });
            entry_by_word_.emplace(entries_.back().candidate.word, index);
        } else {
            auto& candidate = entries_[found->second].candidate;
            candidate.base_weight = (std::max)(candidate.base_weight, weight);
            candidate.user_entry = candidate.user_entry || user_dictionary;
            candidate.flags |= flags;
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
