#include "session_manager.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace piinput::windows {

SessionManager::SessionManager(
    Engine& engine,
    EnglishLexicon* const english_lexicon,
    SettingsSnapshot settings,
    std::string schema)
    : SessionManager(
        engine, english_lexicon, nullptr, std::move(settings), std::move(schema)) {}

SessionManager::SessionManager(
    Engine& engine,
    EnglishLexicon* const english_lexicon,
    SymbolIndex* const symbol_index,
    SettingsSnapshot settings,
    std::string schema)
    : engine_(&engine),
      english_lexicon_(english_lexicon),
      symbol_index_(symbol_index),
      settings_(std::move(settings)),
      schema_(std::move(schema)) {}

std::optional<HostEnvelope> SessionManager::dispatch(const HostEnvelope& request) {
    return dispatch(request, nullptr);
}

std::optional<HostEnvelope> SessionManager::dispatch(
    const HostEnvelope& request,
    HostReply* const reply_out) {
    if (request.client_id == 0U || request.session_id == 0U || request.sequence == 0U ||
        (request.type != HostMessageType::key_event && request.type != HostMessageType::resume &&
            request.type != HostMessageType::commit_result)) {
        return std::nullopt;
    }
    const Key key{request.client_id, request.session_id};
    auto found = sessions_.find(key);
    if (found != sessions_.end() && request.sequence <= found->second.last_sequence) {
        return std::nullopt;
    }
    const bool created = found == sessions_.end();
    if (created && request.type == HostMessageType::commit_result) return std::nullopt;
    Managed& managed = created ? get_or_create(key) : found->second;
    managed.last_touched = ++touch_counter_;

    if (request.type == HostMessageType::commit_result) {
        HostPayloadError error = HostPayloadError::none;
        const auto result = decode_host_commit_result(request.payload, error);
        if (!result.has_value() || !managed.session->confirm_commit(
                result->generation, result->succeeded)) {
            return std::nullopt;
        }
        if (result->succeeded && user_model_dirty_handler_) user_model_dirty_handler_();
        managed.last_sequence = request.sequence;
        return HostEnvelope{
            .version = request.version,
            .client_id = request.client_id,
            .session_id = request.session_id,
            .sequence = request.sequence,
            .generation = result->generation,
            .type = HostMessageType::commit_result,
            .payload = encode_host_commit_result(*result),
        };
    }

    HostReply reply;
    if (request.type == HostMessageType::key_event) {
        HostPayloadError error = HostPayloadError::none;
        const auto event = decode_host_key_event(request.payload, error);
        if (!event.has_value()) return std::nullopt;
        if (created && event->resume.has_value()) {
            managed.session->restore(*event->resume);
        }
        reply = managed.session->apply(*event);
    } else {
        HostPayloadError error = HostPayloadError::none;
        const auto state = decode_host_resume_state(request.payload, error);
        if (!state.has_value()) return std::nullopt;
        managed.session->restore(*state);
        reply = {
            .accepted = true,
            .action = HostAction::update,
            .text = {},
            .snapshot = managed.session->snapshot(),
        };
    }
    managed.last_sequence = request.sequence;
    HostEnvelope envelope{
        .version = request.version,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = reply.snapshot.generation,
        .type = HostMessageType::key_reply,
        .payload = encode_host_reply(reply, request.version),
    };
    if (reply_out != nullptr) *reply_out = std::move(reply);
    return envelope;
}

bool SessionManager::confirm_commit(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const HostCommitResult& result) {
    const auto found = sessions_.find({client_id, session_id});
    const bool confirmed = found != sessions_.end() &&
        found->second.session->confirm_commit(result.generation, result.succeeded);
    if (confirmed && result.succeeded && user_model_dirty_handler_) {
        user_model_dirty_handler_();
    }
    return confirmed;
}

void SessionManager::set_user_model_dirty_handler(std::function<void()> handler) {
    user_model_dirty_handler_ = std::move(handler);
}

std::size_t SessionManager::session_count() const noexcept {
    return sessions_.size();
}

std::optional<HostReply> SessionManager::open_symbol_center(
    const std::uint64_t client_id,
    const std::uint64_t session_id) {
    const auto found = sessions_.find({client_id, session_id});
    if (found == sessions_.end()) return std::nullopt;
    return found->second.session->apply({.kind = HostKeyKind::open_symbol_center});
}

std::optional<HostReply> SessionManager::expand_candidates(
    const std::uint64_t client_id,
    const std::uint64_t session_id) {
    const auto found = sessions_.find({client_id, session_id});
    if (found == sessions_.end()) return std::nullopt;
    return found->second.session->apply({.kind = HostKeyKind::expand_next_row});
}

std::optional<HostReply> SessionManager::cancel_composition(
    const std::uint64_t client_id,
    const std::uint64_t session_id) {
    const auto found = sessions_.find({client_id, session_id});
    if (found == sessions_.end()) return std::nullopt;
    return found->second.session->apply({.kind = HostKeyKind::escape});
}

std::optional<HostReply> SessionManager::manage_candidate(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const std::uint64_t candidate_id,
    const CandidateManagementAction action) {
    const auto found = sessions_.find({client_id, session_id});
    if (found == sessions_.end()) return std::nullopt;
    auto reply = found->second.session->manage_candidate(candidate_id, action);
    if (reply.accepted && user_model_dirty_handler_) user_model_dirty_handler_();
    return reply;
}

std::optional<HostSnapshot> SessionManager::snapshot(
    const std::uint64_t client_id,
    const std::uint64_t session_id) const {
    const auto found = sessions_.find({client_id, session_id});
    return found == sessions_.end()
        ? std::nullopt
        : std::optional<HostSnapshot>{found->second.session->snapshot()};
}

bool SessionManager::at_composition_boundary(
    const std::uint64_t client_id,
    const std::uint64_t session_id) const noexcept {
    const auto found = sessions_.find({client_id, session_id});
    return found == sessions_.end() || !found->second.session->composing();
}

void SessionManager::update_settings(SettingsSnapshot settings, std::string schema) {
    if (settings_ == settings && schema_ == schema) return;
    settings_ = std::move(settings);
    schema_ = std::move(schema);
    for (auto& [key, managed] : sessions_) {
        (void)key;
        if (!managed.session->composing()) {
            const std::uint64_t previous_generation = managed.session->generation();
            auto replacement = make_session();
            replacement->start_after_generation(previous_generation);
            managed.session = std::move(replacement);
        }
    }
}

std::size_t SessionManager::KeyHash::operator()(const Key& key) const noexcept {
    const std::size_t left = std::hash<std::uint64_t>{}(key.client);
    const std::size_t right = std::hash<std::uint64_t>{}(key.session);
    return left ^ (right + 0x9e3779b9U + (left << 6U) + (left >> 2U));
}

SessionManager::Managed& SessionManager::get_or_create(const Key& key) {
    evict_stale_sessions();
    Managed managed;
    managed.session = make_session();
    return sessions_.emplace(key, std::move(managed)).first->second;
}

// Far more than the number of applications anyone has focused recently, and
// small enough that the decoders behind them cannot add up to anything. The
// point is a ceiling, not a tight budget.
namespace {
constexpr std::size_t max_live_sessions = 64U;
}  // namespace

void SessionManager::evict_stale_sessions() {
    if (sessions_.size() < max_live_sessions) return;
    // Purely least-recently-used. An earlier version also refused to evict a
    // session that was still composing, which sounded careful and was useless:
    // a window abandoned mid-word stays composing forever, so with enough of
    // them nothing was ever evictable. Recency covers the real concern anyway --
    // the session being typed into right now is by definition the newest one --
    // and a session that does get dropped is re-established by the resume path
    // the shim already uses after a Host restart.
    std::vector<std::pair<std::uint64_t, Key>> ages;
    ages.reserve(sessions_.size());
    for (const auto& [key, managed] : sessions_) {
        ages.emplace_back(managed.last_touched, key);
    }
    // Drop the oldest half rather than one per insert, so this scan runs once
    // every 32 new sessions instead of on every one.
    const std::size_t drop = max_live_sessions / 2U;
    std::nth_element(ages.begin(), ages.begin() + static_cast<std::ptrdiff_t>(drop),
        ages.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    for (std::size_t index = 0U; index < drop; ++index) {
        sessions_.erase(ages[index].second);
    }
}

std::unique_ptr<HostSession> SessionManager::make_session() const {
    return std::make_unique<HostSession>(
        *engine_, english_lexicon_, symbol_index_, settings_, schema_);
}

}  // namespace piinput::windows
