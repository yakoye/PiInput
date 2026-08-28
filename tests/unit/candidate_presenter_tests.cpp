#include "candidate_presenter.h"
#include "text_caret_geometry.h"

#include <windows.h>

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_candidate_context_commands_include_dismissal() {
    const auto pin = piinput::windows::candidate_context_action_from_command(1U);
    const auto remove = piinput::windows::candidate_context_action_from_command(2U);
    const auto unpin = piinput::windows::candidate_context_action_from_command(3U);
    const auto dismiss = piinput::windows::candidate_context_action_from_command(0U);
    check(pin.has_value() && *pin == piinput::windows::CandidateContextAction::pin_first,
        "context command 1 pins the candidate");
    check(remove.has_value() &&
            *remove == piinput::windows::CandidateContextAction::delete_candidate,
        "context command 2 deletes the candidate for this exact pinyin");
    check(unpin.has_value() && *unpin == piinput::windows::CandidateContextAction::unpin,
        "context command 3 cancels a fixed-first preference without deleting learning");
    check(dismiss.has_value() && *dismiss == piinput::windows::CandidateContextAction::dismiss,
        "Escape or an outside click dismisses the context menu and composition");
}

piinput::HostSnapshot snapshot(
    const std::uint64_t generation,
    const bool expanded,
    const std::size_t visible_rows) {
    piinput::HostSnapshot value;
    value.generation = generation;
    value.raw = "drlojuzi";
    value.view = {
        .expanded = expanded,
        .items_per_row = 6U,
        .visible_rows = visible_rows,
        .active_row = expanded ? 1U : 0U,
        .first_visible_row = 0U,
    };
    for (std::size_t index = 0; index < 18U; ++index) {
        value.candidates.push_back({index + 1U, "候选" + std::to_string(index), {}, 0});
    }
    return value;
}

void test_presenter_model_keeps_candidates_visible_while_caret_is_resolved() {
    piinput::windows::CandidatePresenterModel model;
    check(model.stage(11U, snapshot(10U, false, 1U)), "first session snapshot is staged");
    check(model.focused_session() == 11U && model.visible_rows() == 1U,
        "first snapshot is retained while its caret is resolved");
    check(model.current_caret() == nullptr,
        "a new context does not flash at the mouse before TSF reports caret availability");
    const piinput::HostCaretUpdate caret{
        .generation = 10U,
        .has_text_caret = true,
        .left = 400,
        .top = 500,
        .right = 402,
        .bottom = 524,
    };
    check(model.apply_caret(11U, caret), "matching text caret is accepted");
    check(model.focused_session() == 11U && model.visible_rows() == 1U,
        "matching text caret reveals exactly one candidate row");
    check(model.current_caret() != nullptr && *model.current_caret() == caret,
        "presenter retains the exact accepted text caret");

    check(!model.stage(11U, snapshot(9U, true, 3U)), "older generation is rejected");
    check(model.visible_rows() == 1U, "stale expansion cannot alter the current view");
    check(model.stage(11U, snapshot(11U, true, 3U)), "newer expansion is staged");
    check(model.focused_session() == 11U && model.visible_rows() == 3U,
        "new candidate contents remain visible instead of hiding between key and caret messages");
    check(model.current_caret() != nullptr && model.current_caret()->has_text_caret &&
            model.current_caret()->generation == 11U,
        "new snapshot reuses the last text caret while awaiting refreshed geometry");
    check(!model.apply_caret(11U, caret), "older caret cannot reveal a newer snapshot");
    const auto future = piinput::HostCaretUpdate{.generation = 12U, .has_text_caret = false};
    check(!model.apply_caret(11U, future), "future caret cannot reveal an older snapshot");
    const auto matching_fallback = piinput::HostCaretUpdate{
        .generation = 11U,
        .has_text_caret = false,
    };
    check(model.apply_caret(11U, matching_fallback),
        "matching unavailable caret explicitly selects mouse fallback");
    check(model.visible_rows() == 3U, "formal expansion renders configured rows");
    check(model.current_caret() != nullptr && !model.current_caret()->has_text_caret,
        "presenter records mouse fallback for applications without text geometry");

    check(model.stage(22U, snapshot(1U, false, 1U)), "another session can be staged");
    check(model.focused_session() == 22U && model.current_caret() == nullptr,
        "a new session waits for its own caret result instead of reusing another window's anchor");
    check(!model.apply_caret(11U, {.generation = 1U, .has_text_caret = false}),
        "caret for another session cannot reveal the staged session");
    check(model.apply_caret(22U, {.generation = 1U, .has_text_caret = false}),
        "matching caret reveals the latest active session");
    check(model.focused_session() == 22U, "focus switches to the latest active session");
    model.hide(22U);
    check(model.focused_session() == 0U, "hiding the focused session clears presentation focus");
}

void test_an_early_matching_caret_opens_the_snapshot() {
    piinput::windows::CandidatePresenterModel model;
    const piinput::HostCaretUpdate early{
        .generation = 10U,
        .has_text_caret = true,
        .left = 400,
        .top = 500,
        .right = 402,
        .bottom = 524,
    };
    model.remember_caret(11U, early);
    check(model.stage(11U, snapshot(10U, false, 1U)),
        "the snapshot accepts a caret that raced ahead of staging");
    const auto* consumed = model.current_caret();
    check(consumed != nullptr && *consumed == early,
        "the exact matching early caret opens the candidate window");

    piinput::windows::CandidatePresenterModel fallback_model;
    fallback_model.remember_caret(11U, {
        .generation = 10U,
        .has_text_caret = false,
    });
    check(fallback_model.stage(11U, snapshot(10U, false, 1U)),
        "the snapshot accepts an early no-geometry result");
    const auto* fallback = fallback_model.current_caret();
    check(fallback != nullptr && !fallback->has_text_caret,
        "an explicit no-geometry result opens at the normal fallback instead of waiting forever");
}

void test_an_early_caret_must_match_generation_and_session() {
    piinput::windows::CandidatePresenterModel model;
    model.remember_caret(11U, {
        .generation = 9U,
        .has_text_caret = true,
        .left = 100,
        .top = 200,
        .right = 102,
        .bottom = 224,
    });
    check(model.stage(11U, snapshot(10U, false, 1U)),
        "a newer snapshot is staged after an older early caret");
    check(model.current_caret() == nullptr,
        "a stale generation cannot open the candidate window");

    piinput::windows::CandidatePresenterModel other_session;
    other_session.remember_caret(22U, {
        .generation = 10U,
        .has_text_caret = true,
        .left = 100,
        .top = 200,
        .right = 102,
        .bottom = 224,
    });
    check(other_session.stage(11U, snapshot(10U, false, 1U)),
        "a snapshot is staged after another session's early caret");
    check(other_session.current_caret() == nullptr,
        "another session's caret cannot open the candidate window");
}

void test_unmatched_raw_input_does_not_show_an_empty_candidate_frame() {
    piinput::windows::CandidatePresenterModel model;
    piinput::HostSnapshot unmatched;
    unmatched.generation = 1U;
    unmatched.raw = "fdsafds";
    unmatched.composition_text = unmatched.raw;
    unmatched.view.items_per_row = 6U;
    unmatched.view.visible_rows = 1U;

    check(model.stage(19U, unmatched), "unmatched raw input is staged");
    check(model.focused_session() == 0U && model.visible_rows() == 0U &&
            model.current_snapshot() == nullptr,
        "raw input without real candidates leaves no blank candidate window");
}

void test_candidate_geometry_scales_and_clamps_to_monitor_work_area() {
    const RECT caret{1900, 1040, 1902, 1060};
    const RECT work{0, 0, 1920, 1080};
    const SIZE desired{600, 240};
    const RECT placed = piinput::windows::place_candidate_window(caret, desired, work, 144U);
    check(placed.left >= work.left && placed.top >= work.top,
        "candidate window stays inside monitor top-left bounds");
    check(placed.right <= work.right && placed.bottom <= work.bottom,
        "candidate window stays inside monitor bottom-right bounds");
    check(placed.right > placed.left && placed.bottom > placed.top,
        "candidate window keeps a positive clamped size");

    const RECT anchor{400, 500, 402, 524};
    const RECT roomy_work{0, 0, 2560, 1440};
    const RECT at_125_percent = piinput::windows::place_candidate_window(
        anchor, {700, 80}, roomy_work, 120U, 4);
    check(at_125_percent.left == anchor.left && at_125_percent.top == anchor.bottom + 5,
        "125-percent text popup uses a crisp DPI-scaled five-pixel caret gap");
    // A line of text is twice as tall at 200 percent, so the caret reported for
    // one is too. The 24-pixel rectangle above would be half a line here, and
    // the placement now lifts such a rectangle to a full line before anchoring.
    const RECT tall_anchor{400, 500, 402, 548};
    const RECT at_200_percent = piinput::windows::place_candidate_window(
        tall_anchor, {1120, 128}, roomy_work, 192U, 4);
    check(at_200_percent.left == tall_anchor.left &&
            at_200_percent.top == tall_anchor.bottom + 8,
        "200-percent text popup uses the target monitor DPI before placement");
}

void test_a_host_that_hides_top_most_windows_pushes_the_bar_clear() {
    // Real numbers from a window trace on Windows 11 25H2. The search panel is
    // drawn in a band no ordinary top-most window reaches, so the bar placed at
    // the caret was on screen, reported visible by every API, and invisible to
    // the user -- the input looked dead while Space still committed correctly.
    const RECT caret{854, 309, 854, 333};
    const RECT panel{560, 260, 1780, 900};
    const RECT work{0, 0, 2048, 1104};
    const SIZE desired{626, 45};

    const RECT unavoided = piinput::windows::place_candidate_window(
        caret, desired, work, 120U, 4);
    check(unavoided.top > caret.bottom && unavoided.top < panel.bottom,
        "without the obstruction the bar lands inside the panel, as it did in the field");

    const RECT avoided = piinput::windows::place_candidate_window(
        caret, desired, work, 120U, 4, &panel);
    check(avoided.top < panel.top,
        "the bar sits above the panel rather than inside it");
    // The panel rectangle is bigger than what it paints, so the bar deliberately
    // sinks back into that dead margin instead of floating clear of it.
    check(avoided.bottom > panel.top && avoided.bottom < panel.top + 24,
        "only the margin-compensating sliver reaches into the panel rectangle");
    check(avoided.top >= work.top,
        "clearing the panel still respects the top of the work area");

    // A panel already against the top of the screen leaves no room above, so
    // the bar goes under it instead of being pushed off-screen.
    const RECT top_panel{560, 0, 1780, 600};
    const RECT below = piinput::windows::place_candidate_window(
        caret, desired, work, 120U, 4, &top_panel);
    check(below.top >= top_panel.bottom,
        "a panel with no room above it pushes the bar below instead");

    // Every other host keeps the placement it always had. A caret outside the
    // obstruction is not touched, so a stray rectangle cannot move a bar that
    // was already correct.
    const RECT elsewhere{200, 950, 200, 974};
    const RECT untouched = piinput::windows::place_candidate_window(
        elsewhere, desired, work, 120U, 4, &panel);
    const RECT baseline = piinput::windows::place_candidate_window(
        elsewhere, desired, work, 120U, 4);
    check(untouched.top == baseline.top && untouched.left == baseline.left,
        "a bar that never overlapped the obstruction is placed exactly as before");
}

void test_a_caret_shorter_than_its_text_line_does_not_get_covered() {
    // Real numbers from a caret trace: WeChat reports a two-pixel-high
    // insertion point, while an ordinary application on the same machine
    // reports twenty-four. Anchoring to the bottom of the two-pixel rectangle
    // dropped the bar onto the line of text the user was still typing.
    const RECT wechat{2201, 1071, 2203, 1073};
    const RECT work{0, 0, 2560, 1380};
    const RECT placed = piinput::windows::place_candidate_window(
        wechat, {746, 50}, work, 120U, 4);
    check(placed.top >= wechat.top + 20,
        "a caret shorter than a line of text must not put the popup over the text");
    check(placed.top > wechat.bottom,
        "the popup still sits below the reported insertion point");

    // An application that reports a real line height keeps the tight gap.
    const RECT ordinary{710, 1169, 710, 1193};
    const RECT normal = piinput::windows::place_candidate_window(
        ordinary, {590, 50}, work, 120U, 4);
    check(normal.top == ordinary.bottom + 5,
        "a caret that already spans its line keeps the five-pixel gap");
}

void test_candidate_geometry_covers_all_eight_work_area_boundaries() {
    const RECT work{0, 0, 1920, 1080};
    const SIZE desired{600, 80};
    constexpr LONG gap = 4;
    const std::array<RECT, 8U> carets{
        RECT{4, 4, 6, 28},
        RECT{959, 4, 961, 28},
        RECT{1914, 4, 1916, 28},
        RECT{4, 520, 6, 544},
        RECT{1914, 520, 1916, 544},
        RECT{4, 1052, 6, 1076},
        RECT{959, 1052, 961, 1076},
        RECT{1914, 1052, 1916, 1076},
    };
    for (std::size_t index = 0U; index < carets.size(); ++index) {
        const RECT placed = piinput::windows::place_candidate_window(
            carets[index], desired, work, 96U, gap);
        check(placed.left >= 4 && placed.top >= 4 &&
                placed.right <= 1916 && placed.bottom <= 1076,
            "all eight boundary placements retain the visible frame inset");
        if (index < 5U) {
            check(placed.top >= carets[index].bottom + gap,
                "top and side boundaries keep the popup below the insertion caret");
        } else {
            check(placed.bottom <= carets[index].top - gap,
                "bottom boundaries flip the popup above the insertion caret");
        }
    }
}

void test_text_caret_placement_preserves_the_full_caret_height() {
    const RECT work{0, 0, 1920, 1080};
    const RECT caret{900, 1040, 902, 1064};
    const RECT placed = piinput::windows::place_candidate_window_at_text_caret(
        caret, {600, 80}, work, 96U, 4);
    check(placed.bottom <= caret.top - 4,
        "a bottom-edge text caret flips the popup above the caret top, not its bottom point");
}

void test_compact_candidate_height_has_no_composition_header_row() {
    check(piinput::windows::candidate_window_height(1U, 96U) == 40,
        "default collapsed candidate window is exactly 40 DIP high");
    check(piinput::windows::candidate_window_height(3U, 96U, 48U) == 108,
        "each explicitly expanded candidate row adds 30 DIP");
    check(piinput::windows::candidate_window_height(1U, 192U, 48U) == 96,
        "compact candidate height scales once at 200 percent DPI");
    check(piinput::windows::candidate_window_height(1U, 96U, 20U) == 20,
        "minimum configured candidate height is honored");
    check(piinput::windows::candidate_window_height(3U, 96U, 72U) == 132,
        "configured first-row height keeps stable thirty-DIP expansion rows");
}

void test_candidate_rows_remain_vertically_aligned_at_minimum_height() {
    const RECT first = piinput::windows::candidate_row_rect(0U, 96U, 20U);
    check(first.top == 0 && first.bottom == 20,
        "the first candidate row uses the configured twenty-DIP height");
    const RECT second = piinput::windows::candidate_row_rect(1U, 96U, 20U);
    check(second.top == 20 && second.bottom == 50,
        "expanded rows start immediately after the compact first row");
    const RECT scaled = piinput::windows::candidate_row_rect(0U, 192U, 20U);
    check(scaled.top == 0 && scaled.bottom == 40,
        "minimum row bounds scale exactly once at 200-percent DPI");
}

void test_candidate_text_is_centered_from_actual_font_metrics() {
    check(piinput::windows::candidate_text_top({0, 0, 0, 40}, 19) == 10,
        "nineteen-pixel Chinese glyph metrics are centered in a forty-pixel row");
    check(piinput::windows::candidate_text_top({0, 40, 0, 70}, 18) == 46,
        "expanded rows center text relative to their own top edge");
    check(piinput::windows::candidate_text_top({0, 0, 0, 20}, 24) == 0,
        "oversized text clamps to the row top instead of producing a negative origin");
}

void test_candidate_visual_settings_allow_ten_dip_text() {
    const piinput::windows::CandidateVisualSettings compact{10U, 20U};
    check(compact.font_size == 10U && compact.window_height == 20U,
        "the compact visual preset combines ten-DIP text with a twenty-DIP row");
}

void test_candidate_toolbar_hit_testing_does_not_consume_candidates() {
    const RECT client{0, 0, 600, 48};
    check(piinput::windows::candidate_toolbar_hit_test(
              {579, 24}, client, false, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::open_menu,
        "the fixed trailing grid button opens the tool menu");
    check(piinput::windows::candidate_toolbar_hit_test(
              {545, 24}, client, false, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::expand_candidates,
        "the chevron button explicitly expands the candidate rows");
    check(piinput::windows::candidate_toolbar_hit_test(
              {510, 24}, client, false, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::none,
        "candidate text before the trailing button remains candidate space");

    const RECT open_client{0, 0, 600, 120};
    check(piinput::windows::candidate_toolbar_hit_test(
              {550, 60}, open_client, true, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::symbols,
        "the first menu row selects Symbols");
    check(piinput::windows::candidate_toolbar_hit_test(
              {550, 100}, open_client, true, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::settings,
        "the second menu row selects Settings");
    check(piinput::windows::candidate_toolbar_hit_test(
              {300, 60}, open_client, true, 96U, 48U) ==
            piinput::windows::CandidateToolbarAction::none,
        "the compact menu never turns the whole popup into a tool hit target");
}

void test_candidate_geometry_stays_fixed_for_one_composition() {
    const RECT first{400, 529, 980, 607};
    const RECT moved_caret_proposal{520, 529, 1100, 607};
    const RECT ordinary = piinput::windows::stabilize_candidate_window_rect(
        &first, moved_caret_proposal, false);
    check(EqualRect(&ordinary, &first) != FALSE,
        "ordinary candidate updates keep the original position, width, and height");

    const RECT expanded_proposal{520, 529, 1100, 667};
    const RECT expanded = piinput::windows::stabilize_candidate_window_rect(
        &first, expanded_proposal, true);
    check(expanded.left == first.left && expanded.top == first.top,
        "explicit row expansion keeps the composition anchor fixed");
    check(expanded.right == first.right,
        "explicit row expansion keeps the original candidate width");
    check(expanded.bottom - expanded.top == expanded_proposal.bottom - expanded_proposal.top,
        "explicit row expansion changes only the candidate height");

    const RECT next_composition = piinput::windows::stabilize_candidate_window_rect(
        nullptr, moved_caret_proposal, false);
    check(EqualRect(&next_composition, &moved_caret_proposal) != FALSE,
        "a new composition is positioned from its new text caret");
}

void test_candidate_geometry_keeps_anchor_but_can_grow_to_show_long_text() {
    const RECT locked{120, 180, 500, 252};
    const RECT wider{120, 180, 920, 252};
    const RECT grown = piinput::windows::stabilize_candidate_window_rect(
        &locked, wider, false);
    check(grown.left == locked.left && grown.top == locked.top,
        "long candidates keep the original composition anchor");
    check(grown.right == wider.right && grown.bottom == locked.bottom,
        "candidate width can grow when full text needs more room");

    const RECT narrower{120, 180, 360, 252};
    const RECT stable = piinput::windows::stabilize_candidate_window_rect(
        &grown, narrower, false);
    check(stable.right == grown.right,
        "later short candidates do not make the popup jitter narrower");

    check(piinput::windows::limit_candidate_window_width(1800, 96U) == 600,
        "candidate popup has a compact 600-DIP maximum width");
    check(piinput::windows::limit_candidate_window_width(1800, 192U) == 1200,
        "candidate popup maximum width follows the target monitor DPI");
    check(piinput::windows::limit_candidate_window_width(480, 192U) == 480,
        "a naturally compact popup is not enlarged by the limit");
}

void test_candidate_growth_is_reclamped_to_the_anchor_monitor() {
    const RECT right_monitor_work{1920, 0, 3840, 1040};
    const RECT escaped_after_stabilizing{3380, 500, 3980, 548};
    const RECT reclamped = piinput::windows::clamp_candidate_window_rect(
        escaped_after_stabilizing, right_monitor_work, 144U);
    check(reclamped.left == 3234 && reclamped.right == 3834,
        "a wider locked popup keeps a visible six-pixel frame inside the target monitor");
    check(reclamped.top == 500 && reclamped.bottom == 548,
        "reclamping horizontal growth preserves the candidate row vertical anchor");

    const RECT left_monitor_work{-1920, 0, 0, 1040};
    const RECT negative_escape{-2050, 220, -1450, 268};
    const RECT negative_reclamped = piinput::windows::clamp_candidate_window_rect(
        negative_escape, left_monitor_work, 96U);
    check(negative_reclamped.left == -1916 && negative_reclamped.right == -1316,
        "negative-coordinate monitors keep the same four-pixel visible frame inset");
}

void test_real_text_caret_can_replace_only_the_initial_mouse_fallback() {
    check(piinput::windows::should_reanchor_candidate_window(true, false, true),
        "the first real TSF text caret replaces a provisional mouse anchor");
    check(!piinput::windows::should_reanchor_candidate_window(true, true, true),
        "later TSF caret updates cannot move a locked composition window");
    check(!piinput::windows::should_reanchor_candidate_window(true, true, false),
        "a mouse fallback cannot replace an established text-caret anchor");
    check(piinput::windows::should_reanchor_candidate_window(false, false, false),
        "a new composition accepts its first available anchor");
}

// A commit hides the bar and drops the live caret. Without a remembered anchor
// the first key of the next word shows nothing until the caret round trip
// completes, which is what made typing after a punctuation feel delayed.
void test_a_word_that_opens_reports_that_its_anchor_must_be_released() {
    piinput::windows::CandidatePresenterModel model;

    // Opening a word: the previous word's anchor must be given up. Relying on
    // hide() having happened is what left the bar at the previous position.
    check(model.stage(11U, snapshot(10U, false, 1U)), "word one is staged");
    check(model.word_just_opened(), "the first staged snapshot opens a word");
    check(model.apply_caret(11U, {
            .generation = 10U, .has_text_caret = true,
            .left = 307, .top = 840, .right = 308, .bottom = 867,
        }), "word one anchors at its real caret");

    // Continuing the same word: the anchor is held so the bar does not slide
    // right as letters are added.
    check(model.stage(11U, snapshot(11U, false, 1U)), "the word continues");
    check(!model.word_just_opened(), "continuing a word keeps its anchor");
    check(model.apply_caret(11U, {
            .generation = 11U, .has_text_caret = true,
            .left = 331, .top = 840, .right = 332, .bottom = 867,
        }), "the moved insertion point is accepted");
    check(model.current_caret()->left == 307, "the bar stays where the word began");

    // Committing and typing again opens a word, wherever the user now is.
    model.hide(11U);
    check(model.stage(11U, snapshot(12U, false, 1U)), "the next word is staged");
    check(model.word_just_opened(), "a word after a commit opens again");
    check(model.apply_caret(11U, {
            .generation = 12U, .has_text_caret = true,
            .left = 175, .top = 1164, .right = 176, .bottom = 1191,
        }), "the next word's caret arrives");
    check(model.current_caret()->top == 1164, "it anchors at the new insertion point");
}

void test_a_composition_extent_is_not_mistaken_for_a_caret() {
    // Measured in Notepad++: the selection range answers GetTextExt with the
    // extent of the whole composition, 121 pixels wide, and that rectangle
    // reports the composition's line. In 30 of 40 samples it disagreed with the
    // real collapsed caret by whole line heights.
    const RECT extent{1880, 544, 2001, 570};
    const RECT caret{1892, 490, 1892, 518};
    check(!piinput::windows::caret_rect_is_plausible(extent),
        "a 121 pixel wide rectangle is a text extent, not an insertion point");
    check(piinput::windows::caret_rect_is_plausible(caret),
        "a collapsed rectangle is an insertion point");

    const auto chosen = piinput::windows::choose_text_caret_geometry(&extent, &caret);
    check(chosen.has_value() && chosen->top == 490,
        "the real caret wins over the composition extent, whichever came first");

    // A block caret is still a caret.
    const RECT block{100, 200, 112, 227};
    check(piinput::windows::caret_rect_is_plausible(block),
        "a block caret one character wide is still an insertion point");

    // With nothing plausible the widest available is better than no position.
    const auto fallback = piinput::windows::choose_text_caret_geometry(&extent, nullptr);
    check(fallback.has_value() && fallback->left == 1880,
        "an extent is still used when no real caret is available");
}

void test_a_word_never_opens_on_a_guessed_position() {
    piinput::windows::CandidatePresenterModel model;

    // Word one, anchored by the caret captured in its own edit session.
    check(model.stage(11U, snapshot(10U, false, 1U)), "word one is staged");
    check(model.apply_caret(11U, {
            .generation = 10U, .has_text_caret = true,
            .left = 150, .top = 230, .right = 152, .bottom = 254,
        }), "word one anchors at its real caret");
    model.hide(11U);

    // The user clicks somewhere else and types. Nothing told the Host where
    // that is yet, and the previous word's caret is not an answer: showing the
    // bar there is what put it a step behind.
    check(model.stage(11U, snapshot(11U, false, 1U)), "word two is staged");
    check(model.current_caret() == nullptr,
        "the bar waits instead of opening at the previous word's position");

    // It appears as soon as the authoritative caret lands, and only there.
    check(model.apply_caret(11U, {
            .generation = 11U, .has_text_caret = true,
            .left = 300, .top = 785, .right = 302, .bottom = 809,
        }), "the real caret for word two arrives");
    const auto* placed = model.current_caret();
    check(placed != nullptr && placed->top == 785 && placed->left == 300,
        "the bar opens exactly at the insertion point");

    // Later letters push the insertion point right; the bar holds its place.
    check(model.stage(11U, snapshot(12U, false, 1U)), "the word keeps growing");
    check(model.apply_caret(11U, {
            .generation = 12U, .has_text_caret = true,
            .left = 336, .top = 785, .right = 338, .bottom = 809,
        }), "a later caret in the same word is accepted");
    check(model.current_caret()->left == 300, "the anchor holds for the word");
}

void test_the_anchor_does_not_drift_while_the_word_grows() {
    piinput::windows::CandidatePresenterModel model;
    check(model.stage(11U, snapshot(10U, false, 1U)), "the word opens");
    check(model.apply_caret(11U, {
            .generation = 10U, .has_text_caret = true,
            .left = 300, .top = 500, .right = 302, .bottom = 524,
        }), "the first real caret anchors the bar");

    // Every further letter pushes the insertion point right. The bar must stay.
    for (std::uint64_t generation = 11U; generation < 15U; ++generation) {
        check(model.stage(11U, snapshot(generation, false, 1U)),
            "the growing word keeps staging");
        check(model.apply_caret(11U, {
                .generation = generation, .has_text_caret = true,
                .left = 300 + static_cast<LONG>(generation) * 12,
                .top = 500,
                .right = 302 + static_cast<LONG>(generation) * 12,
                .bottom = 524,
            }), "the moving insertion point is accepted");
        const auto* held = model.current_caret();
        check(held != nullptr && held->left == 300 && held->top == 500,
            "the bar stays where the word began");
    }

    // Committing releases the anchor so the next word can be placed afresh.
    model.hide(11U);
    check(model.stage(11U, snapshot(20U, false, 1U)), "a new word is staged");
    check(model.apply_caret(11U, {
            .generation = 20U, .has_text_caret = true,
            .left = 700, .top = 500, .right = 702, .bottom = 524,
        }), "the new word gets its own caret");
    const auto* fresh = model.current_caret();
    check(fresh != nullptr && fresh->left == 700,
        "the next word anchors at its own position");
}

void test_switching_application_sessions_resets_candidate_geometry() {
    check(piinput::windows::candidate_session_changed(11U, 22U),
        "switching to another application starts a new candidate geometry lifetime");
    check(!piinput::windows::candidate_session_changed(11U, 11U),
        "continued typing in one application keeps the current geometry lock");
    check(!piinput::windows::candidate_session_changed(0U, 22U),
        "the first active session has no stale geometry to reset");
}

void test_candidate_popup_is_owned_by_the_text_view_root() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND root = CreateWindowExW(
        0U, L"STATIC", L"PiInput candidate owner test", WS_OVERLAPPED,
        0, 0, 320, 120, nullptr, nullptr, instance, nullptr);
    check(root != nullptr, "candidate owner test creates a top-level text host");
    const HWND child = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE,
        8, 8, 240, 28, root, nullptr, instance, nullptr);
    check(child != nullptr, "candidate owner test creates a focused child control");

    piinput::windows::CandidateWindow window;
    check(window.create(instance), "candidate popup class is created");
    window.update(L"ni", {L"你", L"呢"}, 0U, 0U, 0U, 6U, 1U);
    window.show_at_text_caret(
        {20, 20, 22, 44},
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(child)));
    check(piinput::windows::resolve_candidate_owner(
              static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(child))) == root,
        "a text child is normalized to its top-level host");
    check(GetWindow(window.native_handle(), GW_OWNER) == root,
        "CreateWindowEx binds the candidate popup to the text host root");

    DestroyWindow(root);
    window.show_at_text_caret({30, 30, 32, 54});
    check(window.native_handle() != nullptr && IsWindow(window.native_handle()) != FALSE &&
            GetWindow(window.native_handle(), GW_OWNER) == nullptr,
        "a destroyed text host causes the candidate popup to be recreated safely");
    window.destroy();
}

void test_presenter_identity_keeps_same_session_number_isolated_per_process() {
    const auto first = piinput::windows::candidate_presentation_id(1001U, 1U);
    const auto second = piinput::windows::candidate_presentation_id(1002U, 1U);
    const auto next_session = piinput::windows::candidate_presentation_id(1001U, 2U);
    check(first != second,
        "the same TSF session number in different application processes stays isolated");
    check(first != next_session,
        "different sessions in one process stay isolated");
    check(first == piinput::windows::candidate_presentation_id(1001U, 1U),
        "candidate presentation identity is stable for repeated messages");
}

void test_text_caret_geometry_prefers_the_actual_selection_over_composition_extent() {
    const RECT selection{141, 16, 143, 39};
    const RECT duplicated_composition_end{237, 16, 239, 39};
    const auto chosen = piinput::windows::choose_text_caret_geometry(
        &selection, &duplicated_composition_end);
    check(chosen.has_value() && EqualRect(&*chosen, &selection) != FALSE,
        "the actual insertion selection wins when an editor reports a displaced composition end");
}

void test_text_caret_geometry_uses_composition_only_as_a_valid_fallback() {
    const RECT unavailable{0, 0, 0, 0};
    const RECT composition{93, 23, 95, 46};
    const auto fallback = piinput::windows::choose_text_caret_geometry(
        &unavailable, &composition);
    check(fallback.has_value() && EqualRect(&*fallback, &composition) != FALSE,
        "a valid composition caret is used when the application selection has no layout");
    check(!piinput::windows::choose_text_caret_geometry(&unavailable, &unavailable).has_value(),
        "zero rectangles never masquerade as a text caret");
}

void test_a_caret_with_no_height_is_not_a_caret() {
    // MobaXterm's exact answer: one fixed point, zero height, returned
    // unchanged on every keystroke, sitting just outside its own window at the
    // bottom right. Accepted as a caret it pinned the candidate bar to the
    // corner of the screen for a whole session.
    const RECT flat{1919, 1019, 1920, 1019};
    check(!piinput::windows::usable_text_caret_rect(flat),
        "a rectangle with no height is not a position the caret could be at");
    check(!piinput::windows::caret_rect_is_plausible(flat),
        "the flat rectangle is rejected by the narrower test as well");
    const RECT real{770, 212, 780, 232};
    check(piinput::windows::usable_text_caret_rect(real) &&
            piinput::windows::caret_rect_is_plausible(real),
        "the system caret the same application reports is still accepted");
    // A caret drawn as a bare line has no width, and that is not the same
    // defect: only height is required.
    const RECT bar{770, 212, 770, 232};
    check(piinput::windows::usable_text_caret_rect(bar) &&
            piinput::windows::caret_rect_is_plausible(bar),
        "a zero-width caret bar remains usable");
    const RECT flat_only{1919, 1019, 1920, 1019};
    check(!piinput::windows::choose_text_caret_geometry(&flat_only, &flat_only).has_value(),
        "no source offering a flat rectangle produces a caret");
}

void test_text_caret_dpi_normalization_does_not_double_scale_per_monitor_apps() {
    const RECT reported{18, 24, 20, 48};
    const RECT converted{27, 36, 30, 72};
    const RECT per_monitor = piinput::windows::normalized_text_caret_geometry(
        reported, converted, true);
    check(EqualRect(&per_monitor, &reported) != FALSE,
        "per-monitor-aware applications already report physical caret coordinates");
    const RECT legacy = piinput::windows::normalized_text_caret_geometry(
        reported, converted, false);
    check(EqualRect(&legacy, &converted) != FALSE,
        "legacy DPI-aware applications still use the physical-coordinate conversion");
}

}  // namespace

int main() {
    test_candidate_context_commands_include_dismissal();
    test_presenter_model_keeps_candidates_visible_while_caret_is_resolved();
    test_an_early_matching_caret_opens_the_snapshot();
    test_an_early_caret_must_match_generation_and_session();
    test_unmatched_raw_input_does_not_show_an_empty_candidate_frame();
    test_candidate_geometry_scales_and_clamps_to_monitor_work_area();
    test_a_host_that_hides_top_most_windows_pushes_the_bar_clear();
    test_a_caret_shorter_than_its_text_line_does_not_get_covered();
    test_candidate_geometry_covers_all_eight_work_area_boundaries();
    test_text_caret_placement_preserves_the_full_caret_height();
    test_compact_candidate_height_has_no_composition_header_row();
    test_candidate_rows_remain_vertically_aligned_at_minimum_height();
    test_candidate_text_is_centered_from_actual_font_metrics();
    test_candidate_visual_settings_allow_ten_dip_text();
    test_candidate_toolbar_hit_testing_does_not_consume_candidates();
    test_candidate_geometry_stays_fixed_for_one_composition();
    test_candidate_geometry_keeps_anchor_but_can_grow_to_show_long_text();
    test_candidate_growth_is_reclamped_to_the_anchor_monitor();
    test_real_text_caret_can_replace_only_the_initial_mouse_fallback();
    test_a_word_that_opens_reports_that_its_anchor_must_be_released();
    test_a_composition_extent_is_not_mistaken_for_a_caret();
    test_a_word_never_opens_on_a_guessed_position();
    test_the_anchor_does_not_drift_while_the_word_grows();
    test_switching_application_sessions_resets_candidate_geometry();
    test_candidate_popup_is_owned_by_the_text_view_root();
    test_presenter_identity_keeps_same_session_number_isolated_per_process();
    test_text_caret_geometry_prefers_the_actual_selection_over_composition_extent();
    test_text_caret_geometry_uses_composition_only_as_a_valid_fallback();
    test_a_caret_with_no_height_is_not_a_caret();
    test_text_caret_dpi_normalization_does_not_double_scale_per_monitor_apps();
    std::cout << "PiInput candidate presenter tests passed.\n";
    return 0;
}
