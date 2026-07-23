#include "piinput/candidate_grid.h"
#include "piinput/settings.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

piinput::CandidateSettings settings(
    const std::uint32_t items_per_row,
    const std::uint32_t visible_rows) {
    auto value = piinput::default_settings().candidates;
    value.items_per_row = items_per_row;
    value.visible_rows = visible_rows;
    return value;
}

void test_supported_dimensions() {
    for (std::uint32_t columns = 5U; columns <= 9U; ++columns) {
        for (std::uint32_t rows = 1U; rows <= 5U; ++rows) {
            piinput::CandidateGrid grid(settings(columns, rows), 90U);
            check(grid.items_per_row() == columns, "preserves configured column count");
            check(grid.visible_rows() == rows, "preserves configured visible row count");
            check(grid.visible_item_count() == static_cast<std::size_t>(columns * rows),
                "visible count matches configured grid size");
        }
    }
}

void test_row_navigation_and_scrolling() {
    piinput::CandidateGrid grid(settings(6U, 3U), 40U);
    check(grid.active_row() == 0U, "starts on first row");
    check(grid.first_visible_row() == 0U, "starts with first row visible");
    check(grid.selected_index() == 0U, "starts on first candidate");

    grid.move_row(1);
    check(grid.active_row() == 1U && grid.first_visible_row() == 0U,
        "moves down one row without scrolling");
    check(grid.selected_index() == 6U, "active item follows the active row");

    grid.move_row(1);
    grid.move_row(1);
    check(grid.active_row() == 3U && grid.first_visible_row() == 1U,
        "visible window scrolls by one row");
    check(grid.first_visible_index() == 6U, "first visible index follows the row window");
    check(grid.visible_item_count() == 18U, "full three-row window remains visible");

    grid.move_row(-99);
    check(grid.active_row() == 0U && grid.first_visible_row() == 0U,
        "upper boundary clamps without wrapping");

    grid.move_row(99);
    check(grid.active_row() == 6U && grid.first_visible_row() == 4U,
        "lower boundary clamps to the final partial row");
    check(grid.selected_index() == 36U, "final row selects its first candidate");
    check(grid.visible_item_count() == 16U, "visible count includes a partial final row");

    grid.move_row(1);
    check(grid.active_row() == 6U && grid.first_visible_row() == 4U,
        "moving past the lower boundary is a no-op");
}

void test_digit_selection_is_scoped_to_active_row() {
    piinput::CandidateGrid grid(settings(6U, 3U), 14U);
    grid.move_row(1);
    check(grid.candidate_index_for_digit(1U) == 6U, "digit one selects the active row first column");
    check(grid.candidate_index_for_digit(6U) == 11U, "last configured digit selects its column");
    check(grid.candidate_index_for_digit(7U) == piinput::CandidateGrid::invalid_index,
        "digit beyond the configured row width is invalid");
    check(grid.candidate_index_for_digit(0U) == piinput::CandidateGrid::invalid_index,
        "digit zero is invalid");

    grid.move_row(1);
    check(grid.active_row() == 2U, "moves to the partial final row");
    check(grid.candidate_index_for_digit(1U) == 12U, "partial row first candidate is selectable");
    check(grid.candidate_index_for_digit(2U) == 13U, "partial row last candidate is selectable");
    check(grid.candidate_index_for_digit(3U) == piinput::CandidateGrid::invalid_index,
        "digit beyond the partial row is invalid");
}

void test_candidate_count_changes_are_clamped() {
    piinput::CandidateGrid grid(settings(5U, 2U), 23U);
    grid.move_row(4);
    check(grid.active_row() == 4U && grid.first_visible_row() == 3U,
        "reaches final partial row before shrinking");

    grid.set_candidate_count(7U);
    check(grid.candidate_count() == 7U, "updates candidate count");
    check(grid.active_row() == 1U && grid.first_visible_row() == 0U,
        "shrinking clamps active and visible rows");
    check(grid.selected_index() == 5U, "shrinking keeps selected index in range");
    check(grid.visible_item_count() == 7U, "shrinking updates visible count");

    grid.set_candidate_count(0U);
    check(grid.active_row() == 0U && grid.first_visible_row() == 0U,
        "empty candidates reset row state");
    check(grid.selected_index() == piinput::CandidateGrid::invalid_index,
        "empty candidates have no selected item");
    check(grid.visible_item_count() == 0U, "empty candidates have no visible items");
    check(grid.candidate_index_for_digit(1U) == piinput::CandidateGrid::invalid_index,
        "empty candidates reject digit selection");

    grid.reset(12U);
    check(grid.candidate_count() == 12U, "reset updates candidate count");
    check(grid.active_row() == 0U && grid.first_visible_row() == 0U,
        "reset returns to the first row");
    check(grid.selected_index() == 0U, "reset selects the first candidate");
}

void test_page_navigation_clamps_without_wrapping() {
    piinput::CandidateGrid grid(settings(5U, 3U), 41U);
    grid.move_page(1);
    check(grid.active_row() == 3U && grid.first_visible_row() == 1U,
        "page down advances by visible row count and follows active row");
    grid.move_page(99);
    check(grid.active_row() == 8U && grid.first_visible_row() == 6U,
        "page down clamps at final row");
    grid.move_page(-99);
    check(grid.active_row() == 0U && grid.first_visible_row() == 0U,
        "page up clamps at first row");
}

void test_candidate_count_arithmetic_does_not_overflow() {
    piinput::CandidateGrid grid(
        settings(5U, 3U), (std::numeric_limits<std::size_t>::max)());
    grid.move_row(1);
    check(grid.active_row() == 1U, "maximum candidate count still has a second row");
    check(grid.selected_index() == 5U, "maximum candidate count keeps selected arithmetic valid");
    check(grid.visible_item_count() == 15U, "maximum candidate count retains a full visible window");
}

}  // namespace

int main() {
    test_supported_dimensions();
    test_row_navigation_and_scrolling();
    test_digit_selection_is_scoped_to_active_row();
    test_candidate_count_changes_are_clamped();
    test_page_navigation_clamps_without_wrapping();
    test_candidate_count_arithmetic_does_not_overflow();
    std::cout << "All CandidateGrid tests passed\n";
    return 0;
}
