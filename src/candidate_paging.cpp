#include "liteime/candidate_paging.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace liteime {
namespace {

[[nodiscard]] std::size_t parse_page_size(const std::string& value, const std::size_t fallback) {
    try {
        const auto parsed = static_cast<std::size_t>(std::stoul(value));
        return parsed >= 1U && parsed <= 9U ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

}  // namespace

CandidatePageSettings load_candidate_page_settings(const std::filesystem::path& path) {
    CandidatePageSettings settings;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0U, separator);
        const std::string value = line.substr(separator + 1U);
        if (key == "single_syllable_page_size") {
            settings.single_syllable = parse_page_size(value, settings.single_syllable);
        } else if (key == "phrase_page_size") {
            settings.multi_syllable = parse_page_size(value, settings.multi_syllable);
        }
    }
    return settings;
}

std::size_t candidate_page_size(
    const CandidatePageSettings& settings,
    const std::size_t syllable_count,
    const bool symbol_mode) noexcept {
    return symbol_mode || syllable_count > 1U ? settings.multi_syllable : settings.single_syllable;
}

std::size_t move_candidate_page(
    const std::size_t page_start,
    const std::size_t candidate_count,
    const std::size_t page_size,
    const int delta) noexcept {
    if (candidate_count == 0U || page_size == 0U) {
        return 0U;
    }
    const std::size_t page_count = (candidate_count + page_size - 1U) / page_size;
    const auto current = static_cast<long long>((std::min)(page_start / page_size, page_count - 1U));
    const auto count = static_cast<long long>(page_count);
    const auto next = ((current + static_cast<long long>(delta)) % count + count) % count;
    return static_cast<std::size_t>(next) * page_size;
}

std::size_t align_candidate_page(
    const std::size_t page_start,
    const std::size_t candidate_count,
    const std::size_t page_size) noexcept {
    if (candidate_count == 0U || page_size == 0U) {
        return 0U;
    }
    return ((std::min)(page_start, candidate_count - 1U) / page_size) * page_size;
}

}  // namespace liteime
