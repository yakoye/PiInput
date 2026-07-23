#pragma once

#include <cstddef>
#include <cstdint>

namespace piinput {

struct CompositionCaretMapping {
    std::size_t caret{};
    std::int32_t shift_end{};

    bool operator==(const CompositionCaretMapping&) const = default;
};

[[nodiscard]] CompositionCaretMapping map_composition_caret(
    std::size_t text_length,
    std::size_t requested_caret) noexcept;

}  // namespace piinput
