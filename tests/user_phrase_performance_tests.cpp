#include "piinput/candidate_grid.h"
#include "piinput/settings.h"
#include "piinput/user_model.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

void check(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

double percentile(std::vector<double> samples, const double ratio) {
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        ratio * static_cast<double>(samples.size() - 1U));
    return samples[index];
}

template <typename Operation>
std::vector<double> measure(const std::size_t count, Operation operation) {
    std::vector<double> samples;
    samples.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto start = Clock::now();
        operation(index);
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    return samples;
}

void report(const std::string_view name, const std::vector<double>& samples) {
    std::cout << name
              << " samples=" << samples.size()
              << " p50_us=" << percentile(samples, 0.50)
              << " p95_us=" << percentile(samples, 0.95)
              << " p99_us=" << percentile(samples, 0.99) << '\n';
}

void test_indexed_user_phrase_latency() {
    piinput::UserModel model;
    constexpr std::size_t total_entries = 10'000U;
    for (std::size_t index = 0U; index < total_entries; ++index) {
        model.record_composed_phrase(
            "fixture'" + std::to_string(index),
            "word-" + std::to_string(index));
    }
    for (std::size_t index = 0U; index < 12U; ++index) {
        model.record_composed_phrase("shared'query", "shared-" + std::to_string(index));
    }
    check(model.entry_count() == total_entries + 12U,
        "performance fixture contains ten thousand indexed user phrases");

    std::size_t guard = 0U;
    const auto query_samples = measure(5'000U, [&](const std::size_t) {
        guard += model.query_exact("shared'query").size();
    });
    report("user_phrase_exact_query", query_samples);
    check(guard == 60'000U, "indexed query returns only the exact pinyin bucket");
    check(percentile(query_samples, 0.95) <= 200.0,
        "10k-entry user phrase exact-query P95 stays within 0.2 ms");

    const auto learning_samples = measure(2'000U, [&](const std::size_t index) {
        model.record_selection(
            "fixture'" + std::to_string(index % total_entries),
            "word-" + std::to_string(index % total_entries));
    });
    report("user_phrase_memory_learning", learning_samples);
    check(percentile(learning_samples, 0.95) <= 200.0,
        "in-memory learning P95 stays within 0.2 ms");
}

void test_candidate_grid_latency() {
    auto settings = piinput::default_settings().candidates;
    settings.items_per_row = 6U;
    settings.visible_rows = 3U;
    piinput::CandidateGrid grid(settings, 90U);
    const auto samples = measure(20'000U, [&](const std::size_t index) {
        if ((index % 28U) == 0U) grid.reset(90U);
        grid.move_row((index % 2U) == 0U ? 1 : -1);
    });
    report("candidate_row_navigation", samples);
    check(percentile(samples, 0.95) <= 100.0,
        "candidate row navigation P95 stays within 0.1 ms");
}

}  // namespace

int main() {
    try {
        test_indexed_user_phrase_latency();
        test_candidate_grid_latency();
        std::cout << "PiInput user phrase performance tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
