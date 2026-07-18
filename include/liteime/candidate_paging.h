#pragma once

#include <cstddef>
#include <filesystem>

namespace liteime {

struct CandidatePageSettings {
    std::size_t single_syllable{9U};
    std::size_t multi_syllable{6U};
};

[[nodiscard]] CandidatePageSettings load_candidate_page_settings(
    const std::filesystem::path& path);

[[nodiscard]] std::size_t candidate_page_size(
    const CandidatePageSettings& settings,
    std::size_t syllable_count,
    bool symbol_mode) noexcept;

[[nodiscard]] std::size_t move_candidate_page(
    std::size_t page_start,
    std::size_t candidate_count,
    std::size_t page_size,
    int delta) noexcept;

[[nodiscard]] std::size_t align_candidate_page(
    std::size_t page_start,
    std::size_t candidate_count,
    std::size_t page_size) noexcept;

}  // namespace liteime
