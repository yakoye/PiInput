#include "piinput/utf.h"

#include <cwchar>
#include <limits>
#include <stdexcept>

#include "piinput/windows_compat.h"

namespace piinput {
namespace {

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0x10FFFFU) {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        throw std::runtime_error("Invalid Unicode code point");
    }
}

}  // namespace

std::string utf16le_to_utf8(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const std::size_t byte_length,
    const bool stop_at_null) {
    if ((byte_length % 2U) != 0U || offset > bytes.size() || byte_length > bytes.size() - offset) {
        throw std::runtime_error("Invalid UTF-16LE byte range");
    }

    std::string output;
    output.reserve(byte_length);

    const std::size_t end = offset + byte_length;
    std::size_t cursor = offset;
    while (cursor < end) {
        const std::uint16_t first = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[cursor]) |
            (static_cast<std::uint16_t>(bytes[cursor + 1U]) << 8U));
        cursor += 2U;

        if (first == 0U && stop_at_null) {
            break;
        }

        std::uint32_t code_point = first;
        if (first >= 0xD800U && first <= 0xDBFFU) {
            if (cursor >= end) {
                throw std::runtime_error("Truncated UTF-16 surrogate pair");
            }
            const std::uint16_t second = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[cursor]) |
                (static_cast<std::uint16_t>(bytes[cursor + 1U]) << 8U));
            cursor += 2U;
            if (second < 0xDC00U || second > 0xDFFFU) {
                throw std::runtime_error("Invalid UTF-16 surrogate pair");
            }
            code_point = 0x10000U +
                ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                (static_cast<std::uint32_t>(second) - 0xDC00U);
        } else if (first >= 0xDC00U && first <= 0xDFFFU) {
            throw std::runtime_error("Unexpected UTF-16 low surrogate");
        }

        append_utf8(output, code_point);
    }

    return output;
}

std::filesystem::path path_from_utf8(const std::string& value) {
#ifdef _WIN32
    return std::filesystem::path(utf8_to_wide(value));
#else
    return std::filesystem::path(value);
#endif
}

std::string wide_to_utf8(const wchar_t* value) {
    if (value == nullptr) {
        return {};
    }
#ifdef _WIN32
    const int length = static_cast<int>(wcslen(value));
    if (length == 0) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw std::runtime_error("WideCharToMultiByte size query failed");
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8, 0, value, length, result.data(), required, nullptr, nullptr);
    if (written != required) {
        throw std::runtime_error("WideCharToMultiByte conversion failed");
    }
    return result;
#else
    std::string result;
    while (*value != L'\0') {
        append_utf8(result, static_cast<std::uint32_t>(*value));
        ++value;
    }
    return result;
#endif
}


std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
#ifdef _WIN32
    const int input_length = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_length, nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("MultiByteToWideChar size query failed");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_length, result.data(), required);
    if (written != required) {
        throw std::runtime_error("MultiByteToWideChar conversion failed");
    }
    return result;
#else
    std::wstring result;
    for (std::size_t index = 0U; index < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        std::uint32_t code_point = 0U;
        std::size_t length = 0U;
        if (first < 0x80U) {
            code_point = first;
            length = 1U;
        } else if ((first & 0xE0U) == 0xC0U) {
            code_point = first & 0x1FU;
            length = 2U;
        } else if ((first & 0xF0U) == 0xE0U) {
            code_point = first & 0x0FU;
            length = 3U;
        } else if ((first & 0xF8U) == 0xF0U) {
            code_point = first & 0x07U;
            length = 4U;
        } else {
            throw std::runtime_error("Invalid UTF-8 leading byte");
        }
        if (index + length > value.size()) {
            throw std::runtime_error("Truncated UTF-8 sequence");
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::runtime_error("Invalid UTF-8 continuation byte");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if constexpr (sizeof(wchar_t) >= 4U) {
            result.push_back(static_cast<wchar_t>(code_point));
        } else {
            if (code_point <= 0xFFFFU) {
                result.push_back(static_cast<wchar_t>(code_point));
            } else {
                code_point -= 0x10000U;
                result.push_back(static_cast<wchar_t>(0xD800U + (code_point >> 10U)));
                result.push_back(static_cast<wchar_t>(0xDC00U + (code_point & 0x3FFU)));
            }
        }
        index += length;
    }
    return result;
#endif
}

}  // namespace piinput
