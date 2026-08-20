// Per-key latency gate for the Host session layer.
//
// The reported stutter was "我，他们。我们明天，去哪里？明天、后天！打后天。",
// and it was worst on the first letter after a punctuation mark. That first
// letter is a composition boundary, so it exercises a different Host path than
// the letters in the middle of a word. This test replays the whole sentence
// against a real binary lexicon and measures every individual key, so a
// regression in either path shows up as a number rather than as a user report.
#include "piinput/host_session.h"
#include "piinput/settings.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Step final {
    piinput::HostKeyEvent event;
    bool expects_commit{};
    std::string expected_text;
};

[[nodiscard]] Step letter(const char character) {
    return Step{{.kind = piinput::HostKeyKind::text, .character = character}, false, {}};
}

[[nodiscard]] Step punctuation(const char character, const bool shifted = false) {
    return Step{
        {.kind = piinput::HostKeyKind::punctuation, .character = character,
         .shifted = shifted},
        true,
        {}};
}

[[nodiscard]] Step expect_commit(const char character, std::string expected,
    const bool shifted = false) {
    Step step = punctuation(character, shifted);
    step.expected_text = std::move(expected);
    return step;
}

void append_word(std::vector<Step>& steps, const std::string_view code) {
    for (const char character : code) steps.push_back(letter(character));
}

[[nodiscard]] std::vector<Step> reported_sentence() {
    // 我，他们。我们明天，去哪里？明天、后天！大后天。
    std::vector<Step> steps;
    append_word(steps, "wo");
    steps.push_back(expect_commit(',', "我，"));
    append_word(steps, "tamf");
    steps.push_back(expect_commit('.', "他们。"));
    append_word(steps, "womfmktm");
    steps.push_back(expect_commit(',', "我们明天，"));
    append_word(steps, "qvnali");
    steps.push_back(expect_commit('/', "去哪里？", true));
    append_word(steps, "mktm");
    steps.push_back(expect_commit('\\', "明天、"));
    append_word(steps, "hztm");
    steps.push_back(expect_commit('1', "后天！", true));
    append_word(steps, "dahztm");
    steps.push_back(expect_commit('.', "大后天。"));
    return steps;
}

[[nodiscard]] double percentile(std::vector<double> samples, const double ratio) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        ratio * static_cast<double>(samples.size() - 1U));
    return samples[index];
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc < 2) {
        std::cerr << "Usage: piinput-keystroke-latency-tests <lexicon> "
                     "[max_p95_us] [max_key_us]\n";
        return 2;
    }
    const std::filesystem::path lexicon(argv[1]);
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(lexicon, exists_error) || exists_error) {
        std::cout << "SKIP: keystroke latency lexicon not found: "
                  << lexicon.string() << '\n';
        return 77;
    }
    const double max_p95_us = argc >= 3 ? std::stod(argv[2]) : 2000.0;
    const double max_key_us = argc >= 4 ? std::stod(argv[3]) : 6000.0;

    piinput::Engine engine;
    engine.load_lexicon(lexicon);
    if (engine.entry_count() < 50'000U) {
        std::cerr << "Keystroke latency needs the real lexicon, found "
                  << engine.entry_count() << " entries\n";
        return 3;
    }

    auto settings = piinput::default_settings();
    settings.general.schema = piinput::InputSchema::flypy;
    piinput::HostSession session(engine, nullptr, nullptr, settings, "flypy");

    // Warm the bounded prefix caches exactly like the resident Host does before
    // it publishes health, so the gate measures steady typing rather than a
    // cold dictionary.
    for (char initial = 'a'; initial <= 'z'; ++initial) {
        (void)engine.query(std::string(1U, initial), "flypy",
            settings.candidates.max_items, settings);
    }

    const auto steps = reported_sentence();
    std::vector<double> all_keys;
    std::vector<double> boundary_keys;
    std::string committed;
    bool previous_was_commit = true;
    std::size_t failures = 0U;
    double worst_us = 0.0;
    std::string worst_key;

    for (const auto& step : steps) {
        const auto start = Clock::now();
        const auto reply = session.apply(step.event);
        const auto elapsed = std::chrono::duration<double, std::micro>(
            Clock::now() - start).count();
        all_keys.push_back(elapsed);
        if (previous_was_commit) boundary_keys.push_back(elapsed);
        if (elapsed > worst_us) {
            worst_us = elapsed;
            worst_key = std::string(1U, step.event.character);
        }
        if (step.expects_commit) {
            if (reply.action != piinput::HostAction::commit) {
                std::cerr << "FAIL: expected a commit for '" << step.event.character
                          << "'\n";
                ++failures;
            } else {
                committed += reply.text;
                if (!step.expected_text.empty() && reply.text != step.expected_text) {
                    std::cerr << "FAIL: expected '" << step.expected_text
                              << "' but committed '" << reply.text << "'\n";
                    ++failures;
                }
                // Confirming the commit is what the shim reports back after a
                // successful TSF edit; it must stay off the slow path too.
                (void)session.confirm_commit(reply.snapshot.generation, true);
            }
        }
        previous_was_commit = step.expects_commit;
    }

    const double p50 = percentile(all_keys, 0.50);
    const double p95 = percentile(all_keys, 0.95);
    const double boundary_p95 = percentile(boundary_keys, 0.95);
    std::cout << "sentence=" << committed << '\n'
              << "keys=" << all_keys.size() << '\n'
              << "boundary_keys=" << boundary_keys.size() << '\n'
              << "key_p50_us=" << p50 << '\n'
              << "key_p95_us=" << p95 << '\n'
              << "boundary_key_p95_us=" << boundary_p95 << '\n'
              << "key_max_us=" << worst_us << " (key '" << worst_key << "')\n";

    if (committed != "我，他们。我们明天，去哪里？明天、后天！大后天。") {
        std::cerr << "FAIL: the reported sentence did not commit as expected\n";
        ++failures;
    }
    if (p95 > max_p95_us) {
        std::cerr << "FAIL: per-key P95 " << p95 << " us exceeds " << max_p95_us
                  << " us\n";
        ++failures;
    }
    if (worst_us > max_key_us) {
        std::cerr << "FAIL: slowest key " << worst_us << " us exceeds " << max_key_us
                  << " us\n";
        ++failures;
    }
    // The first letter after punctuation is the case the user reported. It must
    // not be systematically slower than ordinary keys.
    if (boundary_p95 > max_key_us) {
        std::cerr << "FAIL: first key after punctuation P95 " << boundary_p95
                  << " us exceeds " << max_key_us << " us\n";
        ++failures;
    }
    if (failures != 0U) {
        std::cerr << failures << " keystroke latency check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PiInput keystroke latency tests passed.\n";
    return EXIT_SUCCESS;
}
