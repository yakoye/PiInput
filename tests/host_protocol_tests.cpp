#include "piinput/host_protocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::byte> bytes(const std::string_view value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<std::byte>(ch));
    }
    return result;
}

piinput::HostEnvelope sample_envelope() {
    return {
        .version = piinput::host_protocol_v1,
        .client_id = 7U,
        .session_id = 11U,
        .sequence = 13U,
        .generation = 17U,
        .type = piinput::HostMessageType::key_event,
        .payload = bytes("drlojuzi"),
    };
}

void test_round_trip_preserves_fixed_width_fields_and_payload() {
    const auto expected = sample_envelope();
    const auto encoded = piinput::encode_host_envelope(expected);
    piinput::ProtocolError error = piinput::ProtocolError::truncated_header;
    const auto decoded = piinput::decode_host_envelope(encoded, error);
    check(decoded.has_value(), "valid envelope decodes");
    check(error == piinput::ProtocolError::none, "valid envelope has no protocol error");
    check(decoded->version == expected.version, "version round trips");
    check(decoded->client_id == expected.client_id, "client id round trips");
    check(decoded->session_id == expected.session_id, "session id round trips");
    check(decoded->sequence == expected.sequence, "sequence round trips");
    check(decoded->generation == expected.generation, "generation round trips");
    check(decoded->type == expected.type, "message type round trips");
    check(decoded->payload == expected.payload, "payload round trips");
}

void test_protocol_v2_envelope_is_accepted_for_extended_candidate_state() {
    auto expected = sample_envelope();
    expected.version = piinput::host_protocol_v2;
    const auto encoded = piinput::encode_host_envelope(expected);
    piinput::ProtocolError error = piinput::ProtocolError::unsupported_version;
    const auto decoded = piinput::decode_host_envelope(encoded, error);
    check(decoded.has_value() && decoded->version == piinput::host_protocol_v2,
        "protocol v2 envelope is accepted for extended candidate state");
}

void test_protocol_v3_accepts_commit_result_messages() {
    auto expected = sample_envelope();
    expected.version = piinput::host_protocol_v3;
    expected.type = piinput::HostMessageType::commit_result;
    const auto encoded = piinput::encode_host_envelope(expected);
    piinput::ProtocolError error = piinput::ProtocolError::none;
    const auto decoded = piinput::decode_host_envelope(encoded, error);
    check(decoded.has_value() && decoded->version == piinput::host_protocol_v3 &&
            decoded->type == piinput::HostMessageType::commit_result,
        "protocol v3 accepts confirmed TSF commit results");
}

void test_decoder_rejects_untrusted_lengths_before_allocation() {
    auto envelope = sample_envelope();
    envelope.payload.assign(piinput::host_max_payload_bytes + 1U, std::byte{0x41});
    bool threw = false;
    try {
        (void)piinput::encode_host_envelope(envelope);
    } catch (const std::length_error&) {
        threw = true;
    }
    check(threw, "encoder rejects payload over the protocol ceiling");

    auto encoded = piinput::encode_host_envelope(sample_envelope());
    constexpr std::size_t payload_length_offset = 48U;
    encoded[payload_length_offset + 0U] = std::byte{0x01};
    encoded[payload_length_offset + 1U] = std::byte{0x00};
    encoded[payload_length_offset + 2U] = std::byte{0x10};
    encoded[payload_length_offset + 3U] = std::byte{0x00};
    piinput::ProtocolError error = piinput::ProtocolError::none;
    check(!piinput::decode_host_envelope(encoded, error).has_value(),
        "oversized declared payload is rejected");
    check(error == piinput::ProtocolError::payload_too_large,
        "oversized declared payload returns the typed error");
}

void test_decoder_rejects_malformed_or_unsupported_envelopes() {
    const auto valid = piinput::encode_host_envelope(sample_envelope());
    piinput::ProtocolError error = piinput::ProtocolError::none;

    check(!piinput::decode_host_envelope(
        std::span<const std::byte>(valid).first(piinput::host_header_bytes - 1U), error).has_value(),
        "truncated header is rejected");
    check(error == piinput::ProtocolError::truncated_header, "truncation has a typed error");

    auto unsupported = valid;
    unsupported[8] = std::byte{0x04};
    check(!piinput::decode_host_envelope(unsupported, error).has_value(),
        "unsupported protocol major version is rejected");
    check(error == piinput::ProtocolError::unsupported_version,
        "unsupported version has a typed error");

    auto unknown_type = valid;
    unknown_type[12] = std::byte{0xff};
    check(!piinput::decode_host_envelope(unknown_type, error).has_value(),
        "unknown message type is rejected");
    check(error == piinput::ProtocolError::unknown_message_type,
        "unknown type has a typed error");

    auto zero_sequence = sample_envelope();
    zero_sequence.sequence = 0U;
    auto zero_sequence_bytes = piinput::encode_host_envelope(zero_sequence);
    check(!piinput::decode_host_envelope(zero_sequence_bytes, error).has_value(),
        "sequence zero is rejected");
    check(error == piinput::ProtocolError::invalid_sequence,
        "zero sequence has a typed error");

    auto mismatched = valid;
    mismatched.pop_back();
    check(!piinput::decode_host_envelope(mismatched, error).has_value(),
        "declared length larger than available bytes is rejected");
    check(error == piinput::ProtocolError::length_mismatch,
        "short body has a typed error");

    auto trailing = valid;
    trailing.push_back(std::byte{0});
    check(!piinput::decode_host_envelope(trailing, error).has_value(),
        "trailing bytes are rejected");
    check(error == piinput::ProtocolError::trailing_bytes,
        "trailing bytes have a typed error");
}

}  // namespace

int main() {
    test_round_trip_preserves_fixed_width_fields_and_payload();
    test_protocol_v2_envelope_is_accepted_for_extended_candidate_state();
    test_protocol_v3_accepts_commit_result_messages();
    test_decoder_rejects_untrusted_lengths_before_allocation();
    test_decoder_rejects_malformed_or_unsupported_envelopes();
    std::cout << "PiInput host protocol tests passed.\n";
    return 0;
}
