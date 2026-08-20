#pragma once

namespace piinput::windows {

struct CompositionEditPolicy final {
    bool finalize_before_selection{};
    bool selection_failure_is_fatal{true};
};

[[nodiscard]] constexpr CompositionEditPolicy composition_edit_policy(
    const bool commit,
    const bool cancel) noexcept {
    const bool finalizing = commit || cancel;
    return {
        .finalize_before_selection = finalizing,
        .selection_failure_is_fatal = !finalizing,
    };
}

}  // namespace piinput::windows
