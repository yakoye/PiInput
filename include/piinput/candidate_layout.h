#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

[[nodiscard]] std::vector<int> fit_candidate_column_widths(
    const std::vector<int>& desired_widths,
    int available_width);

[[nodiscard]] std::wstring candidate_header_text(std::wstring_view composition);

// Only the leading `configured_items_per_row` candidates can influence the
// column count. Callers on the keystroke hot path should pass that bounded
// view instead of materializing the whole candidate list.
[[nodiscard]] std::size_t candidate_items_per_row(
    std::size_t configured_items_per_row,
    std::span<const std::string_view> candidates) noexcept;

[[nodiscard]] std::size_t candidate_items_per_row(
    std::size_t configured_items_per_row,
    const std::vector<std::string>& candidates) noexcept;

}  // namespace piinput
