#include "piinput/host_protocol.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace piinput {

namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'P'}, std::byte{'I'}, std::byte{'I'}, std::byte{'N'},
    std::byte{'P'}, std::byte{'I'}, std::byte{'P'}, std::byte{'C'},
};

template <typename Integer>
void append_little_endian(std::vector<std::byte>& output, Integer value) {
    static_assert(std::is_unsigned_v<Integer>);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        output.push_back(static_cast<std::byte>(value & static_cast<Integer>(0xffU)));
        value >>= 8U;
    }
}

template <typename Integer>
Integer read_little_endian(const std::span<const std::byte> input, const std::size_t offset) {
    static_assert(std::is_unsigned_v<Integer>);
    Integer value{};
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        const auto part = static_cast<Integer>(std::to_integer<unsigned int>(input[offset + index]));
        value |= part << (index * 8U);
    }
    return value;
}

bool is_known_message_type(const HostMessageType type) noexcept {
    switch (type) {
    case HostMessageType::key_event:
    case HostMessageType::key_reply:
    case HostMessageType::focus:
    case HostMessageType::caret:
    case HostMessageType::resume:
    case HostMessageType::health:
    case HostMessageType::drain:
    case HostMessageType::commit_result:
        return true;
    }
    return false;
}

}  // namespace

std::vector<std::byte> encode_host_envelope(const HostEnvelope& envelope) {
    if (envelope.payload.size() > host_max_payload_bytes ||
        envelope.payload.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        throw std::length_error("PiInput host protocol payload exceeds the 1 MiB limit");
    }

    std::vector<std::byte> output;
    output.reserve(host_header_bytes + envelope.payload.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    append_little_endian(output, envelope.version);
    append_little_endian(output, static_cast<std::uint32_t>(envelope.type));
    append_little_endian(output, envelope.client_id);
    append_little_endian(output, envelope.session_id);
    append_little_endian(output, envelope.sequence);
    append_little_endian(output, envelope.generation);
    append_little_endian(output, static_cast<std::uint32_t>(envelope.payload.size()));
    append_little_endian(output, std::uint32_t{0U});
    output.insert(output.end(), envelope.payload.begin(), envelope.payload.end());
    return output;
}

std::optional<HostEnvelope> decode_host_envelope(
    const std::span<const std::byte> input,
    ProtocolError& error) {
    error = ProtocolError::none;
    if (input.size() < host_header_bytes) {
        error = ProtocolError::truncated_header;
        return std::nullopt;
    }
    for (std::size_t index = 0; index < kMagic.size(); ++index) {
        if (input[index] != kMagic[index]) {
            error = ProtocolError::bad_magic;
            return std::nullopt;
        }
    }

    HostEnvelope envelope;
    envelope.version = read_little_endian<std::uint32_t>(input, 8U);
    if (envelope.version != host_protocol_v1 && envelope.version != host_protocol_v2 &&
        envelope.version != host_protocol_v3 && envelope.version != host_protocol_v4 &&
        envelope.version != host_protocol_v5) {
        error = ProtocolError::unsupported_version;
        return std::nullopt;
    }
    envelope.type = static_cast<HostMessageType>(
        read_little_endian<std::uint32_t>(input, 12U));
    if (!is_known_message_type(envelope.type)) {
        error = ProtocolError::unknown_message_type;
        return std::nullopt;
    }
    envelope.client_id = read_little_endian<std::uint64_t>(input, 16U);
    envelope.session_id = read_little_endian<std::uint64_t>(input, 24U);
    envelope.sequence = read_little_endian<std::uint64_t>(input, 32U);
    envelope.generation = read_little_endian<std::uint64_t>(input, 40U);
    if (envelope.sequence == 0U) {
        error = ProtocolError::invalid_sequence;
        return std::nullopt;
    }

    const auto payload_size = read_little_endian<std::uint32_t>(input, 48U);
    if (payload_size > host_max_payload_bytes) {
        error = ProtocolError::payload_too_large;
        return std::nullopt;
    }
    const std::size_t expected_size = host_header_bytes + payload_size;
    if (input.size() < expected_size) {
        error = ProtocolError::length_mismatch;
        return std::nullopt;
    }
    if (input.size() > expected_size) {
        error = ProtocolError::trailing_bytes;
        return std::nullopt;
    }
    envelope.payload.assign(input.begin() + static_cast<std::ptrdiff_t>(host_header_bytes), input.end());
    return envelope;
}

}  // namespace piinput
