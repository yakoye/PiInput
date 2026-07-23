#include "piinput/candidate_grid.h"

#include <algorithm>
#include <cstdint>

namespace piinput {

CandidateGrid::CandidateGrid(
    CandidateSettings settings,
    const std::size_t candidate_count) noexcept
    : settings_(settings),
      candidate_count_(candidate_count) {
    settings_.items_per_row = (std::max)(settings_.items_per_row, 1U);
    settings_.visible_rows = (std::max)(settings_.visible_rows, 1U);
    clamp_view();
}

void CandidateGrid::reset(const std::size_t candidate_count) noexcept {
    candidate_count_ = candidate_count;
    active_row_ = 0U;
    first_visible_row_ = 0U;
    clamp_view();
}

void CandidateGrid::set_candidate_count(const std::size_t candidate_count) noexcept {
    candidate_count_ = candidate_count;
    clamp_view();
}

void CandidateGrid::move_row(const int delta) noexcept {
    if (candidate_count_ == 0U || delta == 0) {
        return;
    }
    const auto last_row = row_count() - 1U;
    const auto next = (std::clamp)(
        static_cast<std::int64_t>(active_row_) + static_cast<std::int64_t>(delta),
        std::int64_t{0},
        static_cast<std::int64_t>(last_row));
    active_row_ = static_cast<std::size_t>(next);
    clamp_view();
}

void CandidateGrid::move_page(const int delta) noexcept {
    const auto rows = static_cast<std::int64_t>(visible_rows());
    const auto movement = (std::clamp)(
        static_cast<std::int64_t>(delta) * rows,
        static_cast<std::int64_t>((std::numeric_limits<int>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)()));
    move_row(static_cast<int>(movement));
}

std::size_t CandidateGrid::candidate_index_for_digit(const std::size_t digit) const noexcept {
    if (digit == 0U || digit > items_per_row() || candidate_count_ == 0U) {
        return invalid_index;
    }
    const std::size_t index = active_row_ * items_per_row() + digit - 1U;
    return index < candidate_count_ ? index : invalid_index;
}

std::size_t CandidateGrid::first_visible_index() const noexcept {
    return first_visible_row_ * items_per_row();
}

std::size_t CandidateGrid::visible_item_count() const noexcept {
    const std::size_t first = first_visible_index();
    if (first >= candidate_count_) {
        return 0U;
    }
    const std::size_t capacity = items_per_row() * visible_rows();
    return (std::min)(capacity, candidate_count_ - first);
}

std::size_t CandidateGrid::selected_index() const noexcept {
    return candidate_count_ == 0U ? invalid_index : active_row_ * items_per_row();
}

std::size_t CandidateGrid::candidate_count() const noexcept {
    return candidate_count_;
}

std::size_t CandidateGrid::items_per_row() const noexcept {
    return settings_.items_per_row;
}

std::size_t CandidateGrid::visible_rows() const noexcept {
    return settings_.visible_rows;
}

std::size_t CandidateGrid::active_row() const noexcept {
    return active_row_;
}

std::size_t CandidateGrid::first_visible_row() const noexcept {
    return first_visible_row_;
}

std::size_t CandidateGrid::row_count() const noexcept {
    return candidate_count_ == 0U
        ? 0U
        : (candidate_count_ - 1U) / items_per_row() + 1U;
}

void CandidateGrid::clamp_view() noexcept {
    const std::size_t rows = row_count();
    if (rows == 0U) {
        active_row_ = 0U;
        first_visible_row_ = 0U;
        return;
    }

    active_row_ = (std::min)(active_row_, rows - 1U);
    if (active_row_ < first_visible_row_) {
        first_visible_row_ = active_row_;
    } else if (active_row_ >= first_visible_row_ + visible_rows()) {
        first_visible_row_ = active_row_ - visible_rows() + 1U;
    }

    const std::size_t last_window_start = rows > visible_rows() ? rows - visible_rows() : 0U;
    first_visible_row_ = (std::min)(first_visible_row_, last_window_start);
}

}  // namespace piinput
