#pragma once

#include "piinput/host_protocol.h"
#include "piinput/host_session.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace piinput {

inline constexpr std::size_t host_max_candidates = 256U;
inline constexpr std::size_t host_max_text_bytes = 64U * 1024U;

enum class HostPayloadError {
    none,
    truncated,
    unknown_value,
    too_large,
    trailing_bytes,
};

struct HostCaretUpdate final {
    std::uint64_t generation{};
    bool has_text_caret{};
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
    // Top-level text-view window reported by the in-process TSF shim. The
    // out-of-process Host uses it as the candidate popup owner so immersive
    // surfaces such as Windows Search keep the popup in their z-order group.
    std::uint64_t owner_window{};

    bool operator==(const HostCaretUpdate&) const = default;
};

struct HostCommitResult final {
    std::uint64_t generation{};
    bool succeeded{};

    bool operator==(const HostCommitResult&) const = default;
};

[[nodiscard]] std::vector<std::byte> encode_host_key_event(const HostKeyEvent& event);
[[nodiscard]] std::optional<HostKeyEvent> decode_host_key_event(
    std::span<const std::byte> input,
    HostPayloadError& error);

[[nodiscard]] std::vector<std::byte> encode_host_resume_state(const HostResumeState& state);
[[nodiscard]] std::optional<HostResumeState> decode_host_resume_state(
    std::span<const std::byte> input,
    HostPayloadError& error);

[[nodiscard]] std::vector<std::byte> encode_host_caret_update(
    const HostCaretUpdate& update,
    std::uint32_t protocol_version = host_protocol_current);
[[nodiscard]] std::optional<HostCaretUpdate> decode_host_caret_update(
    std::span<const std::byte> input,
    HostPayloadError& error,
    std::uint32_t protocol_version = host_protocol_current);

[[nodiscard]] std::vector<std::byte> encode_host_commit_result(
    const HostCommitResult& result);
[[nodiscard]] std::optional<HostCommitResult> decode_host_commit_result(
    std::span<const std::byte> input,
    HostPayloadError& error);

[[nodiscard]] std::vector<std::byte> encode_host_reply(
    const HostReply& reply,
    std::uint32_t protocol_version = host_protocol_current);
[[nodiscard]] std::optional<HostReply> decode_host_reply(
    std::span<const std::byte> input,
    HostPayloadError& error,
    std::uint32_t protocol_version = host_protocol_current);

}  // namespace piinput
