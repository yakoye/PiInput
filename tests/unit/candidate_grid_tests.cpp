#include "piinput/candidate_layout.h"
#include "piinput/candidate_grid.h"
#include "piinput/settings.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

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
            check(grid.visible_rows() == 1U, "starts collapsed to exactly one visible row");
            check(grid.visible_item_count() == static_cast<std::size_t>(columns),
                "initial visible count is exactly one configured row");
        }
    }
}

void test_row_navigation_expands_until_the_next_candidate_reset() {
    piinput::CandidateGrid grid(settings(6U, 3U), 40U);
    check(grid.visible_rows() == 1U && grid.visible_item_count() == 6U,
        "new candidate snapshots render one row only");

    grid.move_row(1);
    check(grid.visible_rows() == 3U && grid.visible_item_count() == 18U,
        "down or equals expands the configured multi-row window");
    check(grid.active_row() == 1U,
        "the expansion key also advances by exactly one row");

    grid.reset(24U);
    check(grid.visible_rows() == 1U && grid.visible_item_count() == 6U,
        "new input collapses an expanded window back to one row");

    grid.move_row(-1);
    check(grid.visible_rows() == 1U && grid.active_row() == 0U,
        "up or minus before the first row keeps a fresh snapshot collapsed");
}

void test_trusted_row_boundary_requests_segmented_selection() {
    piinput::CandidateGrid grid(settings(6U, 3U), 30U);
    check(!grid.can_move_row(1, 6U),
        "a second row made only of untrusted guesses is not exposed");
    check(grid.can_move_row(1, 7U),
        "a trusted candidate on the second row allows normal expansion");
    grid.move_row(1);
    check(grid.can_move_row(-1, 7U), "expanded view can return to the previous row");
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

void test_row_movement_saturates_without_signed_conversion() {
    piinput::CandidateGrid grid(
        settings(1U, 1U), (std::numeric_limits<std::size_t>::max)());

    grid.move_row(INT_MAX);
    check(grid.active_row() == static_cast<std::size_t>(INT_MAX),
        "INT_MAX moves safely within a SIZE_MAX row range");

    grid.move_row(INT_MAX);
    check(grid.active_row() == static_cast<std::size_t>(INT_MAX) * 2U,
        "repeated positive movement uses size_t saturation");

    grid.move_row(INT_MIN);
    check(grid.active_row() == static_cast<std::size_t>(INT_MAX) - 1U,
        "INT_MIN subtracts its full magnitude without signed overflow");

    grid.move_row(INT_MIN);
    check(grid.active_row() == 0U, "negative movement saturates at the first row");
}

void test_candidate_grid_supports_horizontal_and_vertical_selection() {
    auto settings = piinput::default_settings().candidates;
    settings.items_per_row = 6U;
    settings.visible_rows = 3U;
    piinput::CandidateGrid grid(settings, 14U);

    grid.move_column(1);
    grid.move_column(1);
    check(grid.active_column() == 2U && grid.selected_index() == 2U,
        "right arrow selects the next candidate in the current row");
    grid.move_row(1);
    check(grid.active_row() == 1U && grid.active_column() == 0U &&
            grid.selected_index() == 6U,
        "down arrow selects the first candidate in the next row");
    grid.move_column(1);
    grid.move_row(1);
    check(grid.selected_index() == 12U && grid.active_column() == 0U,
        "partial last row also begins at its first candidate");
    grid.move_column(1);
    check(grid.selected_index() == 13U,
        "right arrow selects the next candidate in a partial row");
    grid.move_row(-1);
    check(grid.selected_index() == 6U && grid.active_column() == 0U,
        "up arrow selects the first candidate in the previous row");
    grid.move_column(-1);
    check(grid.selected_index() == 6U,
        "left arrow selects the previous candidate in the current row");
}

void test_candidate_grid_can_activate_a_segment_fallback() {
    auto settings = piinput::default_settings().candidates;
    settings.items_per_row = 6U;
    settings.visible_rows = 3U;
    piinput::CandidateGrid grid(settings, 18U);
    grid.select_index(8U);
    check(grid.selected_index() == 8U && grid.active_row() == 1U &&
            grid.active_column() == 2U && grid.visible_rows() == 3U,
        "segment fallback selection expands the grid and highlights its exact candidate");
}

void test_candidate_column_layout_fits_narrow_widths() {
    const std::vector<int> desired(9U, 180);
    const auto fitted = piinput::fit_candidate_column_widths(desired, 100);
    check(fitted.size() == desired.size(), "layout preserves all configured columns");
    check(std::all_of(fitted.begin(), fitted.end(), [](const int width) {
        return width >= 1;
    }), "layout keeps columns non-empty when the width budget permits");
    check(std::accumulate(fitted.begin(), fitted.end(), 0) <= 100,
        "layout never exceeds a narrow monitor width");

    const auto minimal = piinput::fit_candidate_column_widths(desired, -20);
    check(std::all_of(minimal.begin(), minimal.end(), [](const int width) {
        return width >= 0;
    }), "layout never produces negative column widths");
    check(std::accumulate(minimal.begin(), minimal.end(), 0) <= 1,
        "layout normalizes a non-positive monitor width to one pixel");

    const std::vector<int> already_fits{64, 72, 80};
    check(piinput::fit_candidate_column_widths(already_fits, 400) == already_fits,
        "layout preserves desired widths when they already fit");
}

void test_candidate_header_shows_composition_only() {
    check(piinput::candidate_header_text(L"yuwh") == L"yuwh",
        "candidate header omits the configured input-scheme name");
}

void test_long_candidates_reduce_columns_without_changing_short_word_defaults() {
    check(piinput::candidate_items_per_row(
              6U, {"今天", "很好", "记住", "这个", "输入法", "测试"}) == 6U,
        "ordinary words preserve the configured six-column row");
    check(piinput::candidate_items_per_row(
              6U, {"我今天下午要去超市", "我今天下午准备出门"}) == 2U,
        "medium sentence candidates use two readable columns");
    check(piinput::candidate_items_per_row(
              6U, {"没有完成测试", "输入法候选窗"}) == 2U,
        "a two-item phrase list exposes exactly its two available entries");
    check(piinput::candidate_items_per_row(
              6U, {"中文输入", "符号映射"}) == 2U,
        "a two-item short phrase list does not invent empty columns");
    check(piinput::candidate_items_per_row(
              6U, {"我今天下午要去超市买点水果然后回家"}) == 1U,
        "long sentence candidates use one full-width column");
}

void test_candidate_row_fills_ordered_short_entries_after_long_phrases() {
    check(piinput::candidate_items_per_row(
              6U,
              {"打字字母需要处理", "打字字幕需要处理", "位置", "候选", "输入", "测试"}) == 5U,
        "short later candidates fill the remaining first-row width after two long phrases");
}

void test_later_long_candidate_does_not_shrink_an_ordinary_first_row() {
    check(piinput::candidate_items_per_row(
              6U,
              {"今天", "很好", "记住", "这个", "输入", "测试",
               "这个很长的后排候选不应该改变第一页"}) == 6U,
        "a long candidate outside the configured first row cannot reduce its column count");
}

void test_candidate_grid_can_change_column_count_at_a_generation_boundary() {
    piinput::CandidateGrid grid(settings(6U, 3U), 18U);
    grid.move_column(5);
    grid.set_items_per_row(1U);
    grid.reset(18U);
    check(grid.items_per_row() == 1U,
        "candidate grid adopts the presentation column count");
    check(grid.selected_index() == 0U && grid.active_column() == 0U,
        "changing columns at a new generation resets to a valid first candidate");
}

}  // namespace

int main() {
    test_supported_dimensions();
    test_row_navigation_expands_until_the_next_candidate_reset();
    test_trusted_row_boundary_requests_segmented_selection();
    test_row_navigation_and_scrolling();
    test_digit_selection_is_scoped_to_active_row();
    test_candidate_count_changes_are_clamped();
    test_page_navigation_clamps_without_wrapping();
    test_candidate_count_arithmetic_does_not_overflow();
    test_row_movement_saturates_without_signed_conversion();
    test_candidate_grid_supports_horizontal_and_vertical_selection();
    test_candidate_grid_can_activate_a_segment_fallback();
    test_candidate_column_layout_fits_narrow_widths();
    test_candidate_header_shows_composition_only();
    test_long_candidates_reduce_columns_without_changing_short_word_defaults();
    test_candidate_row_fills_ordered_short_entries_after_long_phrases();
    test_later_long_candidate_does_not_shrink_an_ordinary_first_row();
    test_candidate_grid_can_change_column_count_at_a_generation_boundary();
    std::cout << "All CandidateGrid tests passed\n";
    return 0;
}
