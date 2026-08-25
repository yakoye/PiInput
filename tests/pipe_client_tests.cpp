#include "pipe_client.h"
#include "shim_connection_policy.h"
#include "shim_pipe_transport.h"

#include "piinput/host_messages.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_send_key_never_blocks_the_tsf_callback_thread() {
    std::mutex mutex;
    std::condition_variable ready;
    bool delivered = false;
    piinput::HostEnvelope delivered_envelope;

    piinput::windows::PipeClient client(
        [](const piinput::HostEnvelope& request) -> std::optional<piinput::HostEnvelope> {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            piinput::HostReply reply;
            reply.accepted = true;
            reply.action = piinput::HostAction::update;
            reply.snapshot.generation = request.generation + 1U;
            reply.snapshot.raw = "w";
            reply.snapshot.caret = 1U;
            return piinput::HostEnvelope{
                .version = piinput::host_protocol_v1,
                .client_id = request.client_id,
                .session_id = request.session_id,
                .sequence = request.sequence,
                .generation = reply.snapshot.generation,
                .type = piinput::HostMessageType::key_reply,
                .payload = piinput::encode_host_reply(reply),
            };
        },
        [&](const piinput::HostEnvelope& envelope) {
            std::lock_guard lock(mutex);
            delivered_envelope = envelope;
            delivered = true;
            ready.notify_one();
        });

    const auto start = std::chrono::steady_clock::now();
    check(client.send_key(
        {17U, 19U, 1U, 0U},
        {.kind = piinput::HostKeyKind::text, .character = 'w'}),
        "key request enters the bounded queue");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    check(elapsed < std::chrono::milliseconds(30),
        "send_key returns before slow transport work completes");

    std::unique_lock lock(mutex);
    check(ready.wait_for(lock, std::chrono::seconds(2), [&] { return delivered; }),
        "worker eventually delivers the Host reply");
    check(delivered_envelope.sequence == 1U && delivered_envelope.client_id == 17U,
        "reply identity is preserved across the worker queue");
}

void test_resume_uses_the_same_ordered_transport_and_shutdown_is_bounded() {
    std::atomic<int> calls{};
    piinput::windows::PipeClient client(
        [&](const piinput::HostEnvelope& request) -> std::optional<piinput::HostEnvelope> {
            ++calls;
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            check(piinput::decode_host_resume_state(request.payload, error).has_value(),
                "resume payload reaches transport intact");
            return std::nullopt;
        },
        [](const piinput::HostEnvelope&) {});
    check(client.send_resume(
        {2U, 3U, 4U, 5U},
        {5U, "hlheruhdlq", 6U, piinput::HostInputMode::chinese}),
        "resume request enters the queue");
    for (int attempt = 0; attempt < 100 && calls.load() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(calls.load() == 1, "resume request is processed exactly once");
    client.stop();
    check(!client.send_key(
        {2U, 3U, 5U, 5U}, {.kind = piinput::HostKeyKind::text, .character = 'x'}),
        "stopped client rejects new requests");
}

void test_caret_update_uses_reserved_message_type_and_preserves_identity() {
    std::mutex mutex;
    std::condition_variable ready;
    bool delivered = false;
    piinput::HostEnvelope observed;
    const piinput::HostCaretUpdate caret{
        .generation = 37U,
        .has_text_caret = true,
        .left = -800,
        .top = 120,
        .right = -798,
        .bottom = 144,
    };
    piinput::windows::PipeClient client(
        [&](const piinput::HostEnvelope& request) -> std::optional<piinput::HostEnvelope> {
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            const auto decoded = piinput::decode_host_caret_update(request.payload, error);
            check(request.type == piinput::HostMessageType::caret &&
                    request.client_id == 17U && request.session_id == 19U &&
                    request.sequence == 23U && request.generation == 37U,
                "caret request preserves its envelope identity");
            check(decoded.has_value() && *decoded == caret,
                "caret request preserves its typed geometry payload");
            return piinput::HostEnvelope{
                .version = piinput::host_protocol_v1,
                .client_id = request.client_id,
                .session_id = request.session_id,
                .sequence = request.sequence,
                .generation = request.generation,
                .type = piinput::HostMessageType::caret,
            };
        },
        [&](const piinput::HostEnvelope& envelope) {
            std::lock_guard lock(mutex);
            observed = envelope;
            delivered = true;
            ready.notify_one();
        });

    check(client.send_caret({17U, 19U, 23U, 37U}, caret),
        "caret update enters the ordered queue");
    std::unique_lock lock(mutex);
    check(ready.wait_for(lock, std::chrono::seconds(2), [&] { return delivered; }),
        "caret acknowledgement returns through the callback");
    check(observed.type == piinput::HostMessageType::caret && observed.payload.empty(),
        "caret acknowledgement remains distinct from key replies");
}

void test_focus_update_is_ordered_and_preserves_the_session_identity() {
    std::mutex mutex;
    std::condition_variable ready;
    bool delivered = false;
    piinput::HostEnvelope observed;
    piinput::windows::PipeClient client(
        [&](const piinput::HostEnvelope& request) -> std::optional<piinput::HostEnvelope> {
            check(request.type == piinput::HostMessageType::focus &&
                    request.client_id == 31U && request.session_id == 41U &&
                    request.sequence == 59U && request.generation == 26U,
                "focus request preserves its envelope identity");
            check(request.payload.size() == 1U && request.payload.front() == std::byte{0U},
                "focus=false has an explicit one-byte payload");
            return piinput::HostEnvelope{
                .version = request.version,
                .client_id = request.client_id,
                .session_id = request.session_id,
                .sequence = request.sequence,
                .generation = request.generation,
                .type = piinput::HostMessageType::focus,
            };
        },
        [&](const piinput::HostEnvelope& envelope) {
            std::lock_guard lock(mutex);
            observed = envelope;
            delivered = true;
            ready.notify_one();
        });

    check(client.send_focus({31U, 41U, 59U, 26U}, false),
        "focus update enters the same ordered queue as keys and resume");
    std::unique_lock lock(mutex);
    check(ready.wait_for(lock, std::chrono::seconds(2), [&] { return delivered; }),
        "focus acknowledgement returns through the callback");
    check(observed.type == piinput::HostMessageType::focus,
        "focus acknowledgement remains distinct from key replies");
}

void test_disconnected_host_waits_once_then_fails_fast_during_cooldown() {
    piinput::windows::ShimConnectionPolicy policy;

    const auto first = policy.plan_after_exchange_failure(100U);
    check(first.launch_host, "first unavailable request launches the resident Host");
    // The budget has to outlast a real cold start, or the keys typed during it
    // fall through as Latin letters and the input method looks broken for the
    // first seconds after a sign-in. Measured cold start is about 650 ms warm
    // and over 3 s with a large dictionary on a cold disk. It still has to be
    // bounded, so a Host that never comes back cannot hang typing outright.
    check(first.wait_budget_ms >= 1500U,
        "one cold Host start is given long enough to actually finish");
    check(first.wait_budget_ms <= 4000U,
        "a Host that never answers still cannot stall typing indefinitely");
    check(first.wait_budget_ms == piinput::windows::ShimConnectionPolicy::cold_start_wait_ms,
        "the planned budget is the one the policy publishes");

    const auto queued = policy.plan_after_exchange_failure(850U);
    check(!queued.launch_host && queued.wait_budget_ms == 0U,
        "queued keys never repeat the cold-start wait after one failed window");

    const auto still_cooling = policy.plan_after_exchange_failure(1849U);
    check(!still_cooling.launch_host && still_cooling.wait_budget_ms == 0U,
        "the reconnect cooldown keeps a failed Host from stalling every key");

    const auto retry = policy.plan_after_exchange_failure(1850U);
    check(retry.launch_host && retry.wait_budget_ms == piinput::windows::ShimConnectionPolicy::cold_start_wait_ms,
        "one bounded relaunch is allowed after the cooldown expires");

    policy.record_success();
    const auto new_outage = policy.plan_after_exchange_failure(1900U);
    check(new_outage.launch_host && new_outage.wait_budget_ms == piinput::windows::ShimConnectionPolicy::cold_start_wait_ms,
        "a successful connection resets the circuit for a later independent outage");
}

void test_installed_tools_resolve_beside_the_active_host_not_the_shim() {
    const std::filesystem::path active_host =
        L"C:/Users/test/AppData/Local/PiInput/bin/PiInputHost.exe";
    check(piinput::windows::program_beside_host(active_host, L"yesymbol.exe") ==
            active_host.parent_path() / L"yesymbol.exe",
        "symbol tool resolves beside CurrentHostPath");
    check(piinput::windows::program_beside_host(active_host, L"PiInput-Settings.exe") ==
            active_host.parent_path() / L"PiInput-Settings.exe",
        "settings resolves beside CurrentHostPath");
}

}  // namespace

int main() {
    test_send_key_never_blocks_the_tsf_callback_thread();
    test_resume_uses_the_same_ordered_transport_and_shutdown_is_bounded();
    test_caret_update_uses_reserved_message_type_and_preserves_identity();
    test_focus_update_is_ordered_and_preserves_the_session_identity();
    test_disconnected_host_waits_once_then_fails_fast_during_cooldown();
    test_installed_tools_resolve_beside_the_active_host_not_the_shim();
    std::cout << "PiInput pipe client tests passed.\n";
    return 0;
}
