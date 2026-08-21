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
    // Back to a single row at the top, keeping the candidates. reset() would
    // do the same but takes the count with it, so callers that only want the
    // view folded had to pass the current count back in to stand still.
    void collapse() noexcept;
    [[nodiscard]] bool expanded() const noexcept;
    void set_items_per_row(std::uint32_t items_per_row) noexcept;
    // How many rows may be on screen at once. Normally the configured value;
    // a list that is meant to be seen whole sets its own.
    void set_visible_rows(std::size_t visible_rows) noexcept;
    // Show the rows without waiting for the user to page into them.
    void expand() noexcept;
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
    // How many rows the candidates occupy. Public because paging past an end
    // needs to know whether there is another row to wrap to, and recomputing
    // it from the count and the row width outside would duplicate the rule.
    [[nodiscard]] std::size_t row_count() const noexcept;

private:
    void clamp_view() noexcept;

    CandidateSettings settings_;
    std::size_t candidate_count_{};
    std::size_t active_row_{};
    std::size_t active_column_{};
    std::size_t first_visible_row_{};
    bool expanded_{};
};

}  // namespace piinput
