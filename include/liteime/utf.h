#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace liteime {

[[nodiscard]] std::string utf16le_to_utf8(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::size_t byte_length,
    bool stop_at_null = false);

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value);
[[nodiscard]] std::string wide_to_utf8(const wchar_t* value);
[[nodiscard]] std::wstring utf8_to_wide(std::string_view value);

}  // namespace liteime
