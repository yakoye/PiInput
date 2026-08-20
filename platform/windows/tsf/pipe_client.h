#pragma once

#include "composition_mirror.h"
#include "piinput/host_messages.h"
#include "piinput/host_protocol.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace piinput::windows {

class PipeClient final {
public:
    using Transport = std::function<std::optional<HostEnvelope>(const HostEnvelope&)>;
    using ReplyCallback = std::function<void(const HostEnvelope&)>;

    PipeClient(Transport transport, ReplyCallback callback);
    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;
    ~PipeClient();

    [[nodiscard]] bool send_key(const MirrorRequest& request, const HostKeyEvent& event);
    [[nodiscard]] bool send_resume(const MirrorRequest& request, const HostResumeState& state);
    [[nodiscard]] bool send_caret(const MirrorRequest& request, const HostCaretUpdate& update);
    [[nodiscard]] bool send_focus(const MirrorRequest& request, bool foreground);
    [[nodiscard]] bool send_commit_result(
        const MirrorRequest& request,
        const HostCommitResult& result);
    void stop() noexcept;

private:
    [[nodiscard]] bool enqueue(HostEnvelope envelope);
    void run() noexcept;

    static constexpr std::size_t max_queued_requests = 64U;
    Transport transport_;
    ReplyCallback callback_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<HostEnvelope> queue_;
    bool stopping_{};
    std::thread worker_;
};

}  // namespace piinput::windows
