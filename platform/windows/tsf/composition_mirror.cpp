#include "composition_mirror.h"

#include <algorithm>

namespace piinput::windows {

CompositionMirror::CompositionMirror(
    const std::uint64_t client_id,
    const std::uint64_t session_id) noexcept
    : client_id_(client_id), session_id_(session_id) {}

MirrorRequest CompositionMirror::begin_request() noexcept {
    return {client_id_, session_id_, next_sequence_++, generation_};
}

bool CompositionMirror::confirm(
    const MirrorRequest& request,
    const HostReply& reply) {
    if (request.client_id != client_id_ || request.session_id != session_id_ ||
        request.sequence <= confirmed_sequence_ || reply.snapshot.generation < generation_) {
        return false;
    }
    confirmed_sequence_ = request.sequence;
    connected_ = true;
    if (reply.action == HostAction::commit || reply.action == HostAction::cancel ||
        reply.action == HostAction::launch_symbol_tool ||
        reply.action == HostAction::launch_settings) {
        pending_reply_ = reply;
        edit_pending_ = true;
        pending_edit_sequence_ = request.sequence;
        return true;
    }
    generation_ = reply.snapshot.generation;
    raw_ = reply.snapshot.raw;
    composition_text_ = reply.snapshot.composition_text.empty()
        ? raw_
        : reply.snapshot.composition_text;
    caret_ = (std::min)(reply.snapshot.caret, composition_text_.size());
    mode_ = reply.snapshot.mode;
    confirmed_snapshot_ = reply.snapshot;
    return true;
}

bool CompositionMirror::is_current_update(
    const MirrorRequest& request) const noexcept {
    return request.client_id == client_id_ && request.session_id == session_id_ &&
        request.sequence == confirmed_sequence_ && request.generation <= generation_;
}

std::optional<HostResumeState> CompositionMirror::complete_edit(
    const bool succeeded) noexcept {
    if (!edit_pending_) return std::nullopt;
    const bool has_newer_snapshot = confirmed_sequence_ > pending_edit_sequence_;
    if (succeeded && !has_newer_snapshot) {
        generation_ = pending_reply_.snapshot.generation;
        raw_ = pending_reply_.snapshot.raw;
        composition_text_ = pending_reply_.snapshot.composition_text.empty()
            ? raw_
            : pending_reply_.snapshot.composition_text;
        caret_ = (std::min)(pending_reply_.snapshot.caret, composition_text_.size());
        mode_ = pending_reply_.snapshot.mode;
        confirmed_snapshot_ = pending_reply_.snapshot;
    }
    pending_reply_ = {};
    edit_pending_ = false;
    pending_edit_sequence_ = 0U;
    return succeeded ? std::nullopt : std::optional<HostResumeState>(resume_state());
}

void CompositionMirror::reset_session(const std::uint64_t session_id) noexcept {
    session_id_ = session_id;
    next_sequence_ = 1U;
    confirmed_sequence_ = 0U;
    generation_ = 0U;
    raw_.clear();
    composition_text_.clear();
    caret_ = 0U;
    mode_ = HostInputMode::chinese;
    connected_ = false;
    edit_pending_ = false;
    pending_edit_sequence_ = 0U;
    confirmed_snapshot_ = {};
    pending_reply_ = {};
}

void CompositionMirror::discard_composition() noexcept {
    // Everything issued before the application ended its TSF composition is
    // obsolete.  Mark those sequences confirmed so a late pipe reply cannot
    // recreate text which the application has already finalized or removed.
    confirmed_sequence_ = next_sequence_ == 0U ? 0U : next_sequence_ - 1U;
    raw_.clear();
    composition_text_.clear();
    caret_ = 0U;
    edit_pending_ = false;
    pending_edit_sequence_ = 0U;
    confirmed_snapshot_ = {};
    pending_reply_ = {};
}

void CompositionMirror::disconnect() noexcept {
    connected_ = false;
}

void CompositionMirror::reconnect() noexcept {
    connected_ = true;
}

bool CompositionMirror::connected() const noexcept {
    return connected_;
}

std::uint64_t CompositionMirror::generation() const noexcept {
    return generation_;
}

const std::string& CompositionMirror::raw() const noexcept {
    return raw_;
}

const std::string& CompositionMirror::composition_text() const noexcept {
    return composition_text_;
}

std::size_t CompositionMirror::caret() const noexcept {
    return caret_;
}

const HostSnapshot& CompositionMirror::snapshot() const noexcept {
    return confirmed_snapshot_;
}

const std::string& CompositionMirror::pending_commit() const noexcept {
    static const std::string empty;
    return edit_pending_ ? pending_reply_.text : empty;
}

HostAction CompositionMirror::pending_action() const noexcept {
    return edit_pending_ ? pending_reply_.action : HostAction::none;
}

HostResumeState CompositionMirror::resume_state() const {
    return {generation_, raw_, (std::min)(caret_, raw_.size()), mode_};
}

}  // namespace piinput::windows
