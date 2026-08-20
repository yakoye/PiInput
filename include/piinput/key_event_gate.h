#pragma once

namespace piinput {

enum class TsfKeyPhase {
    test_down,
    key_down,
    test_up,
    key_up,
};

enum class KeyIntent {
    ordinary,
    expand_candidates,
};

struct KeyEventDecision final {
    bool mutates_state{};
    bool expand_candidates{};
};

class KeyEventGate final {
public:
    [[nodiscard]] static KeyEventDecision decide(
        TsfKeyPhase phase,
        KeyIntent intent) noexcept;
};

}  // namespace piinput
