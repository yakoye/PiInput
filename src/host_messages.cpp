#include "piinput/host_messages.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace piinput {

namespace {

class Writer final {
public:
    void reserve(const std::size_t bytes) { output_.reserve(bytes); }

    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_unsigned_v<Integer>);
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            output_.push_back(static_cast<std::byte>(value & static_cast<Integer>(0xffU)));
            value >>= 8U;
        }
    }

    void text(const std::string_view value) {
        if (value.size() > host_max_text_bytes ||
            value.size() > (std::numeric_limits<std::uint32_t>::max)()) {
            throw std::length_error("PiInput Host text exceeds the protocol limit");
        }
        integer(static_cast<std::uint32_t>(value.size()));
        for (const unsigned char character : value) {
            output_.push_back(static_cast<std::byte>(character));
        }
    }

    [[nodiscard]] std::vector<std::byte> finish() && { return std::move(output_); }

private:
    std::vector<std::byte> output_;
};

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> input) : input_(input) {}

    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer() noexcept {
        static_assert(std::is_unsigned_v<Integer>);
        if (remaining() < sizeof(Integer)) return std::nullopt;
        Integer value{};
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            const auto part = static_cast<Integer>(
                std::to_integer<unsigned int>(input_[offset_ + index]));
            value |= part << (index * 8U);
        }
        offset_ += sizeof(Integer);
        return value;
    }

    [[nodiscard]] std::optional<std::string> text(HostPayloadError& error) {
        const auto length = integer<std::uint32_t>();
        if (!length.has_value()) {
            error = HostPayloadError::truncated;
            return std::nullopt;
        }
        if (*length > host_max_text_bytes) {
            error = HostPayloadError::too_large;
            return std::nullopt;
        }
        if (remaining() < *length) {
            error = HostPayloadError::truncated;
            return std::nullopt;
        }
        std::string result;
        result.reserve(*length);
        for (std::size_t index = 0; index < *length; ++index) {
            result.push_back(static_cast<char>(
                std::to_integer<unsigned char>(input_[offset_ + index])));
        }
        offset_ += *length;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return input_.size() - offset_; }

private:
    std::span<const std::byte> input_;
    std::size_t offset_{};
};

bool known_key_kind(const HostKeyKind kind) noexcept {
    return kind >= HostKeyKind::text && kind <= HostKeyKind::open_symbol_center;
}

bool known_action(const HostAction action) noexcept {
    return action >= HostAction::none && action <= HostAction::launch_program;
}

bool known_mode(const HostInputMode mode) noexcept {
    return mode == HostInputMode::chinese || mode == HostInputMode::english;
}

template <typename Integer>
Integer checked_size(const std::size_t value) {
    if (value > (std::numeric_limits<Integer>::max)()) {
        throw std::length_error("PiInput Host view value exceeds the protocol limit");
    }
    return static_cast<Integer>(value);
}

}  // namespace

std::vector<std::byte> encode_host_key_event(const HostKeyEvent& event) {
    Writer writer;
    writer.integer(static_cast<std::uint8_t>(event.kind));
    writer.integer(static_cast<std::uint8_t>(event.character));
    writer.integer(static_cast<std::uint16_t>(event.shifted ? 1U : 0U));
    writer.integer(std::uint32_t{0U});
    writer.integer(event.candidate_id);
    writer.integer(static_cast<std::uint8_t>(event.resume.has_value() ? 1U : 0U));
    writer.integer(static_cast<std::uint8_t>(
        event.resume.has_value() ? event.resume->mode : HostInputMode::chinese));
    writer.integer(std::uint16_t{0U});
    writer.integer(event.resume.has_value() ? event.resume->generation : 0U);
    writer.integer(static_cast<std::uint64_t>(
        event.resume.has_value() ? event.resume->caret : 0U));
    writer.text(event.resume.has_value() ? event.resume->raw : std::string_view{});
    return std::move(writer).finish();
}

std::optional<HostKeyEvent> decode_host_key_event(
    const std::span<const std::byte> input,
    HostPayloadError& error) {
    error = HostPayloadError::none;
    Reader reader(input);
    const auto kind = reader.integer<std::uint8_t>();
    const auto character = reader.integer<std::uint8_t>();
    const auto flags = reader.integer<std::uint16_t>();
    const auto reserved32 = reader.integer<std::uint32_t>();
    const auto candidate_id = reader.integer<std::uint64_t>();
    const auto has_resume = reader.integer<std::uint8_t>();
    const auto resume_mode = reader.integer<std::uint8_t>();
    const auto resume_reserved = reader.integer<std::uint16_t>();
    const auto resume_generation = reader.integer<std::uint64_t>();
    const auto resume_caret = reader.integer<std::uint64_t>();
    const auto resume_raw = reader.text(error);
    if (!kind || !character || !flags || !reserved32 || !candidate_id ||
        !has_resume || !resume_mode || !resume_reserved || !resume_generation ||
        !resume_caret || !resume_raw) {
        error = HostPayloadError::truncated;
        return std::nullopt;
    }
    const auto key_kind = static_cast<HostKeyKind>(*kind);
    if (!known_key_kind(key_kind)) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    if (reader.remaining() != 0U) {
        error = HostPayloadError::trailing_bytes;
        return std::nullopt;
    }
    if ((*flags & ~std::uint16_t{1U}) != 0U) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    if (*has_resume > 1U || *resume_reserved != 0U ||
        (*has_resume != 0U && !known_mode(static_cast<HostInputMode>(*resume_mode))) ||
        *resume_caret > resume_raw->size()) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    HostKeyEvent result{
        key_kind,
        static_cast<char>(*character),
        *candidate_id,
        (*flags & 1U) != 0U,
    };
    if (*has_resume != 0U) {
        result.resume = HostResumeState{
            *resume_generation,
            std::move(*resume_raw),
            static_cast<std::size_t>(*resume_caret),
            static_cast<HostInputMode>(*resume_mode),
        };
    }
    return result;
}

std::vector<std::byte> encode_host_resume_state(const HostResumeState& state) {
    Writer writer;
    writer.integer(state.generation);
    writer.integer(checked_size<std::uint64_t>(state.caret));
    writer.integer(static_cast<std::uint8_t>(state.mode));
    writer.integer(std::uint8_t{0U});
    writer.integer(std::uint16_t{0U});
    writer.integer(std::uint32_t{0U});
    writer.text(state.raw);
    return std::move(writer).finish();
}

std::optional<HostResumeState> decode_host_resume_state(
    const std::span<const std::byte> input,
    HostPayloadError& error) {
    error = HostPayloadError::none;
    Reader reader(input);
    const auto generation = reader.integer<std::uint64_t>();
    const auto caret = reader.integer<std::uint64_t>();
    const auto mode_raw = reader.integer<std::uint8_t>();
    const auto reserved8 = reader.integer<std::uint8_t>();
    const auto reserved16 = reader.integer<std::uint16_t>();
    const auto reserved32 = reader.integer<std::uint32_t>();
    if (!generation || !caret || !mode_raw || !reserved8 || !reserved16 || !reserved32) {
        error = HostPayloadError::truncated;
        return std::nullopt;
    }
    const auto mode = static_cast<HostInputMode>(*mode_raw);
    if (!known_mode(mode) || *caret > (std::numeric_limits<std::size_t>::max)()) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    auto raw = reader.text(error);
    if (!raw.has_value()) return std::nullopt;
    if (reader.remaining() != 0U) {
        error = HostPayloadError::trailing_bytes;
        return std::nullopt;
    }
    return HostResumeState{*generation, std::move(*raw), static_cast<std::size_t>(*caret), mode};
}

std::vector<std::byte> encode_host_caret_update(
    const HostCaretUpdate& update,
    const std::uint32_t protocol_version) {
    if (protocol_version != host_protocol_v1 && protocol_version != host_protocol_v2 &&
        protocol_version != host_protocol_v3 && protocol_version != host_protocol_v4 &&
        protocol_version != host_protocol_v5 && protocol_version != host_protocol_v6) {
        throw std::invalid_argument("unsupported PiInput Host caret protocol version");
    }
    Writer writer;
    writer.integer(update.generation);
    writer.integer(static_cast<std::uint32_t>(update.has_text_caret ? 1U : 0U));
    writer.integer(std::bit_cast<std::uint32_t>(update.left));
    writer.integer(std::bit_cast<std::uint32_t>(update.top));
    writer.integer(std::bit_cast<std::uint32_t>(update.right));
    writer.integer(std::bit_cast<std::uint32_t>(update.bottom));
    if (protocol_version >= host_protocol_v4) writer.integer(update.owner_window);
    if (protocol_version >= host_protocol_v5) {
        writer.integer(static_cast<std::uint32_t>(update.show_candidate_window ? 1U : 0U));
    }
    if (protocol_version >= host_protocol_v6) {
        writer.integer(static_cast<std::uint32_t>(update.app_shows_composition ? 1U : 0U));
    }
    return std::move(writer).finish();
}

std::optional<HostCaretUpdate> decode_host_caret_update(
    const std::span<const std::byte> input,
    HostPayloadError& error,
    const std::uint32_t protocol_version) {
    error = HostPayloadError::none;
    if (protocol_version != host_protocol_v1 && protocol_version != host_protocol_v2 &&
        protocol_version != host_protocol_v3 && protocol_version != host_protocol_v4 &&
        protocol_version != host_protocol_v5 && protocol_version != host_protocol_v6) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    Reader reader(input);
    const auto generation = reader.integer<std::uint64_t>();
    const auto flags = reader.integer<std::uint32_t>();
    const auto left = reader.integer<std::uint32_t>();
    const auto top = reader.integer<std::uint32_t>();
    const auto right = reader.integer<std::uint32_t>();
    const auto bottom = reader.integer<std::uint32_t>();
    const auto owner_window = protocol_version >= host_protocol_v4
        ? reader.integer<std::uint64_t>()
        : std::optional<std::uint64_t>{0U};
    const auto show_candidate_window = protocol_version >= host_protocol_v5
        ? reader.integer<std::uint32_t>()
        : std::optional<std::uint32_t>{1U};
    // 默认 1：v6 之前的对端不发这个字段，而「应用自己显示了」正是那时候的行为。
    const auto app_shows_composition = protocol_version >= host_protocol_v6
        ? reader.integer<std::uint32_t>()
        : std::optional<std::uint32_t>{1U};
    if (!generation || !flags || !left || !top || !right || !bottom ||
        !owner_window || !show_candidate_window || !app_shows_composition) {
        error = HostPayloadError::truncated;
        return std::nullopt;
    }
    if (reader.remaining() != 0U) {
        error = HostPayloadError::trailing_bytes;
        return std::nullopt;
    }
    if ((*flags & ~std::uint32_t{1U}) != 0U || *show_candidate_window > 1U ||
        *app_shows_composition > 1U) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    HostCaretUpdate result{
        .generation = *generation,
        .has_text_caret = (*flags & 1U) != 0U,
        .left = std::bit_cast<std::int32_t>(*left),
        .top = std::bit_cast<std::int32_t>(*top),
        .right = std::bit_cast<std::int32_t>(*right),
        .bottom = std::bit_cast<std::int32_t>(*bottom),
        .owner_window = *owner_window,
        .show_candidate_window = *show_candidate_window != 0U,
        .app_shows_composition = *app_shows_composition != 0U,
    };
    if (result.has_text_caret &&
        (result.right < result.left || result.bottom < result.top)) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    return result;
}

std::vector<std::byte> encode_host_commit_result(const HostCommitResult& result) {
    Writer writer;
    writer.integer(result.generation);
    writer.integer(static_cast<std::uint32_t>(result.succeeded ? 1U : 0U));
    writer.integer(std::uint32_t{0U});
    return std::move(writer).finish();
}

std::optional<HostCommitResult> decode_host_commit_result(
    const std::span<const std::byte> input,
    HostPayloadError& error) {
    error = HostPayloadError::none;
    Reader reader(input);
    const auto generation = reader.integer<std::uint64_t>();
    const auto flags = reader.integer<std::uint32_t>();
    const auto reserved = reader.integer<std::uint32_t>();
    if (!generation || !flags || !reserved) {
        error = HostPayloadError::truncated;
        return std::nullopt;
    }
    if (reader.remaining() != 0U) {
        error = HostPayloadError::trailing_bytes;
        return std::nullopt;
    }
    if (*flags > 1U || *reserved != 0U) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    return HostCommitResult{*generation, *flags != 0U};
}

std::vector<std::byte> encode_host_reply(
    const HostReply& reply,
    const std::uint32_t protocol_version) {
    if (protocol_version != host_protocol_v1 && protocol_version != host_protocol_v2 &&
        protocol_version != host_protocol_v3 && protocol_version != host_protocol_v4 &&
        protocol_version != host_protocol_v5 && protocol_version != host_protocol_v6) {
        throw std::invalid_argument("unsupported PiInput Host reply protocol version");
    }
    if (reply.snapshot.candidates.size() > host_max_candidates) {
        throw std::length_error("PiInput Host candidate count exceeds the protocol limit");
    }
    Writer writer;
    // A full candidate page is a few kilobytes. Sizing the buffer up front
    // keeps the per-key encode from repeatedly reallocating and copying.
    std::size_t estimated = 64U + reply.text.size() + reply.snapshot.raw.size() +
        reply.snapshot.composition_text.size();
    for (const auto& candidate : reply.snapshot.candidates) {
        estimated += 24U + candidate.text.size() + candidate.pinyin.size();
    }
    writer.reserve(estimated);
    writer.integer(static_cast<std::uint8_t>(reply.accepted ? 1U : 0U));
    writer.integer(static_cast<std::uint8_t>(reply.action));
    writer.integer(static_cast<std::uint8_t>(reply.snapshot.mode));
    writer.integer(static_cast<std::uint8_t>(reply.snapshot.view.expanded ? 1U : 0U));
    writer.integer(reply.snapshot.generation);
    writer.integer(checked_size<std::uint64_t>(reply.snapshot.caret));
    writer.integer(checked_size<std::uint32_t>(reply.snapshot.view.items_per_row));
    writer.integer(checked_size<std::uint32_t>(reply.snapshot.view.visible_rows));
    writer.integer(checked_size<std::uint32_t>(reply.snapshot.view.active_row));
    writer.integer(checked_size<std::uint32_t>(reply.snapshot.view.first_visible_row));
    if (protocol_version >= host_protocol_v2) {
        writer.integer(checked_size<std::uint32_t>(reply.snapshot.view.active_column));
        writer.integer(static_cast<std::uint8_t>(reply.snapshot.view.mode));
    }
    writer.integer(checked_size<std::uint32_t>(reply.snapshot.candidates.size()));
    writer.text(reply.text);
    writer.text(reply.snapshot.raw);
    if (protocol_version >= host_protocol_v2) {
        writer.text(reply.snapshot.composition_text.empty()
            ? reply.snapshot.raw
            : reply.snapshot.composition_text);
    }
    for (const auto& candidate : reply.snapshot.candidates) {
        writer.integer(candidate.id);
        writer.integer(std::bit_cast<std::uint64_t>(candidate.score));
        writer.text(candidate.text);
        writer.text(candidate.pinyin);
    }
    return std::move(writer).finish();
}

std::optional<HostReply> decode_host_reply(
    const std::span<const std::byte> input,
    HostPayloadError& error,
    const std::uint32_t protocol_version) {
    error = HostPayloadError::none;
    Reader reader(input);
    const auto accepted = reader.integer<std::uint8_t>();
    const auto action_raw = reader.integer<std::uint8_t>();
    const auto mode_raw = reader.integer<std::uint8_t>();
    const auto expanded = reader.integer<std::uint8_t>();
    const auto generation = reader.integer<std::uint64_t>();
    const auto caret = reader.integer<std::uint64_t>();
    const auto items = reader.integer<std::uint32_t>();
    const auto rows = reader.integer<std::uint32_t>();
    const auto active = reader.integer<std::uint32_t>();
    const auto first = reader.integer<std::uint32_t>();
    std::optional<std::uint32_t> active_column{0U};
    std::optional<std::uint8_t> candidate_mode{static_cast<std::uint8_t>(0U)};
    if (protocol_version >= host_protocol_v2) {
        active_column = reader.integer<std::uint32_t>();
        candidate_mode = reader.integer<std::uint8_t>();
    }
    const auto count = reader.integer<std::uint32_t>();
    if (!accepted || !action_raw || !mode_raw || !expanded || !generation || !caret ||
        !items || !rows || !active || !first || !active_column || !candidate_mode || !count) {
        error = HostPayloadError::truncated;
        return std::nullopt;
    }
    const auto action = static_cast<HostAction>(*action_raw);
    const auto mode = static_cast<HostInputMode>(*mode_raw);
    if (*accepted > 1U || *expanded > 1U || !known_action(action) || !known_mode(mode) ||
        *candidate_mode > static_cast<std::uint8_t>(HostCandidateMode::segment_selection)) {
        error = HostPayloadError::unknown_value;
        return std::nullopt;
    }
    if (*count > host_max_candidates) {
        error = HostPayloadError::too_large;
        return std::nullopt;
    }
    auto text = reader.text(error);
    if (!text.has_value()) return std::nullopt;
    auto raw = reader.text(error);
    if (!raw.has_value()) return std::nullopt;
    std::optional<std::string> composition = *raw;
    if (protocol_version >= host_protocol_v2) {
        composition = reader.text(error);
        if (!composition.has_value()) return std::nullopt;
    }

    HostReply reply;
    reply.accepted = *accepted != 0U;
    reply.action = action;
    reply.text = std::move(*text);
    reply.snapshot.generation = *generation;
    reply.snapshot.raw = std::move(*raw);
    reply.snapshot.composition_text = std::move(*composition);
    reply.snapshot.caret = static_cast<std::size_t>(*caret);
    reply.snapshot.mode = mode;
    reply.snapshot.view = {
        *expanded != 0U, *items, *rows, *active, *first, *active_column,
        static_cast<HostCandidateMode>(*candidate_mode),
    };
    reply.snapshot.candidates.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        const auto id = reader.integer<std::uint64_t>();
        const auto score = reader.integer<std::uint64_t>();
        if (!id || !score) {
            error = HostPayloadError::truncated;
            return std::nullopt;
        }
        auto candidate_text = reader.text(error);
        if (!candidate_text.has_value()) return std::nullopt;
        auto pinyin = reader.text(error);
        if (!pinyin.has_value()) return std::nullopt;
        reply.snapshot.candidates.push_back({
            *id,
            std::move(*candidate_text),
            std::move(*pinyin),
            std::bit_cast<std::int64_t>(*score),
        });
    }
    if (reader.remaining() != 0U) {
        error = HostPayloadError::trailing_bytes;
        return std::nullopt;
    }
    return reply;
}

}  // namespace piinput
