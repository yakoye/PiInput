#include "composition_mirror.h"
#include "composition_edit_policy.h"
#include "deferred_update_queue.h"
#include "final_edit_key_queue.h"

#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

piinput::HostReply update_reply(
    const std::uint64_t generation,
    std::string raw,
    const std::size_t caret) {
    piinput::HostReply reply;
    reply.accepted = true;
    reply.action = piinput::HostAction::update;
    reply.snapshot.generation = generation;
    reply.snapshot.raw = std::move(raw);
    reply.snapshot.composition_text = reply.snapshot.raw;
    reply.snapshot.caret = caret;
    reply.snapshot.candidates.push_back({
        .id = generation * 100U,
        .text = "candidate",
        .pinyin = "candidate",
        .score = 10,
    });
    return reply;
}

void test_segment_composition_is_displayed_without_corrupting_resume_state() {
    piinput::windows::CompositionMirror mirror(9U, 10U);
    auto reply = update_reply(12U, "ruhdlq", 6U);
    reply.snapshot.composition_text = "\xE9\xBB\x84\xE6\xB2\xB3ruhdlq";
    reply.snapshot.caret = reply.snapshot.composition_text.size();
    reply.snapshot.view.mode = piinput::HostCandidateMode::segment_selection;

    check(mirror.confirm(mirror.begin_request(), reply),
        "segment-selection reply is accepted");
    check(mirror.composition_text() == reply.snapshot.composition_text &&
            mirror.caret() == reply.snapshot.composition_text.size(),
        "TSF sees the staged Chinese prefix followed by unresolved pinyin");
    const auto resume = mirror.resume_state();
    check(resume.raw == "ruhdlq" && resume.caret == 6U,
        "Host restart resumes only unresolved raw pinyin with a valid raw caret");
}

void test_sequences_and_generations_are_monotonic_and_stale_replies_are_ignored() {
    piinput::windows::CompositionMirror mirror(77U, 91U);
    const auto first = mirror.begin_request();
    const auto second = mirror.begin_request();
    check(first.client_id == 77U && first.session_id == 91U && first.sequence == 1U,
        "mirror stamps client/session and starts sequence at one");
    check(second.sequence == 2U, "mirror sequences increase monotonically");

    check(mirror.confirm(second, update_reply(5U, "wo", 2U)),
        "newer reply is accepted");
    check(mirror.raw() == "wo" && mirror.generation() == 5U,
        "accepted reply updates the local raw composition");
    check(mirror.snapshot().candidates.size() == 1U &&
            mirror.snapshot().candidates.front().id == 500U,
        "accepted reply retains the exact Host candidate snapshot for selection");
    check(!mirror.confirm(first, update_reply(4U, "w", 1U)),
        "out-of-order reply is rejected");
    check(mirror.raw() == "wo", "stale reply cannot roll back local composition");
    check(mirror.snapshot().candidates.front().id == 500U,
        "stale reply cannot replace the confirmed candidate snapshot");
}

void test_only_the_latest_confirmed_update_may_edit_the_application_composition() {
    piinput::windows::CompositionMirror mirror(77U, 92U);
    const auto older = mirror.begin_request();
    check(mirror.confirm(older, update_reply(5U, "gan", 3U)),
        "older composition update is initially confirmed");
    check(mirror.is_current_update(older),
        "the latest confirmed update is eligible to edit the application");

    const auto newer = mirror.begin_request();
    check(mirror.confirm(newer, update_reply(6U, "ganjue", 6U)),
        "newer composition update is confirmed");
    check(!mirror.is_current_update(older),
        "an older deferred edit cannot overwrite a newer composition");
    check(mirror.is_current_update(newer),
        "the newest deferred edit remains eligible to update the composition");
}

void test_deferred_updates_coalesce_to_the_latest_composition() {
    piinput::windows::DeferredUpdateQueue queue;
    const piinput::windows::MirrorRequest first{77U, 92U, 4U, 5U};
    check(queue.begin(first), "the first asynchronous composition edit becomes active");
    check(!queue.begin({77U, 92U, 5U, 6U}),
        "a second asynchronous composition edit cannot run concurrently");

    queue.defer({{77U, 92U, 5U, 6U}, "ganj", 4U});
    queue.defer({{77U, 92U, 6U, 7U}, "ganju", 5U});
    queue.defer({{77U, 92U, 7U, 8U}, "ganjue", 6U});
    const auto replay = queue.complete(first);
    check(replay.has_value() && replay->request.sequence == 7U &&
            replay->text == "ganjue" && replay->caret == 6U,
        "completing an asynchronous edit replays only the latest deferred composition");
    check(!queue.busy(), "the completed edit releases the single asynchronous slot");
}

void test_composition_finalization_discards_all_deferred_updates() {
    piinput::windows::DeferredUpdateQueue queue;
    const piinput::windows::MirrorRequest inflight{77U, 92U, 8U, 9U};
    check(queue.begin(inflight), "an asynchronous composition update is active");
    queue.defer({{77U, 92U, 9U, 10U}, "gjjt", 4U});

    queue.clear();

    check(!queue.busy(), "commit or cancel releases the asynchronous update slot");
    check(!queue.complete(inflight).has_value(),
        "an update queued before commit cannot replay into the next composition");
}

void test_pending_final_edit_holds_the_next_composition_update() {
    piinput::windows::DeferredUpdateQueue queue;
    const piinput::windows::MirrorRequest commit{77U, 92U, 10U, 11U};
    const piinput::windows::MirrorRequest next_word{77U, 92U, 11U, 12U};
    check(queue.begin(commit), "an asynchronous commit owns the edit-session barrier");
    queue.defer({next_word, "xmzd", 4U});
    check(queue.busy(), "the next composition waits until the final edit completes");

    const auto replay = queue.complete(commit);
    check(replay.has_value() && replay->request.sequence == next_word.sequence &&
            replay->text == "xmzd",
        "the latest next-word composition replays after the previous commit finishes");
}

void test_disconnect_preserves_raw_and_resume_state() {
    piinput::windows::CompositionMirror mirror(3U, 4U);
    const auto request = mirror.begin_request();
    check(mirror.confirm(request, update_reply(8U, "hlheruhdlq", 6U)),
        "composition is confirmed before disconnect");
    mirror.disconnect();
    check(!mirror.connected() && mirror.raw() == "hlheruhdlq" && mirror.caret() == 6U,
        "disconnect preserves raw text and caret locally");
    const auto resume = mirror.resume_state();
    check(resume.raw == "hlheruhdlq" && resume.caret == 6U && resume.generation == 8U,
        "resume handshake contains only trusted local state");
}

void test_external_termination_discards_raw_and_rejects_inflight_replies() {
    piinput::windows::CompositionMirror mirror(31U, 41U);
    check(mirror.confirm(mirror.begin_request(), update_reply(2U, "jpgorutusoui", 12U)),
        "composition exists before the application terminates it");
    const auto inflight = mirror.begin_request();

    mirror.discard_composition();

    check(mirror.raw().empty() && mirror.composition_text().empty() && mirror.caret() == 0U,
        "external termination clears the locally mirrored raw input immediately");
    check(!mirror.confirm(inflight, update_reply(3U, "jpgorutusouij", 13U)),
        "an in-flight reply from before termination cannot resurrect the old composition");
    check(mirror.resume_state().raw.empty(),
        "focus recovery after external termination cannot restore duplicate raw letters");
}

void test_failed_commit_rolls_back_until_edit_session_confirms() {
    piinput::windows::CompositionMirror mirror(1U, 2U);
    const auto update = mirror.begin_request();
    check(mirror.confirm(update, update_reply(2U, "wo", 2U)), "raw composition is present");

    piinput::HostReply commit;
    commit.accepted = true;
    commit.action = piinput::HostAction::commit;
    commit.text = "我";
    commit.snapshot.generation = 3U;
    const auto commit_request = mirror.begin_request();
    check(mirror.confirm(commit_request, commit), "commit reply is staged");
    check(mirror.raw() == "wo" && mirror.pending_commit() == "我",
        "Host commit does not clear local raw state before TSF succeeds");
    const auto recovery = mirror.complete_edit(false);
    check(recovery.has_value() && recovery->raw == "wo" && recovery->caret == 2U,
        "failed TSF edit returns the exact trusted state needed to restore the Host");
    check(mirror.raw() == "wo" && mirror.pending_commit().empty(),
        "failed TSF edit rolls back the pending commit locally");

    const auto retry = mirror.begin_request();
    check(mirror.confirm(retry, commit), "commit can be staged again");
    const auto completed = mirror.complete_edit(true);
    check(!completed.has_value(), "successful TSF edit does not request Host recovery");
    check(mirror.raw().empty() && mirror.generation() == 3U,
        "successful TSF edit atomically accepts Host commit and clears raw state");
}

void test_tool_action_waits_for_the_cancel_edit() {
    piinput::windows::CompositionMirror mirror(5U, 7U);
    check(mirror.confirm(mirror.begin_request(), update_reply(2U, "fh", 2U)),
        "tool shortcut composition is mirrored before selection");

    piinput::HostReply launch;
    launch.accepted = true;
    launch.action = piinput::HostAction::launch_symbol_tool;
    launch.snapshot.generation = 3U;
    check(mirror.confirm(mirror.begin_request(), launch) &&
            mirror.pending_action() == piinput::HostAction::launch_symbol_tool &&
            mirror.raw() == "fh",
        "tool action remains pending until TSF removes the composition");
    const auto recovery = mirror.complete_edit(false);
    check(recovery.has_value() && recovery->raw == "fh" &&
            mirror.pending_action() == piinput::HostAction::none,
        "failed cancellation restores raw input and drops the launch action");

    check(mirror.confirm(mirror.begin_request(), launch),
        "tool action can be selected again after recovery");
    check(!mirror.complete_edit(true).has_value() && mirror.raw().empty(),
        "successful cancellation accepts the cleared Host snapshot");

    check(mirror.confirm(mirror.begin_request(), update_reply(4U, "jsq", 3U)),
        "calculator shortcut composition is mirrored");
    piinput::HostReply program;
    program.accepted = true;
    program.action = piinput::HostAction::launch_program;
    program.text = "package:regcalc64";
    program.snapshot.generation = 5U;
    check(mirror.confirm(mirror.begin_request(), program) &&
            mirror.pending_action() == piinput::HostAction::launch_program &&
            mirror.pending_commit() == "package:regcalc64",
        "generic launch target remains pending until the cancel edit succeeds");
    check(!mirror.complete_edit(true).has_value() && mirror.pending_commit().empty(),
        "successful cancellation consumes the generic launch target exactly once");
}

void test_late_commit_completion_cannot_erase_the_next_composition() {
    piinput::windows::CompositionMirror mirror(17U, 29U);
    check(mirror.confirm(mirror.begin_request(), update_reply(4U, "gjjt", 4U)),
        "the first word is composing before it is selected");

    auto commit_reply = update_reply(5U, "", 0U);
    commit_reply.action = piinput::HostAction::commit;
    commit_reply.text = "\xE6\x84\x9F\xE8\xA7\x89";
    const auto commit_request = mirror.begin_request();
    check(mirror.confirm(commit_request, commit_reply),
        "the first word waits for the application's commit edit session");

    const auto next_request = mirror.begin_request();
    check(mirror.confirm(next_request, update_reply(6U, "xmzd", 4U)),
        "the next word may arrive before the previous asynchronous commit completes");
    check(mirror.raw() == "xmzd", "the next word is locally confirmed");

    check(!mirror.complete_edit(true).has_value(), "the previous commit succeeds");
    check(mirror.raw() == "xmzd" && mirror.generation() == 6U,
        "a late successful commit cannot overwrite the newer composition snapshot");
}

void test_new_text_context_starts_an_isolated_host_session() {
    piinput::windows::CompositionMirror mirror(21U, 31U);
    const auto old_request = mirror.begin_request();
    check(mirror.confirm(old_request, update_reply(7U, "wogjjthfhcys", 12U)),
        "the first text context has an active composition");

    mirror.reset_session(32U);
    const auto new_request = mirror.begin_request();
    check(new_request.session_id == 32U && new_request.sequence == 1U,
        "a new text context gets an independent session and sequence space");
    check(mirror.raw().empty() && mirror.composition_text().empty() &&
            mirror.caret() == 0U && mirror.generation() == 0U,
        "a new text context cannot inherit another tab's composition");
    check(!mirror.confirm(old_request, update_reply(8U, "wogjjthfhcysw", 13U)),
        "a delayed reply from the old text context is rejected");
}

void test_final_commit_does_not_depend_on_optional_caret_selection() {
    const auto update = piinput::windows::composition_edit_policy(false, false);
    check(!update.finalize_before_selection && update.selection_failure_is_fatal,
        "an in-progress composition still requires its caret selection to be updated");

    const auto commit = piinput::windows::composition_edit_policy(true, false);
    check(commit.finalize_before_selection && !commit.selection_failure_is_fatal,
        "a committed candidate is finalized before optional caret placement");

    const auto cancel = piinput::windows::composition_edit_policy(false, true);
    check(cancel.finalize_before_selection && !cancel.selection_failure_is_fatal,
        "cancelling a composition also cannot be blocked by caret placement");
}

void test_rapid_punctuation_commits_are_serialized_at_the_tsf_boundary() {
    piinput::windows::FinalEditKeyQueue queue;
    queue.begin_final_edit();
    check(queue.should_queue(true),
        "a pending commit still blocks another key that could commit as well");
    check(!queue.should_queue(false),
        "an ordinary letter does not wait for the pending commit's TSF edit");

    queue.push({.kind = piinput::HostKeyKind::text, .character = 't'});
    check(queue.should_queue(false),
        "once anything is queued, later keys stay behind it to keep order");
    queue.push({.kind = piinput::HostKeyKind::text, .character = 'a'});
    queue.push({.kind = piinput::HostKeyKind::punctuation, .character = '.'});
    check(queue.size() == 3U,
        "all keys typed during the final TSF edit are retained in order");

    const auto first = queue.complete_final_edit();
    check(first.has_value() && first->character == 't' && queue.replay_inflight(),
        "only the first queued key is replayed after the first commit completes");
    check(!queue.complete_final_edit().has_value(),
        "a replayed key reply must arrive before another queued key is sent");

    const auto second = queue.complete_replayed_reply(false);
    check(second.has_value() && second->character == 'a',
        "an ordinary update releases exactly the next queued key");
    const auto punctuation = queue.complete_replayed_reply(false);
    check(punctuation.has_value() && punctuation->kind == piinput::HostKeyKind::punctuation,
        "the second punctuation remains ordered after the second Chinese segment");

    check(!queue.complete_replayed_reply(true).has_value() &&
            queue.final_edit_pending(),
        "a replayed punctuation reply starts a new final-edit barrier");
    queue.push({.kind = piinput::HostKeyKind::text, .character = 'w'});
    check(!queue.complete_replayed_reply(false).has_value() && queue.size() == 1U,
        "later Chinese input cannot bypass the second pending punctuation commit");
    check(!piinput::windows::key_may_begin_final_edit(
              piinput::HostKeyKind::text, false),
        "a Chinese letter only ever updates the composition");
    check(piinput::windows::key_may_begin_final_edit(
              piinput::HostKeyKind::text, true),
        "direct English echoes each letter back as a commit");
    check(piinput::windows::key_may_begin_final_edit(
              piinput::HostKeyKind::punctuation, false) &&
            piinput::windows::key_may_begin_final_edit(
                piinput::HostKeyKind::space, false) &&
            piinput::windows::key_may_begin_final_edit(
                piinput::HostKeyKind::backspace, false),
        "punctuation, space and backspace can all finalize a composition");
    check(!piinput::windows::key_may_begin_final_edit(
              piinput::HostKeyKind::next_candidate, false) &&
            !piinput::windows::key_may_begin_final_edit(
                piinput::HostKeyKind::expand_next_row, false),
        "candidate navigation never finalizes a composition");
    const auto next_segment = queue.complete_final_edit();
    check(next_segment.has_value() && next_segment->character == 'w',
        "the next Chinese segment resumes after the second commit succeeds");
}

}  // namespace

int main() {
    test_sequences_and_generations_are_monotonic_and_stale_replies_are_ignored();
    test_only_the_latest_confirmed_update_may_edit_the_application_composition();
    test_deferred_updates_coalesce_to_the_latest_composition();
    test_composition_finalization_discards_all_deferred_updates();
    test_pending_final_edit_holds_the_next_composition_update();
    test_disconnect_preserves_raw_and_resume_state();
    test_external_termination_discards_raw_and_rejects_inflight_replies();
    test_failed_commit_rolls_back_until_edit_session_confirms();
    test_tool_action_waits_for_the_cancel_edit();
    test_late_commit_completion_cannot_erase_the_next_composition();
    test_segment_composition_is_displayed_without_corrupting_resume_state();
    test_new_text_context_starts_an_isolated_host_session();
    test_final_commit_does_not_depend_on_optional_caret_selection();
    test_rapid_punctuation_commits_are_serialized_at_the_tsf_boundary();
    std::cout << "PiInput composition mirror tests passed.\n";
    return 0;
}
