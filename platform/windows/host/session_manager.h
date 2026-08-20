#pragma once

#include "piinput/host_messages.h"
#include "piinput/host_protocol.h"
#include "piinput/host_session.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace piinput::windows {

class SessionManager final {
public:
    SessionManager(
        Engine& engine,
        EnglishLexicon* english_lexicon,
        SettingsSnapshot settings,
        std::string schema);
    SessionManager(
        Engine& engine,
        EnglishLexicon* english_lexicon,
        SymbolIndex* symbol_index,
        SettingsSnapshot settings,
        std::string schema);

    [[nodiscard]] std::optional<HostEnvelope> dispatch(const HostEnvelope& request);
    // `reply_out`, when supplied, receives the reply the envelope was encoded
    // from. The Host presents candidates from it directly instead of decoding
    // its own payload back into strings on every keystroke.
    [[nodiscard]] std::optional<HostEnvelope> dispatch(
        const HostEnvelope& request,
        HostReply* reply_out);
    [[nodiscard]] std::optional<HostReply> open_symbol_center(
        std::uint64_t client_id,
        std::uint64_t session_id);
    [[nodiscard]] std::optional<HostReply> expand_candidates(
        std::uint64_t client_id,
        std::uint64_t session_id);
    [[nodiscard]] std::optional<HostReply> cancel_composition(
        std::uint64_t client_id,
        std::uint64_t session_id);
    [[nodiscard]] std::optional<HostReply> manage_candidate(
        std::uint64_t client_id,
        std::uint64_t session_id,
        std::uint64_t candidate_id,
        CandidateManagementAction action);
    [[nodiscard]] bool confirm_commit(
        std::uint64_t client_id,
        std::uint64_t session_id,
        const HostCommitResult& result);
    [[nodiscard]] std::optional<HostSnapshot> snapshot(
        std::uint64_t client_id,
        std::uint64_t session_id) const;
    // True when the session does not exist yet or holds no raw input, i.e. the
    // next key starts a new composition. Answering this without copying a whole
    // snapshot keeps the composition boundary off the allocation path.
    [[nodiscard]] bool at_composition_boundary(
        std::uint64_t client_id,
        std::uint64_t session_id) const noexcept;
    void update_settings(SettingsSnapshot settings, std::string schema);
    void set_user_model_dirty_handler(std::function<void()> handler);
    [[nodiscard]] std::size_t session_count() const noexcept;

private:
    struct Key final {
        std::uint64_t client{};
        std::uint64_t session{};
        bool operator==(const Key&) const = default;
    };

    struct KeyHash final {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
    };

    struct Managed final {
        std::unique_ptr<HostSession> session;
        std::uint64_t last_sequence{};
        // When this session was last spoken to, for eviction ordering.
        std::uint64_t last_touched{};
    };

    [[nodiscard]] Managed& get_or_create(const Key& key);
    // Sessions used to accumulate for the life of the Host, which runs from
    // login: every focus change in every application left one behind, each
    // holding its own decoder state. Keeping the recently used ones bounds
    // that without touching anything a user could still be typing into.
    void evict_stale_sessions();
    [[nodiscard]] std::unique_ptr<HostSession> make_session() const;

    Engine* engine_{};
    EnglishLexicon* english_lexicon_{};
    SymbolIndex* symbol_index_{};
    SettingsSnapshot settings_;
    std::string schema_;
    std::unordered_map<Key, Managed, KeyHash> sessions_;
    std::function<void()> user_model_dirty_handler_;
    std::uint64_t touch_counter_{};
};

}  // namespace piinput::windows
