#include "session_manager.h"

#include "piinput/host_messages.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

piinput::HostEnvelope key_envelope(
    const std::uint64_t client,
    const std::uint64_t session,
    const std::uint64_t sequence,
    const char character) {
    return {
        .version = piinput::host_protocol_v1,
        .client_id = client,
        .session_id = session,
        .sequence = sequence,
        .generation = sequence,
        .type = piinput::HostMessageType::key_event,
        .payload = piinput::encode_host_key_event({
            .kind = piinput::HostKeyKind::text,
            .character = character,
        }),
    };
}

piinput::HostReply decode_reply(const piinput::HostEnvelope& envelope) {
    piinput::HostPayloadError error = piinput::HostPayloadError::none;
    const auto reply = piinput::decode_host_reply(envelope.payload, error, envelope.version);
    check(reply.has_value(), "session manager reply payload decodes");
    return *reply;
}

void test_manager_isolates_sessions_and_rejects_nonmonotonic_sequences() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-session-manager.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n我\two\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");

    const auto first = manager.dispatch(key_envelope(1U, 10U, 1U, 'w'));
    check(first.has_value() && decode_reply(*first).snapshot.raw == "w",
        "first client session is created on demand");
    const auto second = manager.dispatch(key_envelope(1U, 10U, 2U, 'o'));
    const auto second_reply = decode_reply(*second);
    check(second_reply.snapshot.raw == "wo" && !second_reply.snapshot.candidates.empty() &&
            second_reply.snapshot.candidates.front().text == "我",
        "ordered keys update only their target host session");
    check(!manager.dispatch(key_envelope(1U, 10U, 2U, 'x')).has_value(),
        "duplicate sequence is rejected before mutating state");
    check(!manager.dispatch(key_envelope(1U, 10U, 1U, 'x')).has_value(),
        "older sequence is rejected before mutating state");

    const auto independent = manager.dispatch(key_envelope(2U, 10U, 1U, 'w'));
    check(independent.has_value() && decode_reply(*independent).snapshot.raw == "w",
        "same session id from another client remains isolated");
    check(manager.session_count() == 2U, "manager tracks two independent client/session pairs");
    std::filesystem::remove(path);
}

void test_resume_recomputes_state_in_a_new_managed_session() {
    piinput::Engine engine;
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");
    const piinput::HostResumeState state{8U, "drlojuzi", 4U, piinput::HostInputMode::chinese};
    piinput::HostEnvelope request{
        .version = piinput::host_protocol_v1,
        .client_id = 9U,
        .session_id = 12U,
        .sequence = 1U,
        .generation = 8U,
        .type = piinput::HostMessageType::resume,
        .payload = piinput::encode_host_resume_state(state),
    };
    const auto response = manager.dispatch(request);
    check(response.has_value(), "resume request is accepted");
    const auto reply = decode_reply(*response);
    check(reply.snapshot.raw == "drlojuzi" && reply.snapshot.caret == 4U &&
            reply.snapshot.generation > state.generation,
        "managed resume restores raw/caret and recomputes a newer generation");
}

void test_first_key_after_host_restart_restores_confirmed_composition() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-session-restart.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n我\two\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager restarted(
        engine, nullptr, piinput::default_settings(), "full");
    auto request = key_envelope(19U, 21U, 42U, 'o');
    piinput::HostKeyEvent event{
        .kind = piinput::HostKeyKind::text,
        .character = 'o',
        .resume = piinput::HostResumeState{
            8U, "w", 1U, piinput::HostInputMode::chinese},
    };
    request.payload = piinput::encode_host_key_event(event);
    const auto response = restarted.dispatch(request);
    check(response.has_value(), "first post-restart key with resume state is accepted");
    const auto reply = decode_reply(*response);
    check(reply.snapshot.raw == "wo" && !reply.snapshot.candidates.empty() &&
            reply.snapshot.candidates.front().text == "我",
        "new Host restores confirmed raw input before applying the current key");
    std::filesystem::remove(path);
}

void test_toolbar_symbol_action_targets_the_exact_client_session() {
    const auto symbol_path =
        std::filesystem::temp_directory_path() / "piinput-session-toolbar-symbols.tsv";
    {
        std::ofstream output(symbol_path, std::ios::binary | std::ios::trunc);
        output << "℃\t摄氏度\t单位\t100\n";
    }
    piinput::Engine engine;
    piinput::SymbolIndex symbols;
    symbols.load_tsv(symbol_path);
    piinput::windows::SessionManager manager(
        engine, nullptr, &symbols, piinput::default_settings(), "full");

    check(manager.dispatch(key_envelope(1U, 10U, 1U, 'a')).has_value(),
        "first toolbar target session exists");
    check(manager.dispatch(key_envelope(2U, 10U, 1U, 'b')).has_value(),
        "same numeric session id in another client remains distinct");
    const auto opened = manager.open_symbol_center(2U, 10U);
    check(opened.has_value() && opened->snapshot.raw == ";;f" &&
            !opened->snapshot.candidates.empty() &&
            opened->snapshot.candidates.front().text == "℃",
        "toolbar action opens symbols in the exact client/session pair");
    const auto untouched = manager.snapshot(1U, 10U);
    check(untouched.has_value() && untouched->raw == "a",
        "toolbar action does not mutate another client with the same session id");
    check(!manager.open_symbol_center(9U, 99U).has_value(),
        "toolbar action rejects a session that has never been created");
    std::filesystem::remove(symbol_path);
}

void test_toolbar_expand_action_reuses_equal_key_row_navigation() {
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-session-toolbar-expand.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "阿\ta\t9000\n啊\ta\t8000\n呵\ta\t7000\n吖\ta\t6000\n"
               << "腌\ta\t5000\n锕\ta\t4000\n嗄\ta\t3000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");

    const auto first = manager.dispatch(key_envelope(3U, 30U, 1U, 'a'));
    check(first.has_value() && decode_reply(*first).snapshot.view.visible_rows == 1U,
        "toolbar target starts with one collapsed candidate row");
    const auto expanded = manager.expand_candidates(3U, 30U);
    check(expanded.has_value() && expanded->accepted &&
            expanded->snapshot.view.visible_rows >= 2U &&
            expanded->snapshot.view.active_row == 1U,
        "toolbar chevron expands candidates through the shared next-row behavior");
    check(!manager.expand_candidates(9U, 99U).has_value(),
        "toolbar chevron rejects an unknown client/session pair");
    std::filesystem::remove(path);
}

void test_candidate_popup_dismiss_cancels_the_exact_composition() {
    piinput::Engine engine;
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");

    check(manager.dispatch(key_envelope(7U, 70U, 1U, 'w')).has_value(),
        "candidate-popup target session exists");
    check(manager.dispatch(key_envelope(8U, 70U, 1U, 'n')).has_value(),
        "another client session exists");

    const auto cancelled = manager.cancel_composition(7U, 70U);
    check(cancelled.has_value() && cancelled->accepted &&
            cancelled->action == piinput::HostAction::cancel &&
            cancelled->snapshot.raw.empty(),
        "dismissing a candidate popup cancels and clears its exact composition");
    check(manager.snapshot(8U, 70U)->raw == "n",
        "popup dismissal cannot cancel another client with the same session id");
    check(!manager.cancel_composition(9U, 99U).has_value(),
        "popup dismissal rejects an unknown client/session pair");
}

void test_runtime_settings_recreate_only_idle_sessions() {
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-session-manager-runtime-settings.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n我\two\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "flypy");

    check(manager.dispatch(key_envelope(1U, 10U, 1U, 'w')).has_value(),
        "active session exists before settings reload");
    auto updated = piinput::default_settings();
    updated.general.schema = piinput::InputSchema::full;
    updated.general.default_language = piinput::DefaultInputLanguage::english;
    manager.update_settings(updated, "full");
    check(manager.snapshot(1U, 10U)->raw == "w" &&
            manager.snapshot(1U, 10U)->mode == piinput::HostInputMode::chinese,
        "an active composition keeps its original schema and mode");

    const auto idle = manager.dispatch(key_envelope(2U, 20U, 1U, 'x'));
    check(idle.has_value() && decode_reply(*idle).action == piinput::HostAction::commit &&
            decode_reply(*idle).text == "x",
        "a new session uses the reloaded default English language");
    std::filesystem::remove(path);
}

void test_runtime_settings_keep_idle_session_generations_monotonic() {
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-session-manager-settings-generation.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n明天\tming'tian\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "flypy");

    const auto typed = manager.dispatch(key_envelope(3U, 30U, 1U, 'm'));
    check(typed.has_value(), "session exists before schema hot reload");
    auto escape = key_envelope(3U, 30U, 2U, '\0');
    escape.payload = piinput::encode_host_key_event({
        .kind = piinput::HostKeyKind::escape,
    });
    const auto cancelled = manager.dispatch(escape);
    check(cancelled.has_value() && decode_reply(*cancelled).snapshot.raw.empty(),
        "session reaches an idle composition boundary before schema hot reload");
    const std::uint64_t generation_before_reload =
        decode_reply(*cancelled).snapshot.generation;

    auto updated = piinput::default_settings();
    updated.general.schema = piinput::InputSchema::full;
    manager.update_settings(updated, "full");

    const auto next_key = manager.dispatch(key_envelope(3U, 30U, 3U, 'm'));
    check(next_key.has_value() &&
            decode_reply(*next_key).snapshot.generation > generation_before_reload,
        "schema hot reload never rolls a live shim session generation backwards");
    std::filesystem::remove(path);
}

void test_runtime_schema_switch_accepts_every_key_of_a_full_pinyin_word() {
    const auto path = std::filesystem::temp_directory_path() /
        "piinput-session-manager-full-pinyin-after-settings.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n感觉\tgan'jue\t5000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "flypy");

    const auto initial = manager.dispatch(key_envelope(4U, 40U, 1U, 'm'));
    check(initial.has_value(), "hot-switch target session exists");
    auto escape = key_envelope(4U, 40U, 2U, '\0');
    escape.payload = piinput::encode_host_key_event({
        .kind = piinput::HostKeyKind::escape,
    });
    check(manager.dispatch(escape).has_value(),
        "hot-switch target reaches an idle boundary");

    auto updated = piinput::default_settings();
    updated.general.schema = piinput::InputSchema::full;
    manager.update_settings(updated, "full");

    const std::string input = "ganjue";
    std::optional<piinput::HostEnvelope> reply;
    for (std::size_t index = 0U; index < input.size(); ++index) {
        reply = manager.dispatch(key_envelope(
            4U, 40U, 3U + static_cast<std::uint64_t>(index), input[index]));
        check(reply.has_value(), "every full-pinyin key receives a Host reply");
    }
    const auto decoded = decode_reply(*reply);
    check(decoded.snapshot.raw == input,
        "schema hot reload preserves every key in a continuous full-pinyin word");
    check(!decoded.snapshot.candidates.empty() &&
            decoded.snapshot.candidates.front().text == "感觉",
        "continuous full-pinyin input resolves the complete word after hot reload");
    std::filesystem::remove(path);
}

[[nodiscard]] piinput::Engine& shared_engine() {
    static piinput::Engine engine = [] {
        const auto path =
            std::filesystem::temp_directory_path() / "piinput-session-eviction.tsv";
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n我\two\t5000\n";
        output.close();
        piinput::Engine built;
        built.load_lexicon(path);
        return built;
    }();
    return engine;
}

// Every focus change in every application binds a context and creates a session.
// The Host runs from login, so without a ceiling these accumulate for the whole
// session, each holding its own decoder state.
void test_sessions_do_not_accumulate_without_bound() {
    auto& engine = shared_engine();
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");

    for (std::uint64_t index = 1U; index <= 400U; ++index) {
        (void)manager.dispatch(key_envelope(index, 1U, 1U, 'w'));
    }
    check(manager.session_count() <= 64U,
        "idle sessions are bounded instead of kept for the life of the Host");
    check(manager.session_count() > 0U, "eviction does not empty the table");
}

// The session being typed into stays alive because it keeps being used. This is
// the property that matters: eviction is least-recently-used, so the active
// session is always the last candidate for removal, never the first.
void test_eviction_spares_the_session_being_typed_into() {
    auto& engine = shared_engine();
    piinput::windows::SessionManager manager(
        engine, nullptr, piinput::default_settings(), "full");

    constexpr std::uint64_t typing_client = 7U;
    (void)manager.dispatch(key_envelope(typing_client, 1U, 1U, 'w'));

    // Other windows come and go while the user keeps typing in theirs.
    std::uint64_t sequence = 2U;
    for (std::uint64_t index = 100U; index <= 500U; ++index) {
        (void)manager.dispatch(key_envelope(index, 1U, 1U, 'w'));
        if (index % 10U == 0U) {
            const auto reply = manager.dispatch(
                key_envelope(typing_client, 1U, sequence++, 'o'));
            check(reply.has_value(), "正在输入的会话应始终可用");
        }
    }
    const auto final_reply = manager.dispatch(
        key_envelope(typing_client, 1U, sequence, 'o'));
    check(final_reply.has_value(), "正在输入的会话不应被回收");
    check(!decode_reply(*final_reply).snapshot.raw.empty(),
        "正在输入的会话应保留已输入内容");
}

}  // namespace

int main() {
    test_manager_isolates_sessions_and_rejects_nonmonotonic_sequences();
    test_resume_recomputes_state_in_a_new_managed_session();
    test_first_key_after_host_restart_restores_confirmed_composition();
    test_toolbar_symbol_action_targets_the_exact_client_session();
    test_toolbar_expand_action_reuses_equal_key_row_navigation();
    test_candidate_popup_dismiss_cancels_the_exact_composition();
    test_runtime_settings_recreate_only_idle_sessions();
    test_runtime_settings_keep_idle_session_generations_monotonic();
    test_runtime_schema_switch_accepts_every_key_of_a_full_pinyin_word();
    test_sessions_do_not_accumulate_without_bound();
    test_eviction_spares_the_session_being_typed_into();
    std::cout << "PiInput session manager tests passed.\n";
    return 0;
}
