#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

struct ParsedComposition {
    std::vector<std::string> syllables;
    std::string trailing_prefix;
    std::string canonical;
};

struct SegmentSelectionEntry {
    std::string word;
    std::string pinyin;
    std::size_t consumed_syllables{};
    // Set when this entry completed the unfinished syllable at the end of the
    // composition rather than one of the syllables already parsed.
    bool consumed_trailing_prefix{};
};

class SegmentSelection final {
public:
    void begin(ParsedComposition composition);
    void clear() noexcept;
    [[nodiscard]] bool stage(
        std::string word,
        std::string pinyin,
        std::size_t consumed_syllables);
    [[nodiscard]] bool undo();

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] std::size_t syllable_offset() const noexcept;
    [[nodiscard]] const ParsedComposition& composition() const noexcept;
    [[nodiscard]] const std::string& staged_text() const noexcept;
    [[nodiscard]] const std::vector<SegmentSelectionEntry>& history() const noexcept;
    [[nodiscard]] std::string remaining_pinyin() const;
    [[nodiscard]] std::string finish() const;

private:
    ParsedComposition composition_;
    std::vector<SegmentSelectionEntry> history_;
    std::string staged_text_;
    std::size_t syllable_offset_{};
    bool trailing_consumed_{};
    bool active_{};
};

}  // namespace piinput
