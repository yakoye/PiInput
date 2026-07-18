#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace liteime {

struct LexiconCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t weight{};
};

class DevLexicon final {
public:
    void load_tsv(const std::filesystem::path& path);
    void load_entries(std::vector<LexiconCandidate> entries);

    [[nodiscard]] std::vector<LexiconCandidate> query_exact(
        const std::string& pinyin,
        std::size_t limit = 10) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    std::unordered_map<std::string, std::vector<LexiconCandidate>> entries_by_pinyin_;
    std::size_t entry_count_{};
};

}  // namespace liteime
