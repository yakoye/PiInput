#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace piinput {

struct SymbolCandidate {
    std::string symbol;
    std::string category;
    std::string name;
    std::vector<std::string> aliases;
    int score{};
};

class SymbolIndex final {
public:
    void load_tsv(const std::filesystem::path& path);

    [[nodiscard]] std::vector<SymbolCandidate> search(
        const std::string& query,
        std::size_t limit = 20U) const;
    [[nodiscard]] std::vector<SymbolCandidate> browse(std::size_t limit = 20U) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;

private:
    std::vector<SymbolCandidate> entries_;
};

}  // namespace piinput
