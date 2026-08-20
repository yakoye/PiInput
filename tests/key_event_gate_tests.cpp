#include "piinput/candidate_grid.h"
#include "piinput/key_event_gate.h"
#include "piinput/settings.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

piinput::CandidateSettings candidate_settings() {
    auto settings = piinput::default_settings().candidates;
    settings.items_per_row = 6U;
    settings.visible_rows = 3U;
    return settings;
}

void test_probe_events_never_mutate_or_expand() {
    using piinput::KeyEventGate;
    using piinput::KeyIntent;
    using piinput::TsfKeyPhase;

    check(!KeyEventGate::decide(TsfKeyPhase::test_down, KeyIntent::expand_candidates).mutates_state,
        "test-down must be side-effect free");
    check(!KeyEventGate::decide(TsfKeyPhase::test_down, KeyIntent::expand_candidates).expand_candidates,
        "test-down must not expand candidates");
    check(!KeyEventGate::decide(TsfKeyPhase::test_up, KeyIntent::expand_candidates).mutates_state,
        "test-up must be side-effect free");
}

void test_formal_equals_expands_after_letters_remain_collapsed() {
    using piinput::KeyEventGate;
    using piinput::KeyIntent;
    using piinput::TsfKeyPhase;

    piinput::CandidateGrid grid(candidate_settings(), 18U);
    for (const char ignored : std::string("drlojuzi")) {
        (void)ignored;
        const auto probe = KeyEventGate::decide(TsfKeyPhase::test_down, KeyIntent::ordinary);
        check(!probe.mutates_state, "letter probe must not mutate state");
        const auto formal = KeyEventGate::decide(TsfKeyPhase::key_down, KeyIntent::ordinary);
        check(formal.mutates_state, "formal letter key must mutate composition state");
        grid.reset(18U);
        check(grid.visible_rows() == 1U, "every new input generation must stay one row");
    }

    const auto equals = KeyEventGate::decide(
        TsfKeyPhase::key_down, KeyIntent::expand_candidates);
    check(equals.expand_candidates, "formal equals/down key must request expansion");
    grid.move_row(1);
    check(grid.visible_rows() == 3U, "formal equals/down expands configured rows");
}

}  // namespace

int main() {
    test_probe_events_never_mutate_or_expand();
    test_formal_equals_expands_after_letters_remain_collapsed();
    std::cout << "PiInput key event gate tests passed.\n";
    return 0;
}
