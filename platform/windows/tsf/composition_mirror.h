#pragma once

#include "piinput/host_session.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace piinput::windows {

struct MirrorRequest final {
    std::uint64_t client_id{};
    std::uint64_t session_id{};
    std::uint64_t sequence{};
    std::uint64_t generation{};
};

class CompositionMirror final {
public:
    CompositionMirror(std::uint64_t client_id, std::uint64_t session_id) noexcept;

    [[nodiscard]] MirrorRequest begin_request() noexcept;
    [[nodiscard]] bool confirm(const MirrorRequest& request, const HostReply& reply);
    [[nodiscard]] bool is_current_update(const MirrorRequest& request) const noexcept;
    [[nodiscard]] std::optional<HostResumeState> complete_edit(bool succeeded) noexcept;
    void reset_session(std::uint64_t session_id) noexcept;
    void discard_composition() noexcept;
    void disconnect() noexcept;
    void reconnect() noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] const std::string& raw() const noexcept;
    [[nodiscard]] const std::string& composition_text() const noexcept;
    [[nodiscard]] std::size_t caret() const noexcept;
    [[nodiscard]] const HostSnapshot& snapshot() const noexcept;
    [[nodiscard]] const std::string& pending_commit() const noexcept;
    [[nodiscard]] HostResumeState resume_state() const;

private:
    std::uint64_t client_id_{};
    std::uint64_t session_id_{};
    std::uint64_t next_sequence_{1U};
    std::uint64_t confirmed_sequence_{};
    std::uint64_t generation_{};
    std::string raw_;
    std::string composition_text_;
    std::size_t caret_{};
    HostInputMode mode_{HostInputMode::chinese};
    bool connected_{true};
    bool edit_pending_{};
    std::uint64_t pending_edit_sequence_{};
    HostSnapshot confirmed_snapshot_;
    HostReply pending_reply_;
};

}  // namespace piinput::windows
