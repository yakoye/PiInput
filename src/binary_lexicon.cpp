#include "piinput/binary_lexicon.h"
#include "piinput/pinyin.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace piinput {
namespace {

constexpr std::array<char, 8> magic = {'L', 'I', 'M', 'E', 'L', 'E', 'X', '1'};
constexpr std::uint32_t format_version = 1U;
constexpr std::uint32_t record_size = 20U;
constexpr std::size_t header_size = 32U;

struct RawEntry {
    std::string word;
    std::string pinyin;
    std::uint32_t weight{};
};

struct Record {
    std::uint32_t word_offset{};
    std::uint32_t word_length{};
    std::uint32_t pinyin_offset{};
    std::uint32_t pinyin_length{};
    std::uint32_t weight{};
};

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

[[nodiscard]] std::vector<RawEntry> read_tsv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open TSV lexicon: " + path.string());
    }
    std::vector<RawEntry> entries;
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
        const auto parsed = std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), weight);
        if (parsed.ec != std::errc{} || parsed.ptr != fields[2].data() + fields[2].size()) {
            throw std::runtime_error("Invalid TSV weight at line " + std::to_string(line_number));
        }
        entries.push_back({std::string(fields[0]), std::string(fields[1]), weight});
    }
    return entries;
}

void write_u32(std::ostream& output, const std::uint32_t value) {
    const std::array<char, 4> bytes = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u64(std::ostream& output, const std::uint64_t value) {
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        output.put(static_cast<char>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint32_t checked_u32(const std::size_t value, const char* label) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(label) + " exceeds PiInput lexicon format limit");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t read_u32_at(
    const unsigned char* const bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64_at(
    const unsigned char* const bytes) noexcept {
    std::uint64_t value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        value |= static_cast<std::uint64_t>(bytes[shift / 8U]) << shift;
    }
    return value;
}

[[nodiscard]] std::size_t canonical_syllable_count(
    const std::string_view canonical) noexcept {
    if (canonical.empty()) return 0U;
    return static_cast<std::size_t>(
        std::count(canonical.begin(), canonical.end(), '\'')) + 1U;
}

}  // namespace

struct BinaryLexicon::Storage final {
#ifdef _WIN32
    HANDLE file{INVALID_HANDLE_VALUE};
    HANDLE mapping{};
#else
    int file{-1};
#endif
    const unsigned char* bytes{};
    std::size_t size{};
    std::size_t count{};
    std::size_t pool_offset{};
    std::size_t pool_size{};

    ~Storage() {
#ifdef _WIN32
        if (bytes != nullptr) UnmapViewOfFile(bytes);
        if (mapping != nullptr) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#else
        if (bytes != nullptr && size != 0U) munmap(
            const_cast<unsigned char*>(bytes), size);
        if (file >= 0) close(file);
#endif
    }

    [[nodiscard]] static std::shared_ptr<Storage> open(
        const std::filesystem::path& path) {
        auto result = std::make_shared<Storage>();
#ifdef _WIN32
        result->file = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (result->file == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Cannot open binary lexicon: " + path.string());
        }
        LARGE_INTEGER file_size{};
        if (GetFileSizeEx(result->file, &file_size) == FALSE ||
            file_size.QuadPart <= 0 ||
            static_cast<unsigned long long>(file_size.QuadPart) >
                static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)())) {
            throw std::runtime_error("Invalid binary lexicon file size");
        }
        result->size = static_cast<std::size_t>(file_size.QuadPart);
        result->mapping = CreateFileMappingW(
            result->file, nullptr, PAGE_READONLY, 0U, 0U, nullptr);
        if (result->mapping == nullptr) {
            throw std::runtime_error("Cannot create binary lexicon file mapping");
        }
        result->bytes = static_cast<const unsigned char*>(
            MapViewOfFile(result->mapping, FILE_MAP_READ, 0U, 0U, 0U));
        if (result->bytes == nullptr) {
            throw std::runtime_error("Cannot map binary lexicon read-only");
        }
#else
        result->file = ::open(path.c_str(), O_RDONLY);
        if (result->file < 0) {
            throw std::runtime_error("Cannot open binary lexicon: " + path.string());
        }
        struct stat status {};
        if (fstat(result->file, &status) != 0 || status.st_size <= 0 ||
            static_cast<std::uintmax_t>(status.st_size) >
                static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
            throw std::runtime_error("Invalid binary lexicon file size");
        }
        result->size = static_cast<std::size_t>(status.st_size);
        void* const view = mmap(
            nullptr, result->size, PROT_READ, MAP_PRIVATE, result->file, 0);
        if (view == MAP_FAILED) {
            throw std::runtime_error("Cannot map binary lexicon read-only");
        }
        result->bytes = static_cast<const unsigned char*>(view);
#endif
        if (result->size < header_size ||
            std::memcmp(result->bytes, magic.data(), magic.size()) != 0) {
            throw std::runtime_error("Invalid PiInput binary lexicon magic");
        }
        const std::uint32_t version = read_u32_at(result->bytes + 8U);
        const std::uint32_t entries = read_u32_at(result->bytes + 12U);
        const std::uint32_t stored_record_size = read_u32_at(result->bytes + 16U);
        const std::uint64_t pool_size_u64 = read_u64_at(result->bytes + 24U);
        if (version != format_version || stored_record_size != record_size) {
            throw std::runtime_error("Unsupported PiInput binary lexicon version");
        }
        const std::uint64_t records_bytes =
            static_cast<std::uint64_t>(entries) * record_size;
        const std::uint64_t pool_offset_u64 = header_size + records_bytes;
        const std::uint64_t expected_size = pool_offset_u64 + pool_size_u64;
        if (pool_offset_u64 > result->size || expected_size != result->size) {
            throw std::runtime_error("Invalid PiInput binary lexicon layout");
        }
        result->count = entries;
        result->pool_offset = static_cast<std::size_t>(pool_offset_u64);
        result->pool_size = static_cast<std::size_t>(pool_size_u64);

        std::string_view previous;
        for (std::size_t index = 0U; index < result->count; ++index) {
            const std::size_t record = header_size + index * record_size;
            const std::uint32_t word_offset = read_u32_at(result->bytes + record);
            const std::uint32_t word_length = read_u32_at(result->bytes + record + 4U);
            const std::uint32_t pinyin_offset = read_u32_at(result->bytes + record + 8U);
            const std::uint32_t pinyin_length = read_u32_at(result->bytes + record + 12U);
            if (static_cast<std::uint64_t>(word_offset) + word_length > result->pool_size ||
                static_cast<std::uint64_t>(pinyin_offset) + pinyin_length > result->pool_size) {
                throw std::runtime_error("Binary lexicon record points outside string pool");
            }
            const std::string_view current(
                reinterpret_cast<const char*>(result->bytes + result->pool_offset + pinyin_offset),
                pinyin_length);
            if (index != 0U && current < previous) {
                throw std::runtime_error("Binary lexicon records are not sorted by pinyin");
            }
            previous = current;
        }
        return result;
    }

    [[nodiscard]] std::uint32_t field(
        const std::size_t index, const std::size_t offset) const noexcept {
        return read_u32_at(bytes + header_size + index * record_size + offset);
    }

    [[nodiscard]] std::string_view word(const std::size_t index) const noexcept {
        const auto offset = field(index, 0U);
        const auto length = field(index, 4U);
        return {reinterpret_cast<const char*>(bytes + pool_offset + offset), length};
    }

    [[nodiscard]] std::string_view pinyin(const std::size_t index) const noexcept {
        const auto offset = field(index, 8U);
        const auto length = field(index, 12U);
        return {reinterpret_cast<const char*>(bytes + pool_offset + offset), length};
    }

    [[nodiscard]] LexiconCandidate candidate(const std::size_t index) const {
        return {std::string(word(index)), std::string(pinyin(index)), field(index, 16U)};
    }

    [[nodiscard]] std::size_t lower_pinyin(const std::string_view key) const noexcept {
        std::size_t first = 0U;
        std::size_t last = count;
        while (first < last) {
            const std::size_t middle = first + (last - first) / 2U;
            if (pinyin(middle) < key) first = middle + 1U;
            else last = middle;
        }
        return first;
    }
};

struct BinaryLexicon::ReverseWordIndex final {
    std::shared_mutex mutex;
    bool built{};
    std::vector<std::uint32_t> records;
};

struct BinaryLexicon::SimplifiedIndex final {
    std::shared_mutex mutex;
    bool built{};
    std::vector<std::pair<std::string, std::uint32_t>> keys;
};

void compile_tsv_to_binary(
    const std::filesystem::path& input_tsv,
    const std::filesystem::path& output_lex) {
    auto entries = read_tsv(input_tsv);
    std::stable_sort(entries.begin(), entries.end(), [](const RawEntry& left, const RawEntry& right) {
        if (left.pinyin != right.pinyin) {
            return left.pinyin < right.pinyin;
        }
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        return left.word < right.word;
    });

    entries.erase(std::unique(entries.begin(), entries.end(), [](const RawEntry& left, const RawEntry& right) {
        return left.word == right.word && left.pinyin == right.pinyin;
    }), entries.end());

    std::unordered_map<std::string, std::uint32_t> offsets;
    std::string pool;
    auto intern = [&offsets, &pool](const std::string& value) -> std::uint32_t {
        const auto found = offsets.find(value);
        if (found != offsets.end()) {
            return found->second;
        }
        const std::uint32_t offset = checked_u32(pool.size(), "String pool offset");
        pool.append(value);
        offsets.emplace(value, offset);
        return offset;
    };

    std::vector<Record> records;
    records.reserve(entries.size());
    for (const auto& entry : entries) {
        records.push_back({
            intern(entry.word),
            checked_u32(entry.word.size(), "Word length"),
            intern(entry.pinyin),
            checked_u32(entry.pinyin.size(), "Pinyin length"),
            entry.weight,
        });
    }

    std::ofstream output(output_lex, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create binary lexicon: " + output_lex.string());
    }
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    write_u32(output, format_version);
    write_u32(output, checked_u32(records.size(), "Entry count"));
    write_u32(output, record_size);
    write_u32(output, 0U);
    write_u64(output, static_cast<std::uint64_t>(pool.size()));
    for (const auto& record : records) {
        write_u32(output, record.word_offset);
        write_u32(output, record.word_length);
        write_u32(output, record.pinyin_offset);
        write_u32(output, record.pinyin_length);
        write_u32(output, record.weight);
    }
    output.write(pool.data(), static_cast<std::streamsize>(pool.size()));
    if (!output) {
        throw std::runtime_error("Failed while writing binary lexicon");
    }
}

void BinaryLexicon::load(const std::filesystem::path& path) {
    auto loaded = Storage::open(path);
    storage_ = std::move(loaded);
    reverse_word_index_ = std::make_shared<ReverseWordIndex>();
    simplified_index_ = std::make_shared<SimplifiedIndex>();
}

std::vector<LexiconCandidate> BinaryLexicon::query_exact(
    const std::string& pinyin,
    const std::size_t limit) const {
    if (!storage_ || pinyin.empty() || limit == 0U) return {};
    std::vector<LexiconCandidate> results;
    results.reserve(limit);
    for (std::size_t index = storage_->lower_pinyin(pinyin);
         index < storage_->count && storage_->pinyin(index) == pinyin &&
             results.size() < limit;
         ++index) {
        results.push_back(storage_->candidate(index));
    }
    return results;
}

std::vector<LexiconCandidate> BinaryLexicon::query_prefix(
    const std::string& pinyin_prefix,
    const std::size_t limit,
    const std::size_t scan_limit,
    const std::size_t max_syllables) const {
    if (!storage_ || pinyin_prefix.empty() || limit == 0U || scan_limit == 0U) {
        return {};
    }
    std::vector<LexiconCandidate> results;
    results.reserve((std::min)(limit, scan_limit));
    std::size_t scanned = 0U;
    std::size_t index = storage_->lower_pinyin(pinyin_prefix);
    while (index < storage_->count && scanned < scan_limit) {
        const std::string_view key = storage_->pinyin(index);
        if (!key.starts_with(pinyin_prefix)) break;
        std::size_t next = index + 1U;
        while (next < storage_->count && storage_->pinyin(next) == key) ++next;
        if (max_syllables != 0U && canonical_syllable_count(key) > max_syllables) {
            ++scanned;
            index = next;
            continue;
        }
        for (; index < next && scanned < scan_limit; ++index, ++scanned) {
            results.push_back(storage_->candidate(index));
        }
        index = next;
    }
    std::stable_sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        if (left.weight != right.weight) return left.weight > right.weight;
        if (left.pinyin.size() != right.pinyin.size()) {
            return left.pinyin.size() < right.pinyin.size();
        }
        if (left.pinyin != right.pinyin) return left.pinyin < right.pinyin;
        return left.word < right.word;
    });
    if (results.size() > limit) results.resize(limit);
    return results;
}

std::vector<LexiconCandidate> BinaryLexicon::query_word(
    const std::string_view word,
    const std::size_t limit) const {
    if (!storage_ || !reverse_word_index_ || word.empty() || limit == 0U) return {};
    {
        std::shared_lock read_lock(reverse_word_index_->mutex);
        if (!reverse_word_index_->built) {
            read_lock.unlock();
            std::unique_lock write_lock(reverse_word_index_->mutex);
            if (!reverse_word_index_->built) {
                reverse_word_index_->records.resize(storage_->count);
                std::iota(reverse_word_index_->records.begin(),
                    reverse_word_index_->records.end(), 0U);
                std::sort(reverse_word_index_->records.begin(),
                    reverse_word_index_->records.end(), [&](const auto left, const auto right) {
                        const auto left_word = storage_->word(left);
                        const auto right_word = storage_->word(right);
                        if (left_word != right_word) return left_word < right_word;
                        const auto left_weight = storage_->field(left, 16U);
                        const auto right_weight = storage_->field(right, 16U);
                        if (left_weight != right_weight) return left_weight > right_weight;
                        const auto left_pinyin = storage_->pinyin(left);
                        const auto right_pinyin = storage_->pinyin(right);
                        if (left_pinyin != right_pinyin) return left_pinyin < right_pinyin;
                        return left < right;
                    });
                reverse_word_index_->built = true;
            }
        }
    }
    std::shared_lock read_lock(reverse_word_index_->mutex);
    const auto first = std::lower_bound(
        reverse_word_index_->records.begin(), reverse_word_index_->records.end(), word,
        [&](const std::uint32_t index, const std::string_view key) {
            return storage_->word(index) < key;
        });
    std::vector<LexiconCandidate> results;
    results.reserve(limit);
    for (auto position = first; position != reverse_word_index_->records.end() &&
         storage_->word(*position) == word && results.size() < limit; ++position) {
        results.push_back(storage_->candidate(*position));
    }
    return results;
}

std::vector<LexiconCandidate> BinaryLexicon::query_simplified(
    const std::string& key,
    const std::vector<std::string>& syllable_filter,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    if (!storage_ || !simplified_index_ || key.empty() ||
        limit == 0U || scan_limit == 0U) return {};
    {
        std::shared_lock read_lock(simplified_index_->mutex);
        if (!simplified_index_->built) {
            read_lock.unlock();
            std::unique_lock write_lock(simplified_index_->mutex);
            if (!simplified_index_->built) {
                for (std::size_t index = 0U; index < storage_->count;) {
                    const std::string_view pinyin = storage_->pinyin(index);
                    simplified_index_->keys.emplace_back(
                        simplified_pinyin_key(pinyin), static_cast<std::uint32_t>(index));
                    do { ++index; }
                    while (index < storage_->count && storage_->pinyin(index) == pinyin);
                }
                std::sort(simplified_index_->keys.begin(), simplified_index_->keys.end(),
                    [&](const auto& left, const auto& right) {
                        if (left.first != right.first) return left.first < right.first;
                        return storage_->pinyin(left.second) < storage_->pinyin(right.second);
                    });
                simplified_index_->built = true;
            }
        }
    }
    const auto matches_filter = [&](const std::string_view pinyin) {
        if (syllable_filter.empty()) return true;
        std::size_t filter_index = 0U;
        std::size_t start = 0U;
        while (start <= pinyin.size() && filter_index < syllable_filter.size()) {
            const std::size_t separator = pinyin.find('\'', start);
            const std::size_t stop = separator == std::string_view::npos
                ? pinyin.size() : separator;
            const auto syllable = pinyin.substr(start, stop - start);
            const auto& required = syllable_filter[filter_index];
            if (!required.empty() && required != syllable) return false;
            ++filter_index;
            if (separator == std::string_view::npos) break;
            start = separator + 1U;
        }
        return filter_index == syllable_filter.size();
    };

    std::shared_lock read_lock(simplified_index_->mutex);
    auto position = std::lower_bound(
        simplified_index_->keys.begin(), simplified_index_->keys.end(), key,
        [](const auto& entry, const std::string& value) { return entry.first < value; });
    std::vector<LexiconCandidate> results;
    std::size_t scanned = 0U;
    for (; position != simplified_index_->keys.end() && position->first == key &&
         scanned < scan_limit; ++position, ++scanned) {
        const std::size_t first_record = position->second;
        const std::string_view pinyin = storage_->pinyin(first_record);
        if (!matches_filter(pinyin)) continue;
        for (std::size_t record = first_record;
             record < storage_->count && storage_->pinyin(record) == pinyin; ++record) {
            results.push_back(storage_->candidate(record));
        }
    }
    const std::size_t typed_syllables = key.size();
    std::stable_sort(results.begin(), results.end(), [&](const auto& left, const auto& right) {
        const bool left_complete = canonical_syllable_count(left.pinyin) == typed_syllables;
        const bool right_complete = canonical_syllable_count(right.pinyin) == typed_syllables;
        if (left_complete != right_complete) return left_complete;
        if (left.weight != right.weight) return left.weight > right.weight;
        if (left.pinyin.size() != right.pinyin.size()) {
            return left.pinyin.size() < right.pinyin.size();
        }
        return left.word < right.word;
    });
    if (results.size() > limit) results.resize(limit);
    return results;
}

std::size_t BinaryLexicon::entry_count() const noexcept {
    return storage_ ? storage_->count : 0U;
}

bool BinaryLexicon::memory_mapped() const noexcept {
    return storage_ != nullptr && storage_->bytes != nullptr;
}

std::size_t BinaryLexicon::mapped_bytes() const noexcept {
    return storage_ ? storage_->size : 0U;
}

bool is_binary_lexicon(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8> found_magic{};
    input.read(found_magic.data(), static_cast<std::streamsize>(found_magic.size()));
    return input && found_magic == magic;
}

}  // namespace piinput
