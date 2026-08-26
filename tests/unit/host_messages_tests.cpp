#include "piinput/host_messages.h"

#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_key_event_and_resume_round_trip() {
    const piinput::HostKeyEvent key{
        .kind = piinput::HostKeyKind::select_candidate,
        .character = 'x',
        .candidate_id = 0x123456789ULL,
        .shifted = true,
        .resume = piinput::HostResumeState{
            77U, "hlhe", 4U, piinput::HostInputMode::chinese},
    };
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const auto decoded_key = piinput::decode_host_key_event(
        piinput::encode_host_key_event(key), error);
    check(decoded_key.has_value() && decoded_key->kind == key.kind &&
            decoded_key->character == key.character &&
            decoded_key->candidate_id == key.candidate_id && decoded_key->shifted &&
            decoded_key->resume == key.resume,
        "host key event round trips");

    const piinput::HostResumeState resume{91U, "hlheruhdlq", 6U, piinput::HostInputMode::chinese};
    const auto decoded_resume = piinput::decode_host_resume_state(
        piinput::encode_host_resume_state(resume), error);
    check(decoded_resume.has_value() && decoded_resume->generation == 91U &&
            decoded_resume->raw == "hlheruhdlq" && decoded_resume->caret == 6U &&
            decoded_resume->mode == piinput::HostInputMode::chinese,
        "host resume state round trips");
}

void test_reply_round_trip_preserves_candidate_snapshot() {
    piinput::HostReply reply;
    reply.accepted = true;
    reply.action = piinput::HostAction::update;
    reply.text = "黄河";
    reply.snapshot.generation = 19U;
    reply.snapshot.raw = "hlhe";
    reply.snapshot.caret = 4U;
    reply.snapshot.mode = piinput::HostInputMode::chinese;
    reply.snapshot.view = {true, 6U, 3U, 1U, 0U};
    reply.snapshot.candidates = {
        {0x1300000001ULL, "黄河", "huang'he", 9000},
        {0x1300000002ULL, "黄鹤", "huang'he", 8000},
    };

    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const auto decoded = piinput::decode_host_reply(piinput::encode_host_reply(reply), error);
    check(decoded.has_value() && decoded->accepted && decoded->action == reply.action,
        "host reply action round trips");
    check(decoded->text == reply.text && decoded->snapshot.raw == reply.snapshot.raw,
        "host reply text round trips as UTF-8");
    check(decoded->snapshot.view.visible_rows == 3U &&
            decoded->snapshot.candidates == reply.snapshot.candidates,
        "host reply candidate view and candidates round trip");
}

void test_candidate_actions_round_trip() {
    for (const auto action : {piinput::HostAction::launch_symbol_tool,
             piinput::HostAction::launch_settings,
             piinput::HostAction::launch_program}) {
        piinput::HostReply reply;
        reply.accepted = true;
        reply.action = action;
        if (action == piinput::HostAction::launch_program) {
            reply.text = "custom:https://example.com";
        }
        reply.snapshot.generation = 23U;
        piinput::HostPayloadError error = piinput::HostPayloadError::none;
        const auto decoded = piinput::decode_host_reply(
            piinput::encode_host_reply(reply), error);
        check(decoded.has_value() && decoded->action == action && decoded->text == reply.text,
            "candidate launch action round trips through the Host protocol");
    }
}

void test_v2_reply_preserves_composition_text_and_active_column() {
    piinput::HostReply reply;
    reply.accepted = true;
    reply.action = piinput::HostAction::update;
    reply.snapshot.generation = 31U;
    reply.snapshot.raw = "hlheruhdlq";
    reply.snapshot.composition_text = "黄河ruhdlq";
    reply.snapshot.caret = reply.snapshot.composition_text.size();
    reply.snapshot.view = {true, 6U, 3U, 1U, 0U, 4U};
    reply.snapshot.candidates = {
        {1U, "入海", "ru'hai", 9000},
        {2U, "如何", "ru'he", 8000},
    };

    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const auto encoded = piinput::encode_host_reply(reply, piinput::host_protocol_v2);
    const auto decoded = piinput::decode_host_reply(
        encoded, error, piinput::host_protocol_v2);
    check(decoded.has_value() &&
            decoded->snapshot.composition_text == "黄河ruhdlq" &&
            decoded->snapshot.view.active_column == 4U,
        "protocol v2 preserves staged composition text and horizontal selection");

    const auto legacy = piinput::decode_host_reply(
        piinput::encode_host_reply(reply, piinput::host_protocol_v1),
        error,
        piinput::host_protocol_v1);
    check(legacy.has_value() && legacy->snapshot.composition_text == legacy->snapshot.raw &&
            legacy->snapshot.view.active_column == 0U,
        "protocol v1 remains backward compatible with old loaded shims");
}

void test_caret_update_round_trip_preserves_text_geometry_and_fallback() {
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const piinput::HostCaretUpdate primary{
        .generation = 73U,
        .has_text_caret = true,
        .left = 100,
        .top = 200,
        .right = 102,
        .bottom = 224,
        .owner_window = 0x12345678U,
        .show_candidate_window = false,
    };
    const auto decoded_primary = piinput::decode_host_caret_update(
        piinput::encode_host_caret_update(primary), error);
    check(decoded_primary.has_value() && *decoded_primary == primary,
        "text caret geometry and popup owner round trip");

    const auto legacy_primary = piinput::decode_host_caret_update(
        piinput::encode_host_caret_update(primary, piinput::host_protocol_v3),
        error,
        piinput::host_protocol_v3);
    check(legacy_primary.has_value() && legacy_primary->owner_window == 0U &&
            legacy_primary->show_candidate_window &&
            legacy_primary->left == primary.left && legacy_primary->bottom == primary.bottom,
        "protocol v3 caret geometry remains compatible without a popup owner");

    const auto owner_only = piinput::decode_host_caret_update(
        piinput::encode_host_caret_update(primary, piinput::host_protocol_v4),
        error,
        piinput::host_protocol_v4);
    check(owner_only.has_value() && owner_only->owner_window == primary.owner_window &&
            owner_only->show_candidate_window,
        "protocol v4 owned caret defaults to the legacy custom candidate window");

    const piinput::HostCaretUpdate negative_monitor{
        .generation = 74U,
        .has_text_caret = true,
        .left = -1920,
        .top = 20,
        .right = -1918,
        .bottom = 44,
    };
    const auto decoded_negative = piinput::decode_host_caret_update(
        piinput::encode_host_caret_update(negative_monitor), error);
    check(decoded_negative.has_value() && *decoded_negative == negative_monitor,
        "negative-monitor text caret geometry round trips");

    const piinput::HostCaretUpdate fallback{.generation = 75U};
    const auto decoded_fallback = piinput::decode_host_caret_update(
        piinput::encode_host_caret_update(fallback), error);
    check(decoded_fallback.has_value() && *decoded_fallback == fallback,
        "unavailable text caret round trips as an explicit mouse fallback");
}

void test_commit_result_is_strict_and_round_trips() {
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const piinput::HostCommitResult result{91U, true};
    const auto encoded = piinput::encode_host_commit_result(result);
    const auto decoded = piinput::decode_host_commit_result(encoded, error);
    check(decoded.has_value() && *decoded == result,
        "TSF commit result round trips with its Host generation");

    auto trailing = encoded;
    trailing.push_back(std::byte{0U});
    check(!piinput::decode_host_commit_result(trailing, error).has_value() &&
            error == piinput::HostPayloadError::trailing_bytes,
        "commit result rejects trailing bytes");

    auto unknown = encoded;
    unknown[8] = std::byte{2U};
    check(!piinput::decode_host_commit_result(unknown, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "commit result rejects unknown success flags");
}

void test_caret_update_decoder_rejects_invalid_flags_rectangles_and_lengths() {
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const piinput::HostCaretUpdate valid{
        .generation = 81U,
        .has_text_caret = true,
        .left = 30,
        .top = 40,
        .right = 32,
        .bottom = 64,
    };

    auto unknown_flags = piinput::encode_host_caret_update(valid);
    unknown_flags[8] = std::byte{0x02};
    check(!piinput::decode_host_caret_update(unknown_flags, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "caret update rejects unknown availability flags");

    auto unknown_visibility = piinput::encode_host_caret_update(valid);
    unknown_visibility[36] = std::byte{0x02};
    check(!piinput::decode_host_caret_update(unknown_visibility, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "caret update rejects unknown integrated-candidate visibility values");

    auto reversed_horizontal = piinput::encode_host_caret_update(valid);
    reversed_horizontal[20] = std::byte{0x1d};
    check(!piinput::decode_host_caret_update(reversed_horizontal, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "caret update rejects right coordinates before left coordinates");

    auto reversed_vertical = piinput::encode_host_caret_update(valid);
    reversed_vertical[24] = std::byte{0x27};
    check(!piinput::decode_host_caret_update(reversed_vertical, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "caret update rejects bottom coordinates above top coordinates");

    auto truncated = piinput::encode_host_caret_update(valid);
    truncated.pop_back();
    check(!piinput::decode_host_caret_update(truncated, error).has_value() &&
            error == piinput::HostPayloadError::truncated,
        "caret update rejects truncation");

    auto trailing = piinput::encode_host_caret_update(valid);
    trailing.push_back(std::byte{0});
    check(!piinput::decode_host_caret_update(trailing, error).has_value() &&
            error == piinput::HostPayloadError::trailing_bytes,
        "caret update rejects trailing bytes");
}

void test_payload_decoder_rejects_truncation_unknown_enums_and_excessive_counts() {
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    auto key = piinput::encode_host_key_event({.kind = piinput::HostKeyKind::text, .character = 'a'});
    key.pop_back();
    check(!piinput::decode_host_key_event(key, error).has_value() &&
            error == piinput::HostPayloadError::truncated,
        "truncated key payload is rejected");

    auto unknown = piinput::encode_host_key_event({.kind = piinput::HostKeyKind::text, .character = 'a'});
    unknown[0] = std::byte{0xff};
    check(!piinput::decode_host_key_event(unknown, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "unknown key kind is rejected");

    auto unknown_flags = piinput::encode_host_key_event({
        .kind = piinput::HostKeyKind::punctuation,
        .character = '.',
    });
    unknown_flags[2] = std::byte{0x02};
    check(!piinput::decode_host_key_event(unknown_flags, error).has_value() &&
            error == piinput::HostPayloadError::unknown_value,
        "unknown key flags are rejected");

    piinput::HostReply reply;
    reply.snapshot.candidates.resize(piinput::host_max_candidates + 1U);
    bool threw = false;
    try {
        (void)piinput::encode_host_reply(reply);
    } catch (const std::length_error&) {
        threw = true;
    }
    check(threw, "reply encoder rejects excessive candidate counts");
}

}  // namespace

int main() {
    test_key_event_and_resume_round_trip();
    test_reply_round_trip_preserves_candidate_snapshot();
    test_candidate_actions_round_trip();
    test_v2_reply_preserves_composition_text_and_active_column();
    test_caret_update_round_trip_preserves_text_geometry_and_fallback();
    test_commit_result_is_strict_and_round_trips();
    test_caret_update_decoder_rejects_invalid_flags_rectangles_and_lengths();
    test_payload_decoder_rejects_truncation_unknown_enums_and_excessive_counts();
    std::cout << "PiInput host message tests passed.\n";
    return 0;
}
