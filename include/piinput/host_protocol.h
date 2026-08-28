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
inline constexpr std::uint32_t host_protocol_v4 = 4U;
// v6 加过一个 app_shows_composition 字段，后来判据被证明不可靠，字段撤掉了。
// 版本号没有跟着撤掉，也不能撤：Shim 是 DLL，加载进应用进程后就一直待在那里，
// 用户不会因为装了输入法更新就把浏览器、编辑器、聊天窗口全部重启一遍。一旦
// Host 不再认 v6，这些进程发来的报文会被整包丢弃，连回复都没有——表现是键被
// 吃掉、字打不出来，直到那个应用重启为止。
//
// 所以规则是：发布过的版本号永远收在白名单里。去掉一个字段意味着「不再发送，
// 收到就跳过」，不意味着可以把版本号一起删掉。
inline constexpr std::uint32_t host_protocol_v5 = 5U;
inline constexpr std::uint32_t host_protocol_v6 = 6U;
// 只发 v5：v6 唯一多出来的字段已经没有含义了，没必要继续往线上写。
inline constexpr std::uint32_t host_protocol_current = host_protocol_v5;
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
