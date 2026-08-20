#pragma once

#include "piinput/host_messages.h"
#include "piinput/host_session.h"
#include "candidate_window.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace piinput::windows {

[[nodiscard]] bool candidate_session_changed(
    std::uint64_t current_session,
    std::uint64_t next_session) noexcept;

[[nodiscard]] std::uint64_t candidate_presentation_id(
    std::uint64_t client_id,
    std::uint64_t session_id) noexcept;

class CandidatePresenterModel final {
public:
    [[nodiscard]] bool stage(std::uint64_t session_id, const HostSnapshot& snapshot);
    // True when the last stage() opened a word rather than continuing one, so
    // the candidate window knows to give up the previous word's anchor.
    [[nodiscard]] bool word_just_opened() const noexcept;
    void remember_caret(std::uint64_t session_id, const HostCaretUpdate& update) noexcept;
    [[nodiscard]] bool apply_caret(std::uint64_t session_id, const HostCaretUpdate& update);
    [[nodiscard]] bool show(std::uint64_t session_id, const HostSnapshot& snapshot);
    void hide(std::uint64_t session_id) noexcept;
    void focus(std::uint64_t session_id) noexcept;

    [[nodiscard]] std::uint64_t focused_session() const noexcept;
    // True when the anchor was carried over from the previous composition
    // because a commit hid the window. It positions the bar immediately instead
    // of waiting a round trip, and stays correctable by the first real caret.
    [[nodiscard]] bool caret_is_inherited() const noexcept;
    [[nodiscard]] std::size_t visible_rows() const noexcept;
    [[nodiscard]] const HostSnapshot* current_snapshot() const noexcept;
    [[nodiscard]] const HostCaretUpdate* current_caret() const noexcept;

private:
    std::unordered_map<std::uint64_t, std::uint64_t> generations_;
    std::uint64_t staged_session_{};
    std::uint64_t focused_session_{};
    HostSnapshot current_;
    HostCaretUpdate caret_;
    // A commit clears the live caret, but the next word starts within a few
    // characters of it. Remembering it lets the first key of the next word show
    // its candidates at once rather than after the caret round trip.
    HostCaretUpdate remembered_caret_;
    std::uint64_t remembered_session_{};
    bool visible_{};
    bool caret_available_{};
    bool caret_inherited_{};
    bool anchor_locked_{};
    bool provisional_pending_{};
    bool word_just_opened_{};
};

class CandidatePresenter final {
public:
    [[nodiscard]] bool create(HINSTANCE instance);
    void set_toolbar_handler(std::function<void(
        std::uint64_t,
        std::uint64_t,
        CandidateToolbarAction)> handler);
    // Left click on a candidate, resolved to its id.
    void set_candidate_select_handler(std::function<void(
        std::uint64_t, std::uint64_t, std::uint64_t)> handler);
    void set_candidate_context_handler(std::function<void(
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        CandidateContextAction)> handler);
    void set_visual_settings(CandidateVisualSettings visual) noexcept;
    [[nodiscard]] bool stage(
        std::uint64_t client_id,
        std::uint64_t session_id,
        const HostSnapshot& snapshot);
    [[nodiscard]] bool show_at(
        std::uint64_t client_id,
        std::uint64_t session_id,
        const HostCaretUpdate& update);
    void remember_caret(
        std::uint64_t client_id, std::uint64_t session_id,
        const HostCaretUpdate& update) noexcept;
    void hide(std::uint64_t client_id, std::uint64_t session_id) noexcept;
    void focus(std::uint64_t client_id, std::uint64_t session_id) noexcept;
    [[nodiscard]] bool stage(std::uint64_t session_id, const HostSnapshot& snapshot);
    [[nodiscard]] bool show_at(std::uint64_t session_id, const HostCaretUpdate& update);
    [[nodiscard]] bool show(std::uint64_t session_id, const HostSnapshot& snapshot);
    void hide(std::uint64_t session_id) noexcept;
    void focus(std::uint64_t session_id) noexcept;

private:
    // Each key reaches show_at() twice: once when the reply is staged and once
    // when the shim reports the real text caret. Converting the whole candidate
    // page to UTF-16 both times is pure duplicate work, so the conversion is
    // cached per staged snapshot.
    [[nodiscard]] const std::vector<std::wstring>& wide_candidates(
        std::uint64_t session_id,
        const HostSnapshot& snapshot);

    CandidatePresenterModel model_;
    CandidateWindow window_;
    CandidateVisualSettings visual_{};
    std::vector<std::wstring> wide_candidates_;
    std::wstring wide_composition_;
    std::uint64_t wide_session_{};
    std::uint64_t wide_generation_{};
    bool wide_valid_{};
    std::function<void(
        std::uint64_t,
        std::uint64_t,
        CandidateToolbarAction)> toolbar_handler_;
    std::function<void(
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        CandidateContextAction)> candidate_context_handler_;
    std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)> candidate_select_handler_;
    std::uint64_t toolbar_client_id_{};
    std::uint64_t toolbar_session_id_{};
};

}  // namespace piinput::windows
