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
    piinput::Engine engine;

    const auto load_start = std::chrono::steady_clock::now();
    engine.load_lexicon(options.lexicon);
    const auto load_end = std::chrono::steady_clock::now();

    std::size_t result_guard = 0U;
    for (std::size_t index = 0U; index < options.warmup; ++index) {
        result_guard += engine.query(options.query, options.schema, 10U).size();
    }

    std::vector<double> microseconds;
    microseconds.reserve(options.iterations);
    for (std::size_t index = 0U; index < options.iterations; ++index) {
        const auto start = std::chrono::steady_clock::now();
        const auto candidates = engine.query(options.query, options.schema, 10U);
        const auto end = std::chrono::steady_clock::now();
        result_guard += candidates.size();
        microseconds.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }

    std::sort(microseconds.begin(), microseconds.end());
    double total = 0.0;
    for (const double value : microseconds) {
        total += value;
    }
    const double average = total / static_cast<double>(microseconds.size());
    const double load_ms = std::chrono::duration<double, std::milli>(load_end - load_start).count();

    const double p95 = percentile(microseconds, 0.95);
    const double p99 = percentile(microseconds, 0.99);
    std::cout << std::fixed << std::setprecision(3)
              << "PiInput benchmark\n"
              << "lexicon_entries=" << engine.entry_count() << '\n'
              << "load_ms=" << load_ms << '\n'
              << "iterations=" << options.iterations << '\n'
              << "average_us=" << average << '\n'
              << "p50_us=" << percentile(microseconds, 0.50) << '\n'
              << "p95_us=" << p95 << '\n'
              << "p99_us=" << p99 << '\n'
              << "max_us=" << microseconds.back() << '\n'
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
