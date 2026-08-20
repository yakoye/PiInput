#pragma once

#include "piinput/settings.h"

#include <cstddef>
#include <limits>

namespace piinput {

class CandidateGrid final {
public:
    static constexpr std::size_t invalid_index = (std::numeric_limits<std::size_t>::max)();

    CandidateGrid(CandidateSettings settings, std::size_t candidate_count) noexcept;

    void reset(std::size_t candidate_count) noexcept;
    void set_items_per_row(std::uint32_t items_per_row) noexcept;
    void set_candidate_count(std::size_t candidate_count) noexcept;
    void move_row(int delta) noexcept;
    void move_column(int delta) noexcept;
    void select_index(std::size_t index) noexcept;
    void move_page(int delta) noexcept;
    [[nodiscard]] bool can_move_row(
        int delta,
        std::size_t eligible_candidate_count) const noexcept;

    [[nodiscard]] std::size_t candidate_index_for_digit(std::size_t digit) const noexcept;
    [[nodiscard]] std::size_t first_visible_index() const noexcept;
    [[nodiscard]] std::size_t visible_item_count() const noexcept;
    [[nodiscard]] std::size_t selected_index() const noexcept;

    [[nodiscard]] std::size_t candidate_count() const noexcept;
    [[nodiscard]] std::size_t items_per_row() const noexcept;
    [[nodiscard]] std::size_t visible_rows() const noexcept;
    [[nodiscard]] std::size_t active_row() const noexcept;
    [[nodiscard]] std::size_t active_column() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;

private:
    [[nodiscard]] std::size_t row_count() const noexcept;
    void clamp_view() noexcept;

    CandidateSettings settings_;
    std::size_t candidate_count_{};
    std::size_t active_row_{};
    std::size_t active_column_{};
    std::size_t first_visible_row_{};
    bool expanded_{};
};

}  // namespace piinput
