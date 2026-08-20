#pragma once

#include "piinput/host_protocol.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace piinput::windows {

// True for keys whose Host reply can itself commit or cancel, i.e. start
// another final TSF edit. Ordinary Chinese letters only ever produce a
// composition update, so they never need to wait behind one.
[[nodiscard]] constexpr bool key_may_begin_final_edit(
    const HostKeyKind kind,
    const bool english_mode) noexcept {
    switch (kind) {
    case HostKeyKind::text:
        // Direct English echoes every letter back as a one-character commit.
        return english_mode;
    case HostKeyKind::move_left:
    case HostKeyKind::move_right:
    case HostKeyKind::move_home:
    case HostKeyKind::move_end:
    case HostKeyKind::previous_candidate:
    case HostKeyKind::next_candidate:
    case HostKeyKind::expand_next_row:
    case HostKeyKind::previous_row:
        return false;
    default:
        return true;
    }
}

// Serializes keys which arrive while an application is still completing a
// final TSF composition edit. Only one replayed key may be in flight because
// its reply decides whether another final edit barrier has begun.
class FinalEditKeyQueue final {
public:
    // A stuck reply must not let a held-down key grow this queue without bound
    // and replay a burst of stale characters minutes later.
    static constexpr std::size_t max_queued_keys = 64U;

    // Applications such as Notepad++ frequently refuse a synchronous edit
    // session, so a commit's TSF edit finishes only on a later message loop
    // turn. Holding *every* key for that window is what made typing right after
    // a punctuation stall and then arrive in a burst. A key that cannot start
    // another final edit is released immediately: its Host work runs in
    // parallel, and DeferredUpdateQueue still orders its composition edit after
    // the pending commit.
    [[nodiscard]] bool should_queue(const bool event_may_begin_final_edit) const noexcept {
        if (!events_.empty() || replay_inflight_) return true;
        return final_edit_pending_ && event_may_begin_final_edit;
    }

    void push(HostKeyEvent event) {
        if (events_.size() >= max_queued_keys) return;
        events_.push_back(std::move(event));
    }

    void begin_final_edit() noexcept { final_edit_pending_ = true; }

    [[nodiscard]] std::optional<HostKeyEvent> complete_final_edit() {
        final_edit_pending_ = false;
        return take_next();
    }

    [[nodiscard]] std::optional<HostKeyEvent> complete_replayed_reply(
        const bool begins_final_edit) {
        replay_inflight_ = false;
        if (begins_final_edit) {
            final_edit_pending_ = true;
            return std::nullopt;
        }
        return take_next();
    }

    void clear() noexcept {
        final_edit_pending_ = false;
        replay_inflight_ = false;
        events_.clear();
    }

    [[nodiscard]] bool final_edit_pending() const noexcept {
        return final_edit_pending_;
    }

    [[nodiscard]] bool replay_inflight() const noexcept { return replay_inflight_; }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

private:
    [[nodiscard]] std::optional<HostKeyEvent> take_next() {
        if (final_edit_pending_ || replay_inflight_ || events_.empty()) {
            return std::nullopt;
        }
        HostKeyEvent event = std::move(events_.front());
        events_.pop_front();
        replay_inflight_ = true;
        return event;
    }

    bool final_edit_pending_{};
    bool replay_inflight_{};
    std::deque<HostKeyEvent> events_;
};

}  // namespace piinput::windows
