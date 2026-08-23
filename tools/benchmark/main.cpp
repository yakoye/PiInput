#include "piinput/engine.h"
#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path lexicon;
    std::string schema{"full"};
    std::string query{"jisuanji"};
    std::size_t iterations{10000U};
    std::size_t warmup{1000U};
    std::size_t rounds{1U};
    // The resident Host asks for a full candidate page, so the default here
    // matches CandidateSettings::max_items rather than a short preview list.
    std::size_t limit{90U};
    std::size_t min_entries{};
    bool skip_if_missing{};
    bool require_results{};
    double max_p95_us{};
    double max_p99_us{};
};

[[nodiscard]] std::size_t parse_size(const std::string& value, const char* name) {
    std::size_t consumed = 0U;
    const unsigned long long parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0U) {
        throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] Options parse_options(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        auto require_value = [&](const char* name) -> const std::string& {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error(std::string(name) + " requires a value");
            }
            return arguments[++index];
        };
        if (argument == "--lexicon") {
            options.lexicon = piinput::path_from_utf8(require_value("--lexicon"));
        } else if (argument == "--schema") {
            options.schema = require_value("--schema");
        } else if (argument == "--query") {
            options.query = require_value("--query");
        } else if (argument == "--iterations") {
            options.iterations = parse_size(require_value("--iterations"), "--iterations");
        } else if (argument == "--warmup") {
            options.warmup = parse_size(require_value("--warmup"), "--warmup");
        } else if (argument == "--rounds") {
            options.rounds = parse_size(require_value("--rounds"), "--rounds");
        } else if (argument == "--limit") {
            options.limit = parse_size(require_value("--limit"), "--limit");
        } else if (argument == "--skip-if-missing") {
            options.skip_if_missing = true;
        } else if (argument == "--min-entries") {
            options.min_entries = parse_size(require_value("--min-entries"), "--min-entries");
        } else if (argument == "--require-results") {
            options.require_results = true;
        } else if (argument == "--max-p95-us") {
            options.max_p95_us = std::stod(require_value("--max-p95-us"));
        } else if (argument == "--max-p99-us") {
            options.max_p99_us = std::stod(require_value("--max-p99-us"));
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "PiInput benchmark\n"
                << "  --lexicon <file.tsv|file.lex>  required\n"
                << "  --schema <full|flypy|natural|mspy|abc>\n"
                << "  --query <input>\n"
                << "  --iterations <count>\n"
                << "  --warmup <count>\n"
                << "  --rounds <count>\n"
                << "  --limit <candidates>           default 90, the Host page size\n"
                << "  --skip-if-missing\n"
                << "  --min-entries <count>\n"
                << "  --require-results\n"
                << "  --max-p95-us <microseconds>\n"
                << "  --max-p99-us <microseconds>\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }
    if (options.lexicon.empty()) {
        throw std::runtime_error("Missing --lexicon");
    }
    return options;
}

[[nodiscard]] double percentile(const std::vector<double>& sorted, const double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1U));
    return sorted[index];
}

int run(const std::vector<std::string>& arguments) {
    const Options options = parse_options(arguments);
    if (options.skip_if_missing && !std::filesystem::exists(options.lexicon)) {
        std::cout << "SKIP: external benchmark lexicon not found: "
                  << options.lexicon.string() << '\n';
        return 77;
    }
    piinput::Engine engine;

    const auto load_start = std::chrono::steady_clock::now();
    engine.load_lexicon(options.lexicon);
    const auto load_end = std::chrono::steady_clock::now();
    if (options.min_entries != 0U && engine.entry_count() < options.min_entries) {
        std::cerr << "Lexicon entry count " << engine.entry_count()
                  << " is below required minimum " << options.min_entries << ".\n";
        return 3;
    }

    std::size_t result_guard = 0U;
    std::vector<double> round_p50;
    std::vector<double> round_p95;
    std::vector<double> round_p99;
    round_p50.reserve(options.rounds);
    round_p95.reserve(options.rounds);
    round_p99.reserve(options.rounds);
    double total = 0.0;
    double maximum = 0.0;
    for (std::size_t round = 0U; round < options.rounds; ++round) {
        for (std::size_t index = 0U; index < options.warmup; ++index) {
            result_guard += engine.query(options.query, options.schema, options.limit).size();
        }
        std::vector<double> microseconds;
        microseconds.reserve(options.iterations);
        for (std::size_t index = 0U; index < options.iterations; ++index) {
            const auto start = std::chrono::steady_clock::now();
            const auto candidates = engine.query(options.query, options.schema, options.limit);
            const auto end = std::chrono::steady_clock::now();
            result_guard += candidates.size();
            const double elapsed =
                std::chrono::duration<double, std::micro>(end - start).count();
            total += elapsed;
            maximum = (std::max)(maximum, elapsed);
            microseconds.push_back(elapsed);
        }
        std::sort(microseconds.begin(), microseconds.end());
        round_p50.push_back(percentile(microseconds, 0.50));
        round_p95.push_back(percentile(microseconds, 0.95));
        round_p99.push_back(percentile(microseconds, 0.99));
        std::cout << std::fixed << std::setprecision(3)
                  << "round_" << round + 1U
                  << "_p50_us=" << round_p50.back() << '\n'
                  << "round_" << round + 1U
                  << "_p95_us=" << round_p95.back() << '\n'
                  << "round_" << round + 1U
                  << "_p99_us=" << round_p99.back() << '\n';
    }
    std::sort(round_p50.begin(), round_p50.end());
    std::sort(round_p95.begin(), round_p95.end());
    std::sort(round_p99.begin(), round_p99.end());
    const double p50 = percentile(round_p50, 0.50);
    const double p95 = percentile(round_p95, 0.50);
    const double p99 = percentile(round_p99, 0.50);
    const double average = total /
        (static_cast<double>(options.iterations) * static_cast<double>(options.rounds));
    const double load_ms = std::chrono::duration<double, std::milli>(load_end - load_start).count();
    if (options.require_results && result_guard == 0U) {
        std::cerr << "Benchmark query produced no candidates.\n";
        return 3;
    }

    std::cout << std::fixed << std::setprecision(3)
              << "PiInput benchmark\n"
              << "lexicon_entries=" << engine.entry_count() << '\n'
              << "lexicon_storage=" << (engine.lexicon_memory_mapped() ? "mmap" : "heap") << '\n'
              << "lexicon_mapped_bytes=" << engine.lexicon_mapped_bytes() << '\n'
              << "load_ms=" << load_ms << '\n'
              << "rounds=" << options.rounds << '\n'
              << "iterations=" << options.iterations << '\n'
              << "limit=" << options.limit << '\n'
              << "average_us=" << average << '\n'
              << "median_p50_us=" << p50 << '\n'
              << "median_p95_us=" << p95 << '\n'
              << "median_p99_us=" << p99 << '\n'
              << "p50_us=" << p50 << '\n'
              << "p95_us=" << p95 << '\n'
              << "p99_us=" << p99 << '\n'
              << "max_us=" << maximum << '\n'
              << "result_guard=" << result_guard << '\n';
    if ((options.max_p95_us > 0.0 && p95 > options.max_p95_us) ||
        (options.max_p99_us > 0.0 && p99 > options.max_p99_us)) {
        std::cerr << "Latency threshold exceeded.\n";
        return 2;
    }
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(piinput::wide_to_utf8(argv[index]));
        }
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(const int argc, char* argv[]) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#endif
