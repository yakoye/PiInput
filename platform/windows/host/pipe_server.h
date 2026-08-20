#pragma once

#include "piinput/host_protocol.h"

#include <functional>
#include <optional>
#include <string>

namespace piinput::windows {

class SessionManager;
class CandidatePresenter;

inline constexpr int host_exit_success = 0;
inline constexpr int host_exit_already_running = 2;
inline constexpr int host_exit_failure = 3;

[[nodiscard]] std::wstring host_pipe_name() noexcept;
[[nodiscard]] std::optional<HostEnvelope> request_host(
    HostMessageType type,
    std::span<const std::byte> payload = {},
    std::uint64_t client_id = 0U,
    std::uint64_t session_id = 1U,
    std::uint64_t sequence = 0U,
    std::uint64_t generation = 1U) noexcept;

class PipeServer final {
public:
    PipeServer(
        std::string build_id,
        SessionManager* sessions,
        CandidatePresenter* presenter = nullptr);
    void set_composition_boundary_handler(std::function<void()> handler);
    // Asked between requests. Lets the tray's "exit" entry end the host without
    // reaching into the pipe loop.
    void set_stop_requested_handler(std::function<bool()> handler);
    [[nodiscard]] int run() noexcept;

private:
    std::string build_id_;
    SessionManager* sessions_{};
    CandidatePresenter* presenter_{};
    std::function<void()> composition_boundary_handler_;
    std::function<bool()> stop_requested_handler_;
};

}  // namespace piinput::windows
