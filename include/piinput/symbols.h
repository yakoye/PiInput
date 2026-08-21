#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
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

    // Typed-name shortcuts, keyed by alias: pai and pi both reach π. Only the
    // spelled-out aliases are used, never the Chinese names -- the key is
    // matched against what the user typed, and nobody types 圆周率 to get π.
    //
    // Capped per key because several aliases name a whole family: sanjiao
    // covers eight triangles and xingzuo twelve signs, and inserting all of
    // them would fill the candidate row and push out the words. The rest stay
    // reachable through the symbol search.
    static constexpr std::size_t max_shortcuts_per_key = 5U;
    [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
        shortcuts_by_alias() const;

private:
    std::vector<SymbolCandidate> entries_;
};

}  // namespace piinput
