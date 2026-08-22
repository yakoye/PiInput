#include "piinput/engine.h"
#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CorpusCase final {
    std::string id;
    std::string mode;
    std::string schema;
    std::string input;
    std::string target;
    std::size_t max_rank{};
};

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(line.data() + start, line.size() - start);
            break;
        }
        fields.emplace_back(line.data() + start, tab - start);
        start = tab + 1U;
    }
    return fields;
}

[[nodiscard]] std::vector<CorpusCase> load_cases(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open structured corpus cases");
    std::vector<CorpusCase> cases;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line_number == 1U && line.starts_with("\xEF\xBB\xBF")) line.erase(0U, 3U);
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tabs(line);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "Invalid structured corpus row " + std::to_string(line_number));
        }
        std::size_t processed = 0U;
        const unsigned long long max_rank = std::stoull(std::string(fields[5]), &processed, 10);
        if (processed != fields[5].size()) {
            throw std::runtime_error(
                "Invalid structured corpus rank at row " + std::to_string(line_number));
        }
        cases.push_back({
            std::string(fields[0]), std::string(fields[1]), std::string(fields[2]),
            std::string(fields[3]), std::string(fields[4]),
            static_cast<std::size_t>(max_rank),
        });
    }
    return cases;
}

int run(const std::filesystem::path& lexicon_path, const std::filesystem::path& case_path) {
    piinput::Engine engine;
    engine.load_lexicon(lexicon_path);
    const auto cases = load_cases(case_path);
    if (cases.empty()) throw std::runtime_error("Structured corpus has no executable cases");

    std::size_t failures = 0U;
    std::size_t rank_cases = 0U;
    std::size_t known_missing_cases = 0U;
    std::size_t result_cases = 0U;
    std::size_t smoke_cases = 0U;
    std::uint64_t slowest_us = 0U;
    std::string slowest_id;
    for (const auto& item : cases) {
        try {
            const std::size_t limit = item.mode == "rank" || item.mode == "known_missing"
                ? (std::max)(item.max_rank, std::size_t{1U})
                : std::size_t{90U};
            const auto began = std::chrono::steady_clock::now();
            const auto candidates = engine.query(item.input, item.schema, limit);
            const auto elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - began).count());
            if (elapsed > slowest_us) {
                slowest_us = elapsed;
                slowest_id = item.id;
            }

            if (item.mode == "rank" || item.mode == "known_missing") {
                const auto found = std::find_if(
                    candidates.begin(), candidates.end(), [&](const auto& candidate) {
                        return candidate.word == item.target;
                    });
                const std::size_t rank = found == candidates.end()
                    ? 0U
                    : static_cast<std::size_t>(found - candidates.begin()) + 1U;
                if (item.mode == "rank") {
                    ++rank_cases;
                } else {
                    ++known_missing_cases;
                }
                if (item.mode == "rank" && (rank == 0U || rank > item.max_rank)) {
                    ++failures;
                    std::cerr << item.id << ": target '" << item.target
                              << "' rank=" << rank << " expected<= " << item.max_rank << '\n';
                } else if (item.mode == "known_missing" &&
                           rank != 0U && rank <= item.max_rank) {
                    ++failures;
                    std::cerr << item.id << ": known gap resolved at rank=" << rank
                              << "; remove it from the baseline\n";
                }
            } else if (item.mode == "results") {
                ++result_cases;
                if (candidates.empty()) {
                    ++failures;
                    std::cerr << item.id << ": implemented long-input path returned no candidates\n";
                }
            } else if (item.mode == "smoke") {
                ++smoke_cases;
            } else {
                ++failures;
                std::cerr << item.id << ": unknown corpus mode " << item.mode << '\n';
            }
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << item.id << ": query failed: " << error.what() << '\n';
        }
    }

    std::cout << "Structured corpus executed=" << cases.size()
              << " rank=" << rank_cases
              << " known_missing=" << known_missing_cases
              << " results=" << result_cases
              << " smoke=" << smoke_cases
              << " entries=" << engine.entry_count()
              << " slowest_us=" << slowest_us
              << " slowest_id=" << slowest_id << '\n';
    return failures == 0U ? 0 : 1;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc != 3) {
        std::cerr << "Usage: piinput-corpus-candidate-tests <lexicon> <cases.tsv>\n";
        return 2;
    }
    try {
        return run(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::cerr << "Structured corpus runner failed: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(const int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: piinput-corpus-candidate-tests <lexicon> <cases.tsv>\n";
        return 2;
    }
    try {
        return run(piinput::path_from_utf8(argv[1]), piinput::path_from_utf8(argv[2]));
    } catch (const std::exception& error) {
        std::cerr << "Structured corpus runner failed: " << error.what() << '\n';
        return 1;
    }
}
#endif
