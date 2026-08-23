#include "pipe_server.h"

#include <share.h>

#include <cstdio>

#include "pipe_security.h"
#include "pipe_endpoint.h"
#include "candidate_presenter.h"
#include "session_manager.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <atomic>
#include <thread>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace piinput::windows {

namespace {

// Same opt-in switch and clock as the Shim's key trace, so the two logs line up
// on GetTickCount. Use a separate file because the Shim intentionally keeps its
// trace handle open with write sharing denied. Records stages only, never text.
std::FILE* host_trace() {
    static std::FILE* file = [] () -> std::FILE* {
        char temp[MAX_PATH]{};
        if (GetTempPathA(MAX_PATH, temp) == 0U) return nullptr;
        if (GetFileAttributesA((std::string(temp) + "piinput-key-trace.on").c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
            return nullptr;
        }
        const std::string path = std::string(temp) + "piinput-host-key-trace.csv";
        return _fsopen(path.c_str(), "a", _SH_DENYWR);
    }();
    return file;
}

void trace_host(const char* const stage) noexcept {
    std::FILE* const file = host_trace();
    if (file == nullptr) return;
    (void)std::fprintf(file, "%lu,%lu,%s,host\n",
        GetTickCount(), GetCurrentProcessId(), stage);
    (void)std::fflush(file);
}

}  // namespace


namespace {

std::vector<std::byte> payload_bytes(const std::string_view text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

// Reading straight into a 1 MiB protocol-ceiling vector allocated and
// zero-filled per message costs far more than every keystroke's actual work.
// The buffer below is allocated once, grows only when a message genuinely
// needs more room, and never shrinks.
class PipeMessageBuffer final {
public:
    [[nodiscard]] std::optional<std::span<const std::byte>> read(const HANDLE pipe) noexcept {
        constexpr std::size_t ceiling = host_header_bytes + host_max_payload_bytes;
        try {
            if (storage_.empty()) storage_.resize(initial_bytes);
        } catch (...) {
            return std::nullopt;
        }
        std::size_t offset = 0U;
        for (;;) {
            DWORD read = 0U;
            const BOOL complete = ReadFile(
                pipe, storage_.data() + offset,
                static_cast<DWORD>(storage_.size() - offset), &read, nullptr);
            offset += read;
            if (complete != FALSE) {
                return std::span<const std::byte>(storage_.data(), offset);
            }
            if (GetLastError() != ERROR_MORE_DATA || storage_.size() >= ceiling) {
                return std::nullopt;
            }
            try {
                storage_.resize((std::min)(ceiling, storage_.size() * 2U));
            } catch (...) {
                return std::nullopt;
            }
        }
    }

private:
    static constexpr std::size_t initial_bytes = 16U * 1024U;
    std::vector<std::byte> storage_;
};

// Waits for a client on a worker thread so the main thread can keep pumping
// window messages while it waits.
//
// The candidate window belongs to this thread, and window messages are only
// delivered to the thread that created the window. Blocking in
// ConnectNamedPipe therefore froze the candidate window for as long as nobody
// was typing: clicks queued up, the toolbar buttons did nothing, and Windows
// marked the window unresponsive. Keys still worked because the loop pumped
// once after each request, which also made an idle click appear to do nothing
// until the next keystroke.
//
// The pipe itself stays synchronous. Making the handle overlapped would have
// reached into every read and write on the hot path, and that path has already
// cost this project several rounds of stutter fixes.
class ConnectionWaiter final {
public:
    [[nodiscard]] bool start() noexcept {
        requested_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        completed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (requested_ == nullptr || completed_ == nullptr) return false;
        try {
            worker_ = std::thread([this] { serve(); });
        } catch (...) {
            return false;
        }
        return true;
    }

    // Blocks until a client connects, pumping window messages meanwhile.
    [[nodiscard]] bool wait_for_client(const HANDLE pipe) noexcept {
        pipe_.store(pipe, std::memory_order_release);
        SetEvent(requested_);
        for (;;) {
            const DWORD wait = MsgWaitForMultipleObjects(
                1U, &completed_, FALSE, INFINITE, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) {
                return connected_.load(std::memory_order_acquire);
            }
            if (wait != WAIT_OBJECT_0 + 1U) {
                return false;
            }
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }

    ~ConnectionWaiter() {
        stopping_.store(true, std::memory_order_release);
        if (requested_ != nullptr) SetEvent(requested_);
        if (worker_.joinable()) worker_.join();
        if (requested_ != nullptr) CloseHandle(requested_);
        if (completed_ != nullptr) CloseHandle(completed_);
    }

    ConnectionWaiter() = default;
    ConnectionWaiter(const ConnectionWaiter&) = delete;
    ConnectionWaiter& operator=(const ConnectionWaiter&) = delete;

private:
    void serve() noexcept {
        for (;;) {
            WaitForSingleObject(requested_, INFINITE);
            if (stopping_.load(std::memory_order_acquire)) return;
            const HANDLE pipe = pipe_.load(std::memory_order_acquire);
            const bool connected = pipe != INVALID_HANDLE_VALUE &&
                (ConnectNamedPipe(pipe, nullptr) != FALSE ||
                    GetLastError() == ERROR_PIPE_CONNECTED);
            connected_.store(connected, std::memory_order_release);
            SetEvent(completed_);
        }
    }

    std::thread worker_;
    HANDLE requested_{nullptr};
    HANDLE completed_{nullptr};
    std::atomic<HANDLE> pipe_{INVALID_HANDLE_VALUE};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};
};

bool write_pipe_message(const HANDLE pipe, const HostEnvelope& envelope) noexcept {
    try {
        const auto encoded = encode_host_envelope(envelope);
        DWORD written = 0U;
        return WriteFile(
            pipe, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr) != FALSE &&
            written == encoded.size();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] HANDLE create_server_pipe(
    const std::wstring& name,
    SECURITY_ATTRIBUTES* const security) noexcept {
    return CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        64U * 1024U,
        64U * 1024U,
        0U,
        security);
}

}  // namespace

std::wstring host_pipe_name() noexcept {
    const auto endpoint = current_host_endpoint_names();
    return endpoint.has_value() ? endpoint->pipe : std::wstring{};
}

std::optional<HostEnvelope> request_host(
    const HostMessageType type,
    const std::span<const std::byte> payload,
    std::uint64_t client_id,
    const std::uint64_t session_id,
    std::uint64_t sequence,
    const std::uint64_t generation) noexcept {
    const std::wstring name = host_pipe_name();
    if (name.empty()) return std::nullopt;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    const ULONGLONG deadline = GetTickCount64() + 500ULL;
    do {
        pipe = CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING, 0U, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY) return std::nullopt;
        if (GetTickCount64() >= deadline) return std::nullopt;
        if (error == ERROR_PIPE_BUSY) {
            (void)WaitNamedPipeW(name.c_str(), 25U);
        } else {
            Sleep(5U);
        }
    } while (GetTickCount64() < deadline);
    if (pipe == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) == FALSE) {
        CloseHandle(pipe);
        return std::nullopt;
    }
    if (client_id == 0U) client_id = static_cast<std::uint64_t>(GetCurrentProcessId());
    if (sequence == 0U) sequence = (std::max)(GetTickCount64(), 1ULL);
    HostEnvelope request{
        .version = host_protocol_v1,
        .client_id = client_id,
        .session_id = session_id,
        .sequence = sequence,
        .generation = generation,
        .type = type,
        .payload = {payload.begin(), payload.end()},
    };
    if (!write_pipe_message(pipe, request)) {
        CloseHandle(pipe);
        return std::nullopt;
    }
    PipeMessageBuffer buffer;
    const auto response_bytes = buffer.read(pipe);
    CloseHandle(pipe);
    if (!response_bytes.has_value()) {
        return std::nullopt;
    }
    ProtocolError error = ProtocolError::none;
    const auto response = decode_host_envelope(*response_bytes, error);
    return response;
}

PipeServer::PipeServer(
    std::string build_id,
    SessionManager* const sessions,
    CandidatePresenter* const presenter,
    const bool lexicon_memory_mapped,
    const std::size_t lexicon_mapped_bytes)
    : build_id_(std::move(build_id)),
      lexicon_memory_mapped_(lexicon_memory_mapped),
      lexicon_mapped_bytes_(lexicon_mapped_bytes),
      sessions_(sessions),
      presenter_(presenter) {}

void PipeServer::set_startup_duration(const std::uint64_t milliseconds) noexcept {
    startup_ms_ = milliseconds;
}

void PipeServer::set_composition_boundary_handler(std::function<void()> handler) {
    composition_boundary_handler_ = std::move(handler);
}

void PipeServer::set_stop_requested_handler(std::function<bool()> handler) {
    stop_requested_handler_ = std::move(handler);
}

int PipeServer::run() noexcept {
    const auto endpoint = current_host_endpoint_names();
    if (!endpoint.has_value()) return host_exit_failure;
    const auto security = PipeSecurity::create();
    const std::wstring& name = endpoint->pipe;
    if (!security.has_value() || name.empty()) {
        return host_exit_failure;
    }

    auto* const attributes = const_cast<SECURITY_ATTRIBUTES*>(&security->attributes());
    bool draining = false;
    PipeMessageBuffer request_buffer;
    ConnectionWaiter waiter;
    if (!waiter.start()) return host_exit_failure;
    HANDLE pipe = create_server_pipe(name, attributes);
    while (!draining && pipe != INVALID_HANDLE_VALUE) {
        const BOOL connected = waiter.wait_for_client(pipe) ? TRUE : FALSE;
        if (stop_requested_handler_ && stop_requested_handler_()) {
            draining = true;
        }
        // Keep one unconnected instance ready while the current request is
        // processed. A fast typist can then connect the next key immediately
        // instead of polling until this loop creates another named pipe.
        HANDLE next_pipe = create_server_pipe(name, attributes);
        bool trace_request_lifecycle = false;
        if (connected != FALSE) {
            const auto request_bytes = request_buffer.read(pipe);
            ProtocolError error = ProtocolError::none;
            const auto request = request_bytes.has_value()
                ? decode_host_envelope(*request_bytes, error)
                : std::nullopt;
            if (request.has_value() && request->type == HostMessageType::key_event) {
                trace_request_lifecycle = true;
                trace_host("host_key_decoded");
            } else if (request.has_value() &&
                request->type == HostMessageType::commit_result) {
                trace_request_lifecycle = true;
                trace_host("host_commit_decoded");
            }
            if (request.has_value() &&
                (request->type == HostMessageType::health || request->type == HostMessageType::drain)) {
                if (request->type == HostMessageType::drain) draining = true;
                const std::string response_text = request->type == HostMessageType::health
                    // build_id stays last: --health compares it against the
                    // end of the response to confirm the exact running build.
                    ? "protocol=" + std::to_string(host_protocol_current) +
                        "\nstartup_ms=" + std::to_string(startup_ms_) +
                        "\nversion=" + std::string(PIINPUT_VERSION) +
                        "\nlexicon_storage=" +
                            (lexicon_memory_mapped_ ? "mmap" : "heap") +
                        "\nlexicon_mapped_bytes=" +
                            std::to_string(lexicon_mapped_bytes_) +
                        "\nhost_pid=" + std::to_string(GetCurrentProcessId()) +
                        "\nbuild_id=" + build_id_
                    : "draining=yes";
                HostEnvelope response{
                    .version = request->version,
                    .client_id = request->client_id,
                    .session_id = request->session_id,
                    .sequence = request->sequence,
                    .generation = request->generation,
                    .type = request->type,
                    .payload = payload_bytes(response_text),
                };
                (void)write_pipe_message(pipe, response);
            } else if (request.has_value() && request->type == HostMessageType::caret &&
                presenter_ != nullptr) {
                HostPayloadError payload_error = HostPayloadError::none;
                const auto update = decode_host_caret_update(request->payload, payload_error);
                if (update.has_value() && update->generation == request->generation) {
                    // A pre-key probe arrives before the snapshot it belongs to
                    // exists, so it cannot be shown. It is still the truth about
                    // where the user is, and the next word opens there.
                    if (!presenter_->show_at(
                            request->client_id, request->session_id, *update)) {
                        presenter_->remember_caret(
                            request->client_id, request->session_id, *update);
                    }
                    HostEnvelope response{
                        .version = request->version,
                        .client_id = request->client_id,
                        .session_id = request->session_id,
                        .sequence = request->sequence,
                        .generation = request->generation,
                        .type = HostMessageType::caret,
                    };
                    (void)write_pipe_message(pipe, response);
                }
            } else if (request.has_value() && request->type == HostMessageType::focus &&
                presenter_ != nullptr && request->payload.size() == 1U) {
                const bool foreground = request->payload.front() == std::byte{1U};
                if (foreground) presenter_->focus(request->client_id, request->session_id);
                else presenter_->hide(request->client_id, request->session_id);
                HostEnvelope response{
                    .version = request->version,
                    .client_id = request->client_id,
                    .session_id = request->session_id,
                    .sequence = request->sequence,
                    .generation = request->generation,
                    .type = HostMessageType::focus,
                };
                (void)write_pipe_message(pipe, response);
            } else if (request.has_value() && sessions_ != nullptr) {
                if (request->type == HostMessageType::key_event &&
                    composition_boundary_handler_ &&
                    sessions_->at_composition_boundary(
                        request->client_id, request->session_id)) {
                    trace_host("host_boundary_begin");
                    composition_boundary_handler_();
                    trace_host("host_boundary_end");
                }
                if (request->type == HostMessageType::key_event) {
                    trace_host("host_key_received");
                }
                HostReply reply;
                const auto response = sessions_->dispatch(*request, &reply);
                if (request->type == HostMessageType::key_event) {
                    trace_host("host_key_answered");
                } else if (request->type == HostMessageType::commit_result) {
                    trace_host("host_commit_answered");
                }
                if (response.has_value()) {
                    if (!write_pipe_message(pipe, *response)) {
                        std::cerr << "PiInputHost: failed to write session response, win32="
                                  << GetLastError() << '\n';
                    }
                    if (request->type == HostMessageType::key_event) {
                        trace_host("host_key_response_written");
                    } else if (request->type == HostMessageType::commit_result) {
                        trace_host("host_commit_response_written");
                    }
                    if (presenter_ != nullptr && response->type == HostMessageType::key_reply) {
                        if (!reply.snapshot.raw.empty()) {
                            (void)presenter_->stage(
                                request->client_id, request->session_id, reply.snapshot);
                        } else {
                            presenter_->hide(request->client_id, request->session_id);
                        }
                    }
                } else {
                    std::cerr << "PiInputHost: session manager rejected request type="
                              << static_cast<std::uint32_t>(request->type)
                              << " sequence=" << request->sequence
                              << " payload=" << request->payload.size() << '\n';
                }
            } else if (!request.has_value()) {
                std::cerr << "PiInputHost: rejected pipe envelope, protocol_error="
                          << static_cast<int>(error) << '\n';
            }
        }
        // The standby instance above removes the next-key connection gap.
        // Flush only guarantees that this response has reached the client
        // before the current instance is disconnected; it no longer prevents
        // the next request from connecting in parallel.
        if (trace_request_lifecycle) trace_host("host_flush_begin");
        FlushFileBuffers(pipe);
        if (trace_request_lifecycle) trace_host("host_flush_end");
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        pipe = next_pipe;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);

    return draining ? host_exit_success : host_exit_failure;
}

}  // namespace piinput::windows
