#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace piinput {

inline constexpr std::uint32_t host_protocol_v1 = 1U;
inline constexpr std::uint32_t host_protocol_v2 = 2U;
inline constexpr std::uint32_t host_protocol_v3 = 3U;
inline constexpr std::uint32_t host_protocol_current = host_protocol_v3;
inline constexpr std::size_t host_header_bytes = 56U;
inline constexpr std::size_t host_max_payload_bytes = 1024U * 1024U;

enum class HostMessageType : std::uint32_t {
    key_event = 1U,
    key_reply = 2U,
    focus = 3U,
    caret = 4U,
    resume = 5U,
    health = 6U,
    drain = 7U,
    commit_result = 8U,
};

enum class ProtocolError {
    none,
    truncated_header,
    bad_magic,
    unsupported_version,
    unknown_message_type,
    payload_too_large,
    invalid_sequence,
    length_mismatch,
    trailing_bytes,
};

struct HostEnvelope final {
    std::uint32_t version{host_protocol_current};
    std::uint64_t client_id{};
    std::uint64_t session_id{};
    std::uint64_t sequence{};
    std::uint64_t generation{};
    HostMessageType type{HostMessageType::key_event};
    std::vector<std::byte> payload;
};

[[nodiscard]] std::vector<std::byte> encode_host_envelope(const HostEnvelope& envelope);
[[nodiscard]] std::optional<HostEnvelope> decode_host_envelope(
    std::span<const std::byte> input,
    ProtocolError& error);

}  // namespace piinput
