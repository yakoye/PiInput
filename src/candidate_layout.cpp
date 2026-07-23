#include "piinput/candidate_layout.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

namespace piinput {

std::vector<int> fit_candidate_column_widths(
    const std::vector<int>& desired_widths,
    const int available_width) {
    if (desired_widths.empty()) {
        return {};
    }

    const int budget = (std::max)(1, available_width);
    std::vector<int> widths = desired_widths;
    for (int& width : widths) {
        width = (std::max)(0, width);
    }
    const std::int64_t desired_total =
        std::accumulate(widths.begin(), widths.end(), std::int64_t{0});
    if (desired_total <= budget) {
        return widths;
    }

    const int base = static_cast<std::size_t>(budget) >= widths.size() ? 1 : 0;
    const int remaining = budget - base * static_cast<int>(widths.size());
    std::vector<int> fitted(widths.size(), base);
    int allocated = base * static_cast<int>(widths.size());
    for (std::size_t index = 0U; index < widths.size(); ++index) {
        const auto proportional = static_cast<int>(
            static_cast<std::int64_t>(widths[index]) * remaining / desired_total);
        fitted[index] += proportional;
        allocated += proportional;
    }
    for (std::size_t index = 0U; allocated < budget; ++index, ++allocated) {
        ++fitted[index % fitted.size()];
    }
    return fitted;
}

}  // namespace piinput
