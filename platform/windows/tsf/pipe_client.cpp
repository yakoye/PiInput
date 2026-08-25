#include "pipe_client.h"

#include "piinput/host_messages.h"

#include <utility>

namespace piinput::windows {

PipeClient::PipeClient(Transport transport, ReplyCallback callback)
    : transport_(std::move(transport)),
      callback_(std::move(callback)),
      worker_([this] { run(); }) {}

PipeClient::~PipeClient() {
    stop();
}

bool PipeClient::send_key(
    const MirrorRequest& request,
    const HostKeyEvent& event) {
    return enqueue({
        .version = host_protocol_current,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = request.generation,
        .type = HostMessageType::key_event,
        .payload = encode_host_key_event(event),
    });
}

bool PipeClient::send_resume(
    const MirrorRequest& request,
    const HostResumeState& state) {
    return enqueue({
        .version = host_protocol_current,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = request.generation,
        .type = HostMessageType::resume,
        .payload = encode_host_resume_state(state),
    });
}

bool PipeClient::send_caret(
    const MirrorRequest& request,
    const HostCaretUpdate& update) {
    return enqueue({
        .version = host_protocol_current,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = request.generation,
        .type = HostMessageType::caret,
        .payload = encode_host_caret_update(update, host_protocol_current),
    });
}

bool PipeClient::send_focus(
    const MirrorRequest& request,
    const bool foreground) {
    return enqueue({
        .version = host_protocol_current,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = request.generation,
        .type = HostMessageType::focus,
        .payload = {foreground ? std::byte{1U} : std::byte{0U}},
    });
}

bool PipeClient::send_commit_result(
    const MirrorRequest& request,
    const HostCommitResult& result) {
    return enqueue({
        .version = host_protocol_current,
        .client_id = request.client_id,
        .session_id = request.session_id,
        .sequence = request.sequence,
        .generation = result.generation,
        .type = HostMessageType::commit_result,
        .payload = encode_host_commit_result(result),
    });
}

void PipeClient::stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            if (!worker_.joinable()) return;
        }
        stopping_ = true;
        queue_.clear();
    }
    ready_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}

bool PipeClient::enqueue(HostEnvelope envelope) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || queue_.size() >= max_queued_requests) return false;
        queue_.push_back(std::move(envelope));
    }
    ready_.notify_one();
    return true;
}

void PipeClient::run() noexcept {
    for (;;) {
        HostEnvelope request;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            request = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            const auto response = transport_(request);
            if (response.has_value()) callback_(*response);
        } catch (...) {
            // A transport or consumer failure disconnects only this request;
            // it must never unwind through an application-owned thread.
        }
    }
}

}  // namespace piinput::windows
