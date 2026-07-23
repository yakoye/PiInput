#include "piinput/composition_caret.h"

#include <algorithm>
#include <limits>

namespace piinput {

CompositionCaretMapping map_composition_caret(
    const std::size_t text_length,
    const std::size_t requested_caret) noexcept {
    const auto maximum_shift = static_cast<std::size_t>(
        (std::numeric_limits<std::int32_t>::max)());
    const std::size_t caret = (std::min)({
        text_length,
        requested_caret,
        maximum_shift,
    });
    return {
        caret,
        static_cast<std::int32_t>(caret),
    };
}

}  // namespace piinput
