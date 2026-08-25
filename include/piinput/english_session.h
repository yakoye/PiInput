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
        bool learning_enabled = true,
        std::vector<CustomShortcutSettings> shortcuts = {});

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
    void set_candidate_limit(std::size_t candidate_limit);
    void clear();

    [[nodiscard]] const EnglishSessionSnapshot& snapshot() const noexcept;
    [[nodiscard]] const std::string& raw_input() const noexcept;
    [[nodiscard]] std::optional<std::string> candidate(std::size_t index) const;
    [[nodiscard]] std::optional<std::string> action_target(std::size_t index) const;
    [[nodiscard]] std::optional<std::string> choose(std::size_t index);
    void restore(EnglishSessionSnapshot snapshot);

private:
    void refresh();

    EnglishLexicon* lexicon_{};
    std::size_t candidate_limit_{90U};
    bool learning_enabled_{true};
    std::vector<CustomShortcutSettings> shortcuts_;
    EnglishSessionSnapshot snapshot_;
};

}  // namespace piinput
