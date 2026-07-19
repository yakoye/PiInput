#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace piinput {

struct ScelMetadata {
    std::string title;
    std::string category;
    std::string description;
    std::string samples;
    std::uint8_t format_mask{};
};

struct ScelEntry {
    std::string word;
    std::string pinyin;
    std::uint16_t weight{};
};

struct ScelDictionary {
    ScelMetadata metadata;
    std::unordered_map<std::uint16_t, std::string> pinyin_table;
    std::vector<ScelEntry> entries;
};

class ScelError final : public std::runtime_error {
public:
    explicit ScelError(const std::string& message);
};

class ScelParser final {
public:
    [[nodiscard]] ScelDictionary parse_file(const std::filesystem::path& path) const;
    [[nodiscard]] ScelDictionary parse_bytes(const std::vector<std::uint8_t>& bytes) const;
};

}  // namespace piinput
