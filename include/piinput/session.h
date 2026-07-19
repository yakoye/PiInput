#pragma once

#include "piinput/engine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace piinput {

struct SessionCandidate {
    std::uint64_t id{};
    EngineCandidate candidate;
};

struct CandidateSnapshot {
    std::uint64_t generation{};
    std::string input;
    std::size_t caret{};
    std::vector<SessionCandidate> candidates;
};

class ImeSession final {
public:
    explicit ImeSession(Engine& engine, std::string schema = "full", std::size_t candidate_limit = 90U);

    void set_schema(std::string schema);
    void set_input(std::string input);
    void insert(char character);
    bool backspace();
    bool delete_forward();
    bool move_left();
    bool move_right();
    void move_home() noexcept;
    void move_end() noexcept;
    void clear();

    [[nodiscard]] const CandidateSnapshot& snapshot() const noexcept;
    [[nodiscard]] std::optional<std::string> choose(std::uint64_t candidate_id);

private:
    void refresh();

    Engine* engine_{};
    std::string schema_;
    std::size_t candidate_limit_{90U};
    CandidateSnapshot snapshot_;
};

}  // namespace piinput
