#include "piinput/key_event_gate.h"

namespace piinput {

KeyEventDecision KeyEventGate::decide(
    const TsfKeyPhase phase,
    const KeyIntent intent) noexcept {
    const bool formal = phase == TsfKeyPhase::key_down || phase == TsfKeyPhase::key_up;
    return {
        .mutates_state = formal,
        .expand_candidates = phase == TsfKeyPhase::key_down &&
            intent == KeyIntent::expand_candidates,
    };
}

}  // namespace piinput
