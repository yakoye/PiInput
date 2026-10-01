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
// v7 给按键事件加了一个文本载荷，目前只有 digit_run 用它：Shim 把刚打出的那串
// 数字交给 Host，好让 Host 在候选窗里给出数字常用语的补全建议。
//
// 新增字段按上面那条规则办：只往白名单里加版本号，不动任何旧版本。Shim 发送
// digit_run 之前必须先看握手报回来的 Host 版本——升级之后旧 Host 可能还在运行，
// 而它不认识这个 kind，收到会整包拒收。门控在 Shim 侧，拒收一次都不该发生。
inline constexpr std::uint32_t host_protocol_v7 = 7U;
// 仍然只把 v5 当默认版本。Shim 不做版本协商，它发什么版本就是什么版本，而升级
// 之后旧 Host 可能还在跑——它的白名单里没有 v7，收到会整包丢弃、连回复都没有，
// 也就是那个应用彻底打不出字。所以普通报文一律继续发 v5。
//
// v7 只用在 digit_run 这一种报文上：它是纯增量的提示，Shim 不等它的回复，旧 Host
// 拒收的后果仅仅是「数字建议不出现」。把版本差异收窄到这一种报文，是这个功能不
// 可能拖垮打字的原因。
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
