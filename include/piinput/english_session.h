#pragma once

#include "piinput/english_lexicon.h"
#include "piinput/settings.h"

#include <cstddef>
#include <optional>
#include <string>

namespace piinput {

struct EnglishSessionSnapshot {
    std::string input;
    std::size_t caret{};
    std::vector<EnglishCandidate> candidates;
};

class EnglishSession final {
public:
    explicit EnglishSession(
        EnglishLexicon& lexicon,
        std::size_t candidate_limit = 90U,
        bool learning_enabled = true);

    [[nodiscard]] static bool should_start(
        bool english_mode,
        const EnglishSettings& settings) noexcept;

    bool insert(char character);
    bool backspace();
    bool delete_forward();
    bool move_left();
    bool move_right();
    bool move_home() noexcept;
    bool move_end() noexcept;
    void clear();

    [[nodiscard]] const EnglishSessionSnapshot& snapshot() const noexcept;
    [[nodiscard]] const std::string& raw_input() const noexcept;
    [[nodiscard]] std::optional<std::string> choose(std::size_t index);

private:
    void refresh();

    EnglishLexicon* lexicon_{};
    std::size_t candidate_limit_{90U};
    bool learning_enabled_{true};
    EnglishSessionSnapshot snapshot_;
};

}  // namespace piinput
