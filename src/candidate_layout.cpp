#include "piinput/candidate_layout.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

namespace piinput {

namespace {

[[nodiscard]] std::size_t utf8_character_count(const std::string_view text) noexcept {
    std::size_t count = 0U;
    for (const unsigned char value : text) {
        if ((value & 0xc0U) != 0x80U) ++count;
    }
    return count;
}

}  // namespace

std::wstring candidate_header_text(const std::wstring_view composition) {
    return std::wstring(composition);
}

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

std::size_t candidate_items_per_row(
    const std::size_t configured_items_per_row,
    const std::vector<std::string>& candidates) noexcept {
    const std::size_t configured = (std::max)(configured_items_per_row, std::size_t{1U});
    const std::size_t limit = (std::min)(configured, candidates.size());
    std::vector<std::string_view> leading;
    try {
        leading.reserve(limit);
    } catch (...) {
        return 1U;
    }
    for (std::size_t index = 0U; index < limit; ++index) {
        leading.emplace_back(candidates[index]);
    }
    return candidate_items_per_row(configured, std::span<const std::string_view>(leading));
}

std::size_t candidate_items_per_row(
    const std::size_t configured_items_per_row,
    const std::span<const std::string_view> candidates) noexcept {
    const std::size_t configured = (std::max)(configured_items_per_row, std::size_t{1U});
    if (candidates.empty()) return 1U;

    constexpr std::size_t kAvailableWidthDip = 584U;
    constexpr std::size_t kMinimumItemWidthDip = 64U;
    constexpr std::size_t kMaximumItemWidthDip = 720U;
    constexpr std::size_t kGlyphWidthDip = 18U;
    constexpr std::size_t kItemChromeWidthDip = 36U;
    constexpr std::size_t kExclusiveSentenceLength = 14U;

    const std::size_t limit = (std::min)(configured, candidates.size());
    std::size_t used_width = 0U;
    std::size_t fitted = 0U;
    for (std::size_t index = 0U; index < limit; ++index) {
        const std::size_t characters = utf8_character_count(candidates[index]);
        if (index == 0U && characters >= kExclusiveSentenceLength) return 1U;
        const std::size_t desired = (std::clamp)(
            characters * kGlyphWidthDip + kItemChromeWidthDip,
            kMinimumItemWidthDip,
            kMaximumItemWidthDip);
        if (fitted != 0U && desired > kAvailableWidthDip - used_width) break;
        used_width += (std::min)(desired, kAvailableWidthDip);
        ++fitted;
        if (used_width == kAvailableWidthDip) break;
    }
    return (std::max)(fitted, std::size_t{1U});
}

}  // namespace piinput
