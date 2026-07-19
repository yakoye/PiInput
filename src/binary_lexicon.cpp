#include "piinput/binary_lexicon.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace piinput {
namespace {

constexpr std::array<char, 8> magic = {'L', 'I', 'M', 'E', 'L', 'E', 'X', '1'};
constexpr std::uint32_t format_version = 1U;
constexpr std::uint32_t record_size = 20U;

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

[[nodiscard]] std::uint32_t read_u32(std::istream& input, const char* label) {
    std::array<unsigned char, 4> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error(std::string("Unexpected end of binary lexicon while reading ") + label);
    }
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(std::istream& input, const char* label) {
    std::uint64_t value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
        const int character = input.get();
        if (character == std::char_traits<char>::eof()) {
            throw std::runtime_error(std::string("Unexpected end of binary lexicon while reading ") + label);
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(character)) << shift;
    }
    return value;
}

[[nodiscard]] std::uint32_t checked_u32(const std::size_t value, const char* label) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(label) + " exceeds PiInput lexicon format limit");
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

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
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open binary lexicon: " + path.string());
    }
    std::array<char, 8> found_magic{};
    input.read(found_magic.data(), static_cast<std::streamsize>(found_magic.size()));
    if (!input || found_magic != magic) {
        throw std::runtime_error("Invalid PiInput binary lexicon magic");
    }
    const std::uint32_t version = read_u32(input, "format version");
    const std::uint32_t entry_count = read_u32(input, "entry count");
    const std::uint32_t stored_record_size = read_u32(input, "record size");
    (void)read_u32(input, "reserved field");
    const std::uint64_t pool_size_u64 = read_u64(input, "string pool size");
    if (version != format_version || stored_record_size != record_size) {
        throw std::runtime_error("Unsupported PiInput binary lexicon version");
    }
    if (pool_size_u64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Binary lexicon string pool is too large");
    }

    std::vector<Record> records;
    records.reserve(entry_count);
    for (std::uint32_t index = 0U; index < entry_count; ++index) {
        records.push_back({
            read_u32(input, "word offset"),
            read_u32(input, "word length"),
            read_u32(input, "pinyin offset"),
            read_u32(input, "pinyin length"),
            read_u32(input, "weight"),
        });
    }
    std::string pool(static_cast<std::size_t>(pool_size_u64), '\0');
    input.read(pool.data(), static_cast<std::streamsize>(pool.size()));
    if (!input) {
        throw std::runtime_error("Truncated PiInput binary lexicon string pool");
    }

    std::vector<LexiconCandidate> entries;
    entries.reserve(records.size());
    for (const auto& record : records) {
        const std::uint64_t word_end = static_cast<std::uint64_t>(record.word_offset) + record.word_length;
        const std::uint64_t pinyin_end = static_cast<std::uint64_t>(record.pinyin_offset) + record.pinyin_length;
        if (word_end > pool.size() || pinyin_end > pool.size()) {
            throw std::runtime_error("Binary lexicon record points outside string pool");
        }
        entries.push_back({
            std::string(pool.data() + record.word_offset, record.word_length),
            std::string(pool.data() + record.pinyin_offset, record.pinyin_length),
            record.weight,
        });
    }
    lexicon_.load_entries(std::move(entries));
}

std::vector<LexiconCandidate> BinaryLexicon::query_exact(
    const std::string& pinyin,
    const std::size_t limit) const {
    return lexicon_.query_exact(pinyin, limit);
}

std::vector<LexiconCandidate> BinaryLexicon::query_prefix(
    const std::string& pinyin_prefix,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    return lexicon_.query_prefix(pinyin_prefix, limit, scan_limit);
}

std::size_t BinaryLexicon::entry_count() const noexcept {
    return lexicon_.entry_count();
}

bool is_binary_lexicon(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8> found_magic{};
    input.read(found_magic.data(), static_cast<std::streamsize>(found_magic.size()));
    return input && found_magic == magic;
}

}  // namespace piinput
