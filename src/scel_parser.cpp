#include "piinput/scel_parser.h"

#include "piinput/utf.h"

#include <fstream>
#include <limits>
#include <sstream>

namespace piinput {
namespace {

constexpr std::size_t kHeaderSize = 12U;
constexpr std::size_t kMetadataTitleOffset = 0x130U;
constexpr std::size_t kMetadataCategoryOffset = 0x338U;
constexpr std::size_t kMetadataDescriptionOffset = 0x540U;
constexpr std::size_t kMetadataSamplesOffset = 0xD40U;
constexpr std::size_t kPinyinTableOffset = 0x1540U;
constexpr std::size_t kWordTableOffset44 = 0x2628U;
constexpr std::size_t kWordTableOffset45 = 0x26C4U;

[[nodiscard]] std::string offset_message(const std::string& message, const std::size_t offset) {
    std::ostringstream stream;
    stream << message << " at offset 0x" << std::hex << offset;
    return stream.str();
}

void require_range(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::size_t length,
    const std::string& what) {
    if (offset > bytes.size() || length > bytes.size() - offset) {
        throw ScelError(offset_message("Unexpected end of SCEL while reading " + what, offset));
    }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::string& what) {
    require_range(bytes, offset, 2U, what);
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ScelError("Cannot open SCEL file: " + path.string());
    }

    const std::streampos end_position = input.tellg();
    if (end_position < 0) {
        throw ScelError("Cannot determine SCEL file size: " + path.string());
    }
    const auto size = static_cast<std::size_t>(end_position);
    std::vector<std::uint8_t> bytes(size);
    input.seekg(0, std::ios::beg);
    if (size != 0U) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!input) {
        throw ScelError("Cannot read complete SCEL file: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::string read_metadata_field(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t begin,
    const std::size_t end) {
    require_range(bytes, begin, end - begin, "metadata");
    return utf16le_to_utf8(bytes, begin, end - begin, true);
}

}  // namespace

ScelError::ScelError(const std::string& message) : std::runtime_error(message) {}

ScelDictionary ScelParser::parse_file(const std::filesystem::path& path) const {
    return parse_bytes(read_file(path));
}

ScelDictionary ScelParser::parse_bytes(const std::vector<std::uint8_t>& bytes) const {
    require_range(bytes, 0U, kHeaderSize, "header");

    if (bytes[0] != 0x40U || bytes[1] != 0x15U || bytes[2] != 0x00U || bytes[3] != 0x00U ||
        bytes[5] != 0x43U || bytes[6] != 0x53U || bytes[7] != 0x01U || bytes[8] != 0x01U) {
        throw ScelError("Unsupported or invalid SCEL header");
    }

    const std::uint8_t format_mask = bytes[4];
    std::size_t word_table_offset = 0U;
    if (format_mask == 0x44U) {
        word_table_offset = kWordTableOffset44;
    } else if (format_mask == 0x45U) {
        word_table_offset = kWordTableOffset45;
    } else {
        throw ScelError("Unsupported SCEL format mask: " + std::to_string(format_mask));
    }

    ScelDictionary dictionary;
    dictionary.metadata.format_mask = format_mask;
    dictionary.metadata.title = read_metadata_field(
        bytes, kMetadataTitleOffset, kMetadataCategoryOffset);
    dictionary.metadata.category = read_metadata_field(
        bytes, kMetadataCategoryOffset, kMetadataDescriptionOffset);
    dictionary.metadata.description = read_metadata_field(
        bytes, kMetadataDescriptionOffset, kMetadataSamplesOffset);
    dictionary.metadata.samples = read_metadata_field(
        bytes, kMetadataSamplesOffset, kPinyinTableOffset);

    require_range(bytes, kPinyinTableOffset, 4U, "pinyin table marker");
    std::size_t cursor = kPinyinTableOffset + 4U;
    bool found_last_pinyin = false;
    while (cursor < bytes.size()) {
        const std::uint16_t pinyin_index = read_u16(bytes, cursor, "pinyin index");
        const std::uint16_t pinyin_byte_length = read_u16(bytes, cursor + 2U, "pinyin length");
        cursor += 4U;
        if (pinyin_byte_length == 0U || (pinyin_byte_length % 2U) != 0U) {
            throw ScelError(offset_message("Invalid pinyin byte length", cursor - 2U));
        }
        require_range(bytes, cursor, pinyin_byte_length, "pinyin text");
        const std::string pinyin = utf16le_to_utf8(bytes, cursor, pinyin_byte_length, false);
        cursor += pinyin_byte_length;
        dictionary.pinyin_table.emplace(pinyin_index, pinyin);
        if (pinyin == "zuo") {
            found_last_pinyin = true;
            break;
        }
    }
    if (!found_last_pinyin) {
        throw ScelError("SCEL pinyin table did not end with 'zuo'");
    }

    require_range(bytes, word_table_offset, 4U, "word table");
    cursor = word_table_offset;
    while (cursor < bytes.size()) {
        require_range(bytes, cursor, 4U, "homophone group header");
        const std::uint16_t word_count = read_u16(bytes, cursor, "word count");
        const std::uint16_t pinyin_index_bytes = read_u16(bytes, cursor + 2U, "pinyin index byte length");
        cursor += 4U;

        if (word_count == 0U || pinyin_index_bytes == 0U || (pinyin_index_bytes % 2U) != 0U) {
            throw ScelError(offset_message("Invalid homophone group header", cursor - 4U));
        }
        require_range(bytes, cursor, pinyin_index_bytes, "pinyin index list");

        std::string joined_pinyin;
        for (std::size_t index_offset = 0U; index_offset < pinyin_index_bytes; index_offset += 2U) {
            const std::uint16_t pinyin_index = read_u16(
                bytes, cursor + index_offset, "pinyin table reference");
            const auto pinyin_it = dictionary.pinyin_table.find(pinyin_index);
            if (pinyin_it == dictionary.pinyin_table.end()) {
                throw ScelError(offset_message("Unknown pinyin table reference", cursor + index_offset));
            }
            if (!joined_pinyin.empty()) {
                joined_pinyin.push_back('\'');
            }
            joined_pinyin.append(pinyin_it->second);
        }
        cursor += pinyin_index_bytes;

        for (std::uint16_t word_index = 0U; word_index < word_count; ++word_index) {
            const std::uint16_t word_byte_length = read_u16(bytes, cursor, "word length");
            cursor += 2U;
            if (word_byte_length == 0U || (word_byte_length % 2U) != 0U) {
                throw ScelError(offset_message("Invalid word byte length", cursor - 2U));
            }
            require_range(bytes, cursor, word_byte_length, "word text");
            const std::string word = utf16le_to_utf8(bytes, cursor, word_byte_length, false);
            cursor += word_byte_length;

            const std::uint16_t extension_length = read_u16(bytes, cursor, "extension length");
            cursor += 2U;
            require_range(bytes, cursor, extension_length, "extension data");
            const std::uint16_t weight = extension_length >= 2U
                ? read_u16(bytes, cursor, "entry weight")
                : 0U;
            cursor += extension_length;

            dictionary.entries.push_back(ScelEntry{word, joined_pinyin, weight});
        }
    }

    return dictionary;
}

}  // namespace piinput
