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
    active_column_ = 0U;
    first_visible_row_ = 0U;
    expanded_ = false;
    clamp_view();
}

void CandidateGrid::set_items_per_row(const std::uint32_t items_per_row) noexcept {
    settings_.items_per_row = (std::max)(items_per_row, 1U);
    clamp_view();
}

void CandidateGrid::set_candidate_count(const std::size_t candidate_count) noexcept {
    candidate_count_ = candidate_count;
    if (candidate_count_ == 0U) {
        expanded_ = false;
    }
    clamp_view();
}

void CandidateGrid::move_row(const int delta) noexcept {
    if (candidate_count_ == 0U || delta == 0) {
        return;
    }
    if (delta < 0 && !expanded_ && active_row_ == 0U) {
        return;
    }
    expanded_ = true;
    const std::size_t previous_row = active_row_;
    const auto last_row = row_count() - 1U;
    if (delta > 0) {
        const std::size_t step = static_cast<std::size_t>(delta);
        const std::size_t remaining = last_row - active_row_;
        active_row_ += (std::min)(step, remaining);
    } else {
        const std::size_t step =
            static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        active_row_ = step > active_row_ ? 0U : active_row_ - step;
    }
    if (active_row_ != previous_row) {
        active_column_ = 0U;
    }
    clamp_view();
}

void CandidateGrid::move_column(const int delta) noexcept {
    if (candidate_count_ == 0U || delta == 0) return;
    const std::size_t row_start = active_row_ * items_per_row();
    const std::size_t row_count = (std::min)(items_per_row(), candidate_count_ - row_start);
    if (delta > 0) {
        const std::size_t step = static_cast<std::size_t>(delta);
        active_column_ += (std::min)(step, row_count - 1U - active_column_);
    } else {
        const std::size_t step = static_cast<std::size_t>(-static_cast<std::int64_t>(delta));
        active_column_ = step > active_column_ ? 0U : active_column_ - step;
    }
}

void CandidateGrid::select_index(const std::size_t index) noexcept {
    if (candidate_count_ == 0U) return;
    const std::size_t clamped = (std::min)(index, candidate_count_ - 1U);
    active_row_ = clamped / items_per_row();
    active_column_ = clamped % items_per_row();
    if (active_row_ != 0U) expanded_ = true;
    clamp_view();
}

void CandidateGrid::move_page(const int delta) noexcept {
    const auto rows = static_cast<std::int64_t>(settings_.visible_rows);
    const auto movement = (std::clamp)(
        static_cast<std::int64_t>(delta) * rows,
        static_cast<std::int64_t>((std::numeric_limits<int>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)()));
    move_row(static_cast<int>(movement));
}

bool CandidateGrid::can_move_row(
    const int delta,
    const std::size_t eligible_candidate_count) const noexcept {
    if (candidate_count_ == 0U || delta == 0) return false;
    if (delta < 0) return active_row_ != 0U;
    const std::size_t eligible = (std::min)(candidate_count_, eligible_candidate_count);
    if (eligible == 0U || active_row_ == (std::numeric_limits<std::size_t>::max)()) {
        return false;
    }
    const std::size_t next_row = active_row_ + 1U;
    if (next_row > (std::numeric_limits<std::size_t>::max)() / items_per_row()) {
        return false;
    }
    return next_row * items_per_row() < eligible;
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
    return candidate_count_ == 0U
        ? invalid_index
        : active_row_ * items_per_row() + active_column_;
}

std::size_t CandidateGrid::candidate_count() const noexcept {
    return candidate_count_;
}

std::size_t CandidateGrid::items_per_row() const noexcept {
    return settings_.items_per_row;
}

std::size_t CandidateGrid::visible_rows() const noexcept {
    return expanded_ ? settings_.visible_rows : 1U;
}

std::size_t CandidateGrid::active_row() const noexcept {
    return active_row_;
}

std::size_t CandidateGrid::active_column() const noexcept {
    return active_column_;
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
        active_column_ = 0U;
        first_visible_row_ = 0U;
        return;
    }

    active_row_ = (std::min)(active_row_, rows - 1U);
    const std::size_t row_start = active_row_ * items_per_row();
    const std::size_t columns = (std::min)(items_per_row(), candidate_count_ - row_start);
    active_column_ = (std::min)(active_column_, columns - 1U);
    if (active_row_ < first_visible_row_) {
        first_visible_row_ = active_row_;
    } else if (active_row_ >= first_visible_row_ + visible_rows()) {
        first_visible_row_ = active_row_ - visible_rows() + 1U;
    }

    const std::size_t last_window_start = rows > visible_rows() ? rows - visible_rows() : 0U;
    first_visible_row_ = (std::min)(first_visible_row_, last_window_start);
}

}  // namespace piinput
