#pragma once

#include "liteime/engine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace liteime {

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
    explicit ImeSession(Engine& engine, std::string schema = "full");

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
    CandidateSnapshot snapshot_;
};

}  // namespace liteime
