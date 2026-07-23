#pragma once

#include "piinput/lexicon.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace piinput {

struct IncrementalParse {
    std::vector<std::string> complete_syllables;
    std::string trailing_prefix;
    std::string canonical_prefix;
    int parse_score{};
};

struct IncrementalDecodeOptions {
    std::size_t beam_width{32U};
    // Maximum total scan budget for one decode. The decoder may request less
    // after applying its per-key candidate headroom.
    std::size_t prefix_scan_limit{4096U};
    std::size_t result_limit{90U};
    std::size_t max_word_syllables{8U};
    bool incomplete_candidates{true};
};

struct IncrementalCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t base_weight{};
    std::int64_t score{};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
};

struct IncrementalDecodeStats {
    std::size_t input_parse_count{};
    std::size_t unique_parse_count{};
    std::size_t retained_parse_count{};
    std::size_t exact_query_calls{};
    std::size_t exact_unique_keys{};
    std::size_t prefix_query_calls{};
    std::size_t prefix_unique_keys{};
    std::size_t max_source_paths{};
    std::size_t max_destination_paths{};
    std::size_t submitted_candidates{};
    std::size_t result_count{};
};

class IncrementalDecoder final {
public:
    using ExactQuery = std::function<std::vector<LexiconCandidate>(std::string_view, std::size_t)>;
    using PrefixQuery = std::function<std::vector<LexiconCandidate>(
        std::string_view, std::size_t, std::size_t)>;
    using UserScore = std::function<int(std::string_view, std::string_view)>;

    IncrementalDecoder(ExactQuery exact_query, PrefixQuery prefix_query, UserScore user_score);

    [[nodiscard]] std::vector<IncrementalCandidate> decode(
        const std::vector<IncrementalParse>& parses,
        const IncrementalDecodeOptions& options = {},
        IncrementalDecodeStats* stats = nullptr) const;

private:
    ExactQuery exact_query_;
    PrefixQuery prefix_query_;
    UserScore user_score_;
};

}  // namespace piinput
