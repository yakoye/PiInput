#include "candidate_presenter.h"

#include "piinput/utf.h"

#include <algorithm>
#include <share.h>

#include <cstdio>
#include <utility>

namespace piinput::windows {

namespace {

// Opt-in placement trace. The candidate bar's placement is provably exact for a
// given caret rectangle -- the standalone piinput-caret-demo measured 44 out of
// 44 placements at dx=0 -- so what remains to be seen is the rectangle the Shim
// actually supplies. This records exactly that, next to nothing else: no keys,
// no text, no composition contents. Set PIINPUT_CARET_TRACE=1 to enable.
std::FILE* caret_trace() {
    static std::FILE* file = [] () -> std::FILE* {
        // A marker file rather than an environment variable: the Host is
        // normally started by the Shim inside another application, and would
        // inherit that application's environment, not the tester's.
        char temp[MAX_PATH]{};
        if (GetTempPathA(MAX_PATH, temp) == 0U) return nullptr;
        const std::string marker = std::string(temp) + "piinput-caret-trace.on";
        if (GetFileAttributesA(marker.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        // Append, and let other processes read while this one writes. fopen_s
        // takes the file exclusively and "w" discards the previous run; the
        // Shim restarts the Host on demand, so both would throw away exactly
        // the data being collected.
        const std::string path = std::string(temp) + "piinput-caret-trace.csv";
        const bool fresh = GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::FILE* const opened = _fsopen(path.c_str(), "a", _SH_DENYWR);
        if (opened != nullptr && fresh) {
            (void)std::fprintf(opened,
                "tick_ms,session,generation,word_opens,has_caret,"
                "caret_l,caret_t,caret_r,caret_b,bar_l,bar_t,bar_r,bar_b\n");
            (void)std::fflush(opened);
        }
        return opened;
    }();
    return file;
}

}  // namespace

bool candidate_session_changed(
    const std::uint64_t current_session,
    const std::uint64_t next_session) noexcept {
    return current_session != 0U && next_session != 0U && current_session != next_session;
}

std::uint64_t candidate_presentation_id(
    const std::uint64_t client_id,
    const std::uint64_t session_id) noexcept {
    return (client_id << 32U) ^ session_id;
}

bool CandidatePresenterModel::stage(
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    if (session_id == 0U) return false;
    const auto found = generations_.find(session_id);
    if (found != generations_.end() && snapshot.generation < found->second) return false;
    generations_[session_id] = snapshot.generation;
    const bool reuse_caret = visible_ && caret_available_ &&
        focused_session_ == session_id;
    const bool consume_early_caret = !reuse_caret &&
        remembered_session_ == session_id &&
        remembered_caret_.generation == snapshot.generation;
    staged_session_ = session_id;
    current_ = snapshot;
    if (reuse_caret) {
        caret_.generation = snapshot.generation;
        caret_available_ = true;
        caret_inherited_ = false;
    } else if (consume_early_caret) {
        // The Shim applies the composition and reports its caret after reading
        // the key reply. The Host writes that reply before staging the matching
        // snapshot, so a fast packaged host such as SearchHost can send the
        // caret back on another pipe first. That is not stale data: generation
        // and presentation identity make it the exact anchor for this snapshot.
        // Consume it now; otherwise the snapshot waits forever for a second
        // caret message that will never be sent.
        caret_ = remembered_caret_;
        caret_available_ = true;
        caret_inherited_ = false;
    } else {
        // A word that is opening has no trustworthy anchor yet. Every attempt to
        // guess one -- the previous word's caret, a pre-key probe -- put the bar
        // a step behind wherever the user actually was. The bar now waits for
        // the caret captured in the composition's own edit session, so it only
        // ever appears where the insertion point really is.
        //
        // The wait is one message-loop turn, and accuracy comes first here.
        caret_ = {
            .generation = snapshot.generation,
            .has_text_caret = false,
        };
        caret_available_ = false;
        caret_inherited_ = false;
    }
    focused_session_ = session_id;
    // Only a word already on screen keeps its anchor. Any other path here means
    // a word is opening, and a lock left over from the previous word would
    // freeze the bar at the previous word's position for this entire one.
    word_just_opened_ = !reuse_caret;
    if (!reuse_caret) anchor_locked_ = false;
    visible_ = !snapshot.raw.empty() && !snapshot.candidates.empty();
    return true;
}

bool CandidatePresenterModel::word_just_opened() const noexcept {
    return word_just_opened_;
}

void CandidatePresenterModel::remember_caret(
    const std::uint64_t session_id,
    const HostCaretUpdate& update) noexcept {
    if (session_id == 0U) return;
    // "No text geometry" is also a complete answer: once the matching
    // snapshot arrives it must open at the system-caret/mouse fallback. Dropping
    // that answer made packaged XAML hosts wait forever just as surely as
    // dropping a real rectangle did.
    remembered_caret_ = update;
    remembered_session_ = session_id;
}

bool CandidatePresenterModel::apply_caret(
    const std::uint64_t session_id,
    const HostCaretUpdate& update) {
    if (session_id == 0U || session_id != staged_session_ ||
        update.generation != current_.generation) {
        return false;
    }
    // The anchor a word opens on is a guess: it is whatever was remembered
    // before the key, and the pre-key probe can miss. Drawing on it keeps the
    // bar responsive, but it must never lock, or the authoritative caret that
    // arrives one round trip later is ignored and the bar stays wherever the
    // guess put it for the whole word.
    const bool provisional = provisional_pending_;
    provisional_pending_ = false;
    if (update.has_text_caret) {
        remembered_caret_ = update;
        remembered_session_ = session_id;
        // The insertion point walks right with every letter. Following it drags
        // the bar across the screen mid-word, so the anchor is taken once when
        // the composition opens and then held until the composition ends --
        // the same thing WeChat and Sogou do.
        if (anchor_locked_ && !current_.raw.empty()) {
            caret_.generation = update.generation;
            caret_available_ = true;
            caret_inherited_ = false;
            focused_session_ = session_id;
            visible_ = !current_.raw.empty() && !current_.candidates.empty();
            return true;
        }
        anchor_locked_ = !current_.raw.empty() && !provisional;
    } else if (remembered_session_ == session_id) {
        // Position unknown. Never keep guessing from an older one.
        remembered_caret_ = {};
        remembered_session_ = 0U;
    }
    caret_ = update;
    caret_available_ = true;
    caret_inherited_ = false;
    focused_session_ = session_id;
    visible_ = !current_.raw.empty() && !current_.candidates.empty();
    return true;
}

bool CandidatePresenterModel::show(
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    if (!stage(session_id, snapshot)) return false;
    return apply_caret(session_id, {
        .generation = snapshot.generation,
        .has_text_caret = false,
    });
}

void CandidatePresenterModel::hide(const std::uint64_t session_id) noexcept {
    if (session_id != focused_session_ && session_id != staged_session_) return;
    staged_session_ = 0U;
    focused_session_ = 0U;
    visible_ = false;
    caret_available_ = false;
    caret_inherited_ = false;
    anchor_locked_ = false;
    provisional_pending_ = false;
    current_ = {};
}

void CandidatePresenterModel::focus(const std::uint64_t session_id) noexcept {
    if (generations_.contains(session_id)) focused_session_ = session_id;
}

bool CandidatePresenterModel::caret_is_inherited() const noexcept {
    return caret_inherited_;
}

std::uint64_t CandidatePresenterModel::focused_session() const noexcept {
    return visible_ ? focused_session_ : 0U;
}

std::size_t CandidatePresenterModel::visible_rows() const noexcept {
    if (!visible_ || current_.candidates.empty()) return visible_ ? 1U : 0U;
    const std::size_t items_per_row = (std::max)(current_.view.items_per_row, std::size_t{1U});
    const std::size_t available_rows =
        (current_.candidates.size() + items_per_row - 1U) / items_per_row;
    return (std::min)((std::max)(current_.view.visible_rows, std::size_t{1U}), available_rows);
}

const HostSnapshot* CandidatePresenterModel::current_snapshot() const noexcept {
    return visible_ ? &current_ : nullptr;
}

const HostCaretUpdate* CandidatePresenterModel::current_caret() const noexcept {
    return visible_ && caret_available_ ? &caret_ : nullptr;
}

bool CandidatePresenter::create(const HINSTANCE instance) {
    window_.set_toolbar_handler([this](const CandidateToolbarAction action) {
        if (toolbar_handler_ && toolbar_client_id_ != 0U && toolbar_session_id_ != 0U) {
            toolbar_handler_(toolbar_client_id_, toolbar_session_id_, action);
        }
    });
    window_.set_candidate_select_handler([this](const std::size_t index) {
        const auto* snapshot = model_.current_snapshot();
        if (snapshot == nullptr || !candidate_select_handler_ || toolbar_client_id_ == 0U ||
            toolbar_session_id_ == 0U || index >= snapshot->candidates.size()) {
            return;
        }
        candidate_select_handler_(
            toolbar_client_id_, toolbar_session_id_, snapshot->candidates[index].id);
    });
    window_.set_candidate_context_handler(
        [this](const std::size_t index, const CandidateContextAction action) {
            const auto* snapshot = model_.current_snapshot();
            if (snapshot == nullptr || !candidate_context_handler_ || toolbar_client_id_ == 0U ||
                toolbar_session_id_ == 0U) {
                return;
            }
            if (action == CandidateContextAction::dismiss) {
                candidate_context_handler_(
                    toolbar_client_id_, toolbar_session_id_, 0U, action);
                return;
            }
            if (index >= snapshot->candidates.size()) return;
            candidate_context_handler_(
                toolbar_client_id_, toolbar_session_id_,
                snapshot->candidates[index].id, action);
        });
    return window_.create(instance);
}

void CandidatePresenter::set_candidate_select_handler(
    std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)> handler) {
    candidate_select_handler_ = std::move(handler);
}

void CandidatePresenter::set_toolbar_handler(
    std::function<void(
        std::uint64_t,
        std::uint64_t,
        CandidateToolbarAction)> handler) {
    toolbar_handler_ = std::move(handler);
}

void CandidatePresenter::set_candidate_context_handler(
    std::function<void(
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        CandidateContextAction)> handler) {
    candidate_context_handler_ = std::move(handler);
}

void CandidatePresenter::set_visual_settings(
    const CandidateVisualSettings visual) noexcept {
    visual_ = visual;
}

bool CandidatePresenter::stage(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    toolbar_client_id_ = client_id;
    toolbar_session_id_ = session_id;
    return stage(candidate_presentation_id(client_id, session_id), snapshot);
}

bool CandidatePresenter::show_at(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const HostCaretUpdate& update) {
    toolbar_client_id_ = client_id;
    toolbar_session_id_ = session_id;
    return show_at(candidate_presentation_id(client_id, session_id), update);
}

void CandidatePresenter::hide(
    const std::uint64_t client_id,
    const std::uint64_t session_id) noexcept {
    hide(candidate_presentation_id(client_id, session_id));
}

void CandidatePresenter::focus(
    const std::uint64_t client_id,
    const std::uint64_t session_id) noexcept {
    toolbar_client_id_ = client_id;
    toolbar_session_id_ = session_id;
    focus(candidate_presentation_id(client_id, session_id));
}

bool CandidatePresenter::stage(
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    if (candidate_session_changed(model_.focused_session(), session_id)) {
        window_.hide();
    }
    if (!model_.stage(session_id, snapshot)) return false;
    wide_valid_ = false;
    // A word that is opening must be free to anchor wherever the user now is.
    if (model_.word_just_opened()) window_.release_anchor();
    if (model_.current_snapshot() == nullptr) {
        window_.hide();
        return true;
    }
    const HostCaretUpdate* const caret = model_.current_caret();
    return caret == nullptr || show_at(session_id, *caret);
}

const std::vector<std::wstring>& CandidatePresenter::wide_candidates(
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    if (wide_valid_ && wide_session_ == session_id &&
        wide_generation_ == snapshot.generation) {
        return wide_candidates_;
    }
    wide_candidates_.clear();
    wide_candidates_.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
        wide_candidates_.push_back(utf8_to_wide(candidate.text));
    }
    wide_composition_ = utf8_to_wide(
        snapshot.composition_text.empty() ? snapshot.raw : snapshot.composition_text);
    wide_session_ = session_id;
    wide_generation_ = snapshot.generation;
    wide_valid_ = true;
    return wide_candidates_;
}

bool CandidatePresenter::show_at(
    const std::uint64_t session_id,
    const HostCaretUpdate& update) {
    if (!model_.apply_caret(session_id, update)) return false;
    const HostSnapshot* const snapshot = model_.current_snapshot();
    if (snapshot == nullptr || snapshot->raw.empty()) {
        window_.hide();
        return true;
    }
    if (!update.show_candidate_window) {
        // The application accepted the Shim's ITfCandidateListUIElement and
        // owns rendering (Windows Search is the primary case). A second,
        // cross-process popup would either be hidden behind the shell surface
        // or duplicate the integrated candidate row.
        window_.hide();
        return true;
    }
    // 在 update 之前设置：它会影响窗口高度，而高度是在 update 里连同内容一起
    // 定下来的。
    window_.set_app_shows_composition(update.app_shows_composition);
    const auto& candidates = wide_candidates(session_id, *snapshot);
    const std::size_t selected = snapshot->view.active_row *
        (std::max)(snapshot->view.items_per_row, std::size_t{1U}) +
        snapshot->view.active_column;
    window_.update(
        wide_composition_,
        candidates,
        selected,
        snapshot->view.active_row,
        snapshot->view.first_visible_row,
        snapshot->view.items_per_row,
        snapshot->view.visible_rows,
        visual_);
    if (update.has_text_caret) {
        const RECT anchor{update.left, update.top, update.right, update.bottom};
        if (model_.caret_is_inherited()) {
            window_.show_at_provisional_caret(anchor, update.owner_window);
        } else {
            window_.show_at_text_caret(anchor, update.owner_window);
        }
    } else {
        window_.show_near_caret(update.owner_window);
    }
    if (std::FILE* const trace = caret_trace(); trace != nullptr) {
        RECT placed{};
        const HWND bar = FindWindowExW(
            nullptr, nullptr, L"PiInputTsfCandidateWindow", nullptr);
        if (bar != nullptr) (void)GetWindowRect(bar, &placed);
        (void)std::fprintf(trace, "%lu,%llu,%llu,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
            GetTickCount(),
            static_cast<unsigned long long>(session_id),
            static_cast<unsigned long long>(update.generation),
            model_.word_just_opened() ? 1 : 0,
            update.has_text_caret ? 1 : 0,
            update.left, update.top, update.right, update.bottom,
            placed.left, placed.top, placed.right, placed.bottom);
        (void)std::fflush(trace);
    }
    return true;
}

bool CandidatePresenter::show(
    const std::uint64_t session_id,
    const HostSnapshot& snapshot) {
    if (!stage(session_id, snapshot)) return false;
    return show_at(session_id, {
        .generation = snapshot.generation,
        .has_text_caret = false,
    });
}

void CandidatePresenter::remember_caret(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const HostCaretUpdate& update) noexcept {
    model_.remember_caret(candidate_presentation_id(client_id, session_id), update);
}

void CandidatePresenter::hide(const std::uint64_t session_id) noexcept {
    model_.hide(session_id);
    wide_valid_ = false;
    if (model_.focused_session() == 0U) {
        toolbar_client_id_ = 0U;
        toolbar_session_id_ = 0U;
        window_.hide();
    }
}

void CandidatePresenter::focus(const std::uint64_t session_id) noexcept {
    model_.focus(session_id);
}

}  // namespace piinput::windows
