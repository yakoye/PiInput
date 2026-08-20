#include "pipe_server.h"

#include "piinput/host_messages.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] double percentile_us(std::vector<std::uint64_t> samples, const double ratio) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        ratio * static_cast<double>(samples.size() - 1U));
    return static_cast<double>(samples[index]);
}

struct TypedClause final {
    std::string code;    // Xiaohe letters for this chunk
    std::string target;  // the text the user actually wants
    std::string punct;   // trailing punctuation key, or "-" for none
    bool shifted{};
};

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    for (;;) {
        const auto tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1U;
    }
    return fields;
}

[[nodiscard]] std::vector<TypedClause> load_type_script(const std::string& path) {
    std::vector<TypedClause> clauses;
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tabs(line);
        if (fields.size() < 4U) continue;
        clauses.push_back(TypedClause{
            fields[0], fields[1], fields[2], fields[3] == "1"});
    }
    return clauses;
}

}  // namespace

int main(const int argc, char** const argv) {
    const bool transport_burst_mode = argc == 3 &&
        std::string_view(argv[1]) == "--transport-burst";
    if (transport_burst_mode) {
        const int count = std::stoi(argv[2]);
        if (count < 8 || count > 1000) return 16;
        std::vector<std::uint64_t> elapsed_us;
        elapsed_us.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const auto started = std::chrono::steady_clock::now();
            const auto response = piinput::windows::request_host(
                piinput::HostMessageType::health);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (!response.has_value()) return 17;
            elapsed_us.push_back(static_cast<std::uint64_t>((std::max)(
                elapsed, std::int64_t{0})));
        }
        std::sort(elapsed_us.begin(), elapsed_us.end());
        const std::size_t p95_index = ((elapsed_us.size() - 1U) * 95U) / 100U;
        std::cout << "transport_requests=" << elapsed_us.size() << '\n'
                  << "transport_p95_us=" << elapsed_us[p95_index] << '\n'
                  << "transport_max_us=" << elapsed_us.back() << '\n';
        return 0;
    }
    // Types an input, presses '=' the requested number of times and dumps the
    // resulting candidate page. Used to inspect what row paging actually shows.
    const bool expand_mode = argc == 4 && std::string_view(argv[1]) == "--expand";
    if (expand_mode) {
        const std::string input = argv[2];
        const int presses = std::stoi(argv[3]);
        const std::uint64_t client_id = static_cast<std::uint64_t>(GetCurrentProcessId());
        std::uint64_t sequence = 1U;
        std::uint64_t generation = 0U;
        std::optional<piinput::HostReply> reply;
        const auto send_key = [&](const piinput::HostKeyEvent& event) {
            const auto response = piinput::windows::request_host(
                piinput::HostMessageType::key_event,
                piinput::encode_host_key_event(event), client_id, 1U,
                sequence++, generation);
            if (!response.has_value()) return false;
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            reply = piinput::decode_host_reply(
                response->payload, error, response->version);
            if (reply.has_value()) generation = reply->snapshot.generation;
            return reply.has_value();
        };
        for (const char character : input) {
            if (!send_key({.kind = piinput::HostKeyKind::text, .character = character})) {
                return 31;
            }
        }
        for (int index = 0; index < presses; ++index) {
            if (!send_key({.kind = piinput::HostKeyKind::expand_next_row})) return 32;
        }
        if (!reply.has_value()) return 33;
        const std::size_t per_row =
            (std::max)(reply->snapshot.view.items_per_row, std::size_t{1U});
        std::cout << "input=" << input << '\n'
                  << "presses=" << presses << '\n'
                  << "mode=" << (reply->snapshot.view.mode ==
                          piinput::HostCandidateMode::segment_selection
                      ? "segment" : "normal") << '\n'
                  << "items_per_row=" << per_row << '\n'
                  << "active_row=" << reply->snapshot.view.active_row << '\n'
                  << "active_column=" << reply->snapshot.view.active_column << '\n'
                  << "first_visible_row=" << reply->snapshot.view.first_visible_row << '\n'
                  << "visible_rows=" << reply->snapshot.view.visible_rows << '\n';
        for (std::size_t index = 0U; index < reply->snapshot.candidates.size(); ++index) {
            if (index % per_row == 0U) {
                std::cout << (index == 0U ? "" : "\n") << "row" << (index / per_row) << ":";
            }
            const auto& text = reply->snapshot.candidates[index].text;
            std::cout << ' ' << (text.empty() ? "<pad>" : text);
        }
        std::cout << '\n';
        return 0;
    }
    // Shift into English and then type continuously. Every English letter is a
    // commit rather than a composition update, so this is the path users reach
    // by pressing Shift and it must not be slower than Chinese typing.
    const bool english_burst_mode = argc == 3 &&
        std::string_view(argv[1]) == "--english-burst";
    if (english_burst_mode) {
        const int count = std::stoi(argv[2]);
        if (count < 8 || count > 4000) return 28;
        const std::uint64_t client_id = static_cast<std::uint64_t>(GetCurrentProcessId());
        constexpr std::uint64_t session_id = 1U;
        std::uint64_t sequence = 1U;
        std::uint64_t generation = 0U;
        std::vector<std::uint64_t> elapsed_us;
        std::string committed;
        const auto send_key = [&](const piinput::HostKeyEvent& event, const bool measured)
            -> std::optional<piinput::HostReply> {
            const auto started = std::chrono::steady_clock::now();
            const auto response = piinput::windows::request_host(
                piinput::HostMessageType::key_event,
                piinput::encode_host_key_event(event), client_id, session_id,
                sequence++, generation);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (measured) {
                elapsed_us.push_back(static_cast<std::uint64_t>(
                    (std::max)(elapsed, std::int64_t{0})));
            }
            if (!response.has_value()) return std::nullopt;
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            auto reply = piinput::decode_host_reply(
                response->payload, error, response->version);
            if (reply.has_value()) generation = reply->snapshot.generation;
            return reply;
        };
        const auto switched = send_key({.kind = piinput::HostKeyKind::switch_to_english}, false);
        if (!switched.has_value() ||
            switched->snapshot.mode != piinput::HostInputMode::english) {
            return 29;
        }
        constexpr std::string_view sample = "the quick brown fox jumps over a lazy dog ";
        std::size_t commits = 0U;
        std::size_t updates = 0U;
        for (int index = 0; index < count; ++index) {
            const char character = sample[static_cast<std::size_t>(index) % sample.size()];
            const auto reply = send_key({
                .kind = character == ' '
                    ? piinput::HostKeyKind::space
                    : piinput::HostKeyKind::text,
                .character = character,
            }, true);
            if (!reply.has_value()) return 30;
            if (reply->action == piinput::HostAction::commit) {
                committed += reply->text;
                ++commits;
            } else if (reply->action == piinput::HostAction::update) {
                ++updates;
            }
        }
        std::sort(elapsed_us.begin(), elapsed_us.end());
        const std::size_t p95_index = ((elapsed_us.size() - 1U) * 95U) / 100U;
        std::cout << "english_keys=" << elapsed_us.size() << '\n'
                  << "english_commits=" << commits << '\n'
                  << "english_updates=" << updates << '\n'
                  << "english_p50_us=" << elapsed_us[elapsed_us.size() / 2U] << '\n'
                  << "english_p95_us=" << elapsed_us[p95_index] << '\n'
                  << "english_max_us=" << elapsed_us.back() << '\n'
                  << "english_committed_bytes=" << committed.size() << '\n';
        return 0;
    }
    // Replays a whole prepared corpus through the resident Host one physical
    // key at a time, exactly as the stable Shim would: letters, then the
    // trailing space or punctuation that commits the clause, then the commit
    // confirmation. Reports the per-key distribution and, separately, the first
    // key after each commit -- the case users feel as post-punctuation lag.
    const bool type_script_mode = argc == 3 &&
        std::string_view(argv[1]) == "--type-script";
    if (type_script_mode) {
        const auto clauses = load_type_script(argv[2]);
        if (clauses.empty()) {
            std::cerr << "type script is empty: " << argv[2] << '\n';
            return 24;
        }
        const std::uint64_t client_id = static_cast<std::uint64_t>(GetCurrentProcessId());
        constexpr std::uint64_t session_id = 1U;
        std::uint64_t sequence = 1U;
        std::uint64_t generation = 0U;
        std::vector<std::uint64_t> key_us;
        std::vector<std::uint64_t> boundary_us;
        std::string committed;
        std::size_t commits = 0U;
        bool next_key_is_boundary = true;

        const auto send = [&](const piinput::HostMessageType type,
                              const std::vector<std::byte>& payload,
                              const std::uint64_t request_generation,
                              const bool measured)
            -> std::optional<piinput::HostEnvelope> {
            const auto started = std::chrono::steady_clock::now();
            auto response = piinput::windows::request_host(
                type, payload, client_id, session_id, sequence++, request_generation);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (measured) {
                const auto value = static_cast<std::uint64_t>(
                    (std::max)(elapsed, std::int64_t{0}));
                key_us.push_back(value);
                if (next_key_is_boundary) boundary_us.push_back(value);
            }
            return response;
        };
        const auto send_key = [&](const piinput::HostKeyEvent& event)
            -> std::optional<piinput::HostReply> {
            const auto response = send(
                piinput::HostMessageType::key_event,
                piinput::encode_host_key_event(event), generation, true);
            next_key_is_boundary = false;
            if (!response.has_value()) return std::nullopt;
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            auto reply = piinput::decode_host_reply(
                response->payload, error, response->version);
            if (reply.has_value()) generation = reply->snapshot.generation;
            return reply;
        };
        // The stable Shim reports every successful edit and never waits on the
        // answer: the Host only replies when the generation carried something
        // to learn, so a silent commit_result is normal, not a failure.
        std::size_t learned_commits = 0U;
        const auto confirm = [&](const std::uint64_t commit_generation) {
            const piinput::HostCommitResult result{
                .generation = commit_generation,
                .succeeded = true,
            };
            const auto response = send(
                piinput::HostMessageType::commit_result,
                piinput::encode_host_commit_result(result), commit_generation, false);
            if (response.has_value()) ++learned_commits;
        };

        std::size_t at_first = 0U;
        std::size_t on_first_row = 0U;
        std::size_t needed_rows = 0U;
        std::size_t not_found = 0U;
        std::size_t wrong_commit = 0U;
        std::size_t selection_keys = 0U;

        for (const auto& clause : clauses) {
            std::optional<piinput::HostReply> reply;
            for (const char character : clause.code) {
                reply = send_key({
                    .kind = piinput::HostKeyKind::text,
                    .character = character,
                });
                if (!reply.has_value()) {
                    std::cerr << "key dispatch failed inside clause: " << clause.code << '\n';
                    return 25;
                }
            }
            if (!reply.has_value()) continue;

            // Real typing rarely stops at candidate 1. Find the wanted text in
            // the live candidate page, walk down whole rows with '=' when it is
            // not on the active row, and then press its digit -- the same keys a
            // user presses.
            const auto locate = [](const piinput::HostReply& current,
                                   const std::string& target) -> std::size_t {
                for (std::size_t index = 0U; index < current.snapshot.candidates.size();
                     ++index) {
                    if (current.snapshot.candidates[index].text == target) return index;
                }
                return (std::numeric_limits<std::size_t>::max)();
            };
            std::size_t index = locate(*reply, clause.target);
            const std::size_t row_size =
                (std::max)(reply->snapshot.view.items_per_row, std::size_t{1U});
            if (index == (std::numeric_limits<std::size_t>::max)()) {
                ++not_found;
            } else if (index == 0U) {
                ++at_first;
            } else if (index < row_size) {
                ++on_first_row;
            } else {
                ++needed_rows;
            }

            bool selected = false;
            if (index != (std::numeric_limits<std::size_t>::max)()) {
                std::size_t wanted_row = index / row_size;
                bool reachable = true;
                while (reply->snapshot.view.active_row < wanted_row) {
                    const std::size_t before = reply->snapshot.view.active_row;
                    reply = send_key({.kind = piinput::HostKeyKind::expand_next_row});
                    ++selection_keys;
                    if (!reply.has_value() ||
                        reply->snapshot.view.mode !=
                            piinput::HostCandidateMode::normal ||
                        reply->snapshot.view.active_row == before) {
                        reachable = false;
                        break;
                    }
                    // Row navigation can re-lay out the page; re-locate.
                    const std::size_t moved = locate(*reply, clause.target);
                    if (moved == (std::numeric_limits<std::size_t>::max)()) {
                        reachable = false;
                        break;
                    }
                    index = moved;
                    wanted_row = index /
                        (std::max)(reply->snapshot.view.items_per_row, std::size_t{1U});
                }
                if (reachable && reply.has_value() &&
                    reply->snapshot.view.active_row == wanted_row) {
                    const std::size_t column = index %
                        (std::max)(reply->snapshot.view.items_per_row, std::size_t{1U});
                    if (column < 9U) {
                        reply = send_key({
                            .kind = piinput::HostKeyKind::select_digit,
                            .character = static_cast<char>('1' + column),
                        });
                        ++selection_keys;
                        if (reply.has_value() &&
                            reply->action == piinput::HostAction::commit) {
                            committed += reply->text;
                            ++commits;
                            if (reply->text != clause.target) ++wrong_commit;
                            confirm(reply->snapshot.generation);
                            next_key_is_boundary = true;
                            selected = true;
                        }
                    }
                }
            }
            if (!selected) {
                // Fall back to committing the active candidate, which is what a
                // user does when the wanted word is not offered at all.
                reply = send_key({.kind = piinput::HostKeyKind::space});
                if (reply.has_value() && reply->action == piinput::HostAction::commit) {
                    committed += reply->text;
                    ++commits;
                    if (reply->text != clause.target) ++wrong_commit;
                    confirm(reply->snapshot.generation);
                    next_key_is_boundary = true;
                }
            }

            if (clause.punct != "-" && !clause.punct.empty()) {
                reply = send_key({
                    .kind = piinput::HostKeyKind::punctuation,
                    .character = clause.punct.front(),
                    .shifted = clause.shifted,
                });
                if (!reply.has_value()) {
                    std::cerr << "punctuation key failed after: " << clause.code << '\n';
                    return 26;
                }
                if (reply->action == piinput::HostAction::commit) {
                    committed += reply->text;
                    ++commits;
                    confirm(reply->snapshot.generation);
                    next_key_is_boundary = true;
                }
            }
        }

        std::cout << "clauses=" << clauses.size() << '\n'
                  << "keys=" << key_us.size() << '\n'
                  << "selection_keys=" << selection_keys << '\n'
                  << "commits=" << commits << '\n'
                  << "boundary_keys=" << boundary_us.size() << '\n'
                  << "key_p50_us=" << percentile_us(key_us, 0.50) << '\n'
                  << "key_p95_us=" << percentile_us(key_us, 0.95) << '\n'
                  << "key_p99_us=" << percentile_us(key_us, 0.99) << '\n'
                  << "key_max_us="
                  << (key_us.empty() ? 0U : *std::max_element(key_us.begin(), key_us.end()))
                  << '\n'
                  << "boundary_p50_us=" << percentile_us(boundary_us, 0.50) << '\n'
                  << "boundary_p95_us=" << percentile_us(boundary_us, 0.95) << '\n'
                  << "boundary_max_us="
                  << (boundary_us.empty()
                          ? 0U
                          : *std::max_element(boundary_us.begin(), boundary_us.end()))
                  << '\n'
                  << "target_at_first=" << at_first << '\n'
                  << "target_on_first_row=" << on_first_row << '\n'
                  << "target_needed_row_paging=" << needed_rows << '\n'
                  << "target_not_in_page=" << not_found << '\n'
                  << "wrong_commit=" << wrong_commit << '\n'
                  << "committed_bytes=" << committed.size() << '\n'
                  << "committed=" << committed << '\n';
        return 0;
    }
    const bool punctuation_chain_mode = argc == 2 &&
        std::string_view(argv[1]) == "--punctuation-chain";
    if (punctuation_chain_mode) {
        const std::uint64_t client_id = static_cast<std::uint64_t>(GetCurrentProcessId());
        constexpr std::uint64_t session_id = 1U;
        std::uint64_t sequence = 1U;
        std::uint64_t generation = 0U;
        std::vector<std::uint64_t> elapsed_us;
        std::string committed;

        const auto request = [&](const piinput::HostMessageType type,
                                 const std::vector<std::byte>& payload,
                                 const std::uint64_t request_generation)
            -> std::optional<piinput::HostEnvelope> {
            const auto started = std::chrono::steady_clock::now();
            auto response = piinput::windows::request_host(
                type, payload, client_id, session_id, sequence++, request_generation);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            elapsed_us.push_back(static_cast<std::uint64_t>((std::max)(
                elapsed, std::int64_t{0})));
            return response;
        };
        const auto send_key = [&](const piinput::HostKeyEvent& event)
            -> std::optional<piinput::HostReply> {
            const auto response = request(
                piinput::HostMessageType::key_event,
                piinput::encode_host_key_event(event), generation);
            if (!response.has_value()) return std::nullopt;
            piinput::HostPayloadError error = piinput::HostPayloadError::none;
            auto reply = piinput::decode_host_reply(
                response->payload, error, response->version);
            if (reply.has_value()) generation = reply->snapshot.generation;
            return reply;
        };
        const auto confirm = [&](const std::uint64_t commit_generation) {
            const piinput::HostCommitResult result{
                .generation = commit_generation,
                .succeeded = true,
            };
            const auto response = request(
                piinput::HostMessageType::commit_result,
                piinput::encode_host_commit_result(result), commit_generation);
            return response.has_value() &&
                response->type == piinput::HostMessageType::commit_result;
        };
        const auto type_text = [&](const std::string_view text) {
            for (const char character : text) {
                if (!send_key({
                        .kind = piinput::HostKeyKind::text,
                        .character = character,
                    }).has_value()) {
                    return false;
                }
            }
            return true;
        };
        if (!type_text("wo")) return 18;
        auto reply = send_key({
            .kind = piinput::HostKeyKind::punctuation,
            .character = ',',
        });
        if (!reply.has_value() || reply->action != piinput::HostAction::commit ||
            reply->text != "我，") {
            return 19;
        }
        committed += reply->text;
        if (!confirm(reply->snapshot.generation)) return 20;

        if (!type_text("tamf")) return 21;
        reply = send_key({
            .kind = piinput::HostKeyKind::punctuation,
            .character = '.',
        });
        if (!reply.has_value() || reply->action != piinput::HostAction::commit ||
            reply->text != "他们。") {
            return 22;
        }
        committed += reply->text;
        if (!confirm(reply->snapshot.generation)) return 23;

        std::sort(elapsed_us.begin(), elapsed_us.end());
        const std::size_t p95_index = ((elapsed_us.size() - 1U) * 95U) / 100U;
        std::cout << "punctuation_chain_text=" << committed << '\n'
                  << "punctuation_chain_requests=" << elapsed_us.size() << '\n'
                  << "punctuation_chain_p95_us=" << elapsed_us[p95_index] << '\n'
                  << "punctuation_chain_max_us=" << elapsed_us.back() << '\n';
        return 0;
    }
    const bool resume_mode = argc == 4 && std::string_view(argv[1]) == "--resume";
    const bool caret_mode = argc == 3 && std::string_view(argv[1]) == "--caret";
    const bool toolbar_mode = argc == 3 && std::string_view(argv[1]) == "--toolbar-responsive";
    const bool window_height_mode = argc == 3 && std::string_view(argv[1]) == "--window-height";
    const bool visual_mode = argc == 3 && std::string_view(argv[1]) == "--visual-candidate";
    if (argc != 2 && !resume_mode && !caret_mode && !toolbar_mode &&
        !window_height_mode && !visual_mode) return 2;
    const std::string resume_raw = resume_mode ? argv[2] : "";
    const std::string input = resume_mode
        ? argv[3]
        : ((caret_mode || toolbar_mode || window_height_mode || visual_mode) ? argv[2] : argv[1]);
    std::uint64_t sequence = 1U;
    piinput::HostReply last;
    bool first_key = true;
    std::uint64_t request_max_us = 0U;
    std::vector<std::uint64_t> request_elapsed_us;
    request_elapsed_us.reserve(input.size());
    for (const char character : input) {
        piinput::HostKeyEvent event{
            .kind = piinput::HostKeyKind::text,
            .character = character,
        };
        if (first_key && resume_mode) {
            event.resume = piinput::HostResumeState{
                .generation = 1U,
                .raw = resume_raw,
                .caret = resume_raw.size(),
                .mode = piinput::HostInputMode::chinese,
            };
        }
        first_key = false;
        const piinput::HostEnvelope request{
            .version = piinput::host_protocol_v1,
            .client_id = static_cast<std::uint64_t>(GetCurrentProcessId()),
            .session_id = 1U,
            .sequence = sequence++,
            .generation = last.snapshot.generation,
            .type = piinput::HostMessageType::key_event,
            .payload = piinput::encode_host_key_event(event),
        };
        const auto request_started = std::chrono::steady_clock::now();
        const auto response = piinput::windows::request_host(
            request.type, request.payload, request.client_id, request.session_id,
            request.sequence, request.generation);
        const auto request_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - request_started).count();
        const auto normalized_request_elapsed = static_cast<std::uint64_t>(
            (std::max)(request_elapsed, std::int64_t{0}));
        request_max_us = (std::max)(request_max_us, normalized_request_elapsed);
        request_elapsed_us.push_back(normalized_request_elapsed);
        if (!response.has_value()) return 3;
        piinput::HostPayloadError error = piinput::HostPayloadError::none;
        const auto reply = piinput::decode_host_reply(
            response->payload, error, response->version);
        if (!reply.has_value()) return 4;
        last = *reply;
    }
    if (caret_mode || toolbar_mode || window_height_mode || visual_mode) {
        RECT foreground_bounds{0, 0, 0, 0};
        const HWND foreground = GetForegroundWindow();
        const bool foreground_available = foreground != nullptr &&
            GetWindowRect(foreground, &foreground_bounds) != FALSE;
        const LONG caret_left = foreground_available ? foreground_bounds.left + 120 : 120;
        const LONG caret_top = foreground_available ? foreground_bounds.top + 140 : 240;
        const piinput::HostCaretUpdate update{
            .generation = last.snapshot.generation,
            .has_text_caret = true,
            .left = caret_left,
            .top = caret_top,
            .right = caret_left + 2,
            .bottom = caret_top + 24,
        };
        const auto payload = piinput::encode_host_caret_update(update);
        const auto response = piinput::windows::request_host(
            piinput::HostMessageType::caret,
            payload,
            static_cast<std::uint64_t>(GetCurrentProcessId()),
            1U,
            sequence++,
            update.generation);
        if (!response.has_value() || response->type != piinput::HostMessageType::caret ||
            !response->payload.empty()) {
            return 5;
        }
        std::cout << "caret_ack=yes\n";
    }
    if (visual_mode) {
        HWND candidate = nullptr;
        for (int attempt = 0; attempt < 20 && candidate == nullptr; ++attempt) {
            candidate = FindWindowW(L"PiInputTsfCandidateWindow", nullptr);
            if (candidate == nullptr) Sleep(10U);
        }
        if (candidate == nullptr) return 15;
        std::cout << "candidate_hwnd=" << reinterpret_cast<std::uintptr_t>(candidate) << '\n';
        std::cout.flush();
        Sleep(15000U);
    }
    if (window_height_mode) {
        HWND candidate = nullptr;
        for (int attempt = 0; attempt < 20 && candidate == nullptr; ++attempt) {
            candidate = FindWindowW(L"PiInputTsfCandidateWindow", nullptr);
            if (candidate == nullptr) Sleep(10U);
        }
        if (candidate == nullptr) return 13;
        RECT window{};
        if (GetWindowRect(candidate, &window) == FALSE) return 14;
        std::cout << "window_height=" << (window.bottom - window.top) << '\n';
    }
    if (toolbar_mode) {
        HWND candidate = nullptr;
        for (int attempt = 0; attempt < 20 && candidate == nullptr; ++attempt) {
            candidate = FindWindowW(L"PiInputTsfCandidateWindow", nullptr);
            if (candidate == nullptr) Sleep(10U);
        }
        if (candidate == nullptr) return 6;
        // Wait for the host to go back to waiting for the next connection.
        // Without this the click lands while the pipe loop is still finishing
        // the previous request and happens to pump messages right after, which
        // is not what a user does -- they look at the candidates for a moment
        // and then click, by which time the loop is blocked in
        // ConnectNamedPipe and nothing pumps the queue at all.
        Sleep(600U);
        RECT client{};
        if (GetClientRect(candidate, &client) == FALSE) return 7;
        DWORD_PTR ignored = 0U;
        const LPARAM click = MAKELPARAM(
            (std::max)(client.right - 20, 1L),
            (std::max)(client.top + 20, 1L));
        if (SendMessageTimeoutW(candidate, WM_LBUTTONUP, 0U, click,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 250U, &ignored) == 0U) {
            return 8;
        }
        if (GetClientRect(candidate, &client) == FALSE) return 9;
        const LPARAM symbols_click = MAKELPARAM(
            (std::max)(client.right - 80, 1L),
            (std::max)(client.bottom - 54, 1L));
        if (SendMessageTimeoutW(candidate, WM_LBUTTONUP, 0U, symbols_click,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 250U, &ignored) == 0U) {
            return 10;
        }
        const auto probe_payload = piinput::encode_host_key_event({
            .kind = piinput::HostKeyKind::next_candidate,
        });
        const auto probe_response = piinput::windows::request_host(
            piinput::HostMessageType::key_event,
            probe_payload,
            static_cast<std::uint64_t>(GetCurrentProcessId()),
            1U,
            sequence++,
            last.snapshot.generation);
        if (!probe_response.has_value()) return 11;
        piinput::HostPayloadError probe_error = piinput::HostPayloadError::none;
        const auto probe = piinput::decode_host_reply(
            probe_response->payload, probe_error, probe_response->version);
        if (!probe.has_value() || probe->snapshot.raw != ";;f") return 12;
        std::cout << "toolbar_ack=yes\n";
        std::cout << "toolbar_symbols=yes\n";
    }
    std::sort(request_elapsed_us.begin(), request_elapsed_us.end());
    const std::size_t request_p95_index = request_elapsed_us.empty()
        ? 0U
        : ((request_elapsed_us.size() - 1U) * 95U) / 100U;
    const std::uint64_t request_p95_us = request_elapsed_us.empty()
        ? 0U
        : request_elapsed_us[request_p95_index];
    std::cout << "raw=" << last.snapshot.raw << '\n'
              << "generation=" << last.snapshot.generation << '\n'
              << "action=" << static_cast<unsigned>(last.action) << '\n'
              << "text=" << last.text << '\n'
              << "mode=" << static_cast<unsigned>(last.snapshot.mode) << '\n'
              << "request_p95_us=" << request_p95_us << '\n'
              << "request_max_us=" << request_max_us << '\n'
              << "candidates=" << last.snapshot.candidates.size() << '\n';
    if (!last.snapshot.candidates.empty()) {
        std::cout << "first=" << last.snapshot.candidates.front().text << '\n';
    }
    return 0;
}
