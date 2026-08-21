#pragma once

#include <cstdint>

namespace piinput::windows {

struct ShimConnectionAttempt final {
    bool launch_host{};
    std::uint32_t wait_budget_ms{};
};

// A missing Host must never make every queued key repeat a long cold-start
// wait.  One request owns a short launch window; requests arriving during the
// cooldown still probe the pipe, but fail fast when it is not ready.
class ShimConnectionPolicy final {
public:
    [[nodiscard]] ShimConnectionAttempt plan_after_exchange_failure(
        const std::uint64_t now_ms) noexcept {
        if (now_ms < retry_after_ms_) return {};
        retry_after_ms_ = now_ms + retry_cooldown_ms;
        return {.launch_host = true, .wait_budget_ms = cold_start_wait_ms};
    }

    void record_success() noexcept { retry_after_ms_ = 0U; }

    // Measured cold start is about 650 ms warm and over 3 s with a large
    // dictionary on a cold disk, so 750 ms was under the real figure even in
    // the good case: the keys typed during it fell through as Latin letters.
    // The Host is normally warmed at activation now, so this budget only
    // covers the case where it died and has to be restarted mid-sentence --
    // waiting there is far better than emitting the wrong characters.
    static constexpr std::uint32_t cold_start_wait_ms = 2500U;
    static constexpr std::uint32_t retry_cooldown_ms = 1750U;

private:
    std::uint64_t retry_after_ms_{};
};

}  // namespace piinput::windows
