#pragma once

#include <vector>

namespace piinput {

[[nodiscard]] std::vector<int> fit_candidate_column_widths(
    const std::vector<int>& desired_widths,
    int available_width);

}  // namespace piinput
