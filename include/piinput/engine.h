#pragma once

#include "piinput/candidate_evidence.h"
#include "piinput/binary_lexicon.h"
#include "piinput/lexicon.h"
#include "piinput/pinyin.h"
#include "piinput/settings.h"
#include "piinput/segment_selection.h"
#include "piinput/shuangpin.h"
#include "piinput/user_model.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace piinput {

struct EngineCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t base_weight{};
    std::int64_t score{};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
    CandidateEvidence evidence;
};

class Engine final {
public:
    Engine();
    Engine(const Engine& other);
    Engine& operator=(const Engine& other);
    Engine(Engine&& other) noexcept = default;
    Engine& operator=(Engine&& other) noexcept = default;

    void load_lexicon(const std::filesystem::path& path);
    void load_user_model(const std::filesystem::path& path);
    void save_user_model(const std::filesystem::path& path) const;
    void record_selection(const std::string& pinyin, const std::string& word);
    void record_composed_phrase(const std::string& pinyin, const std::string& word);
    void pin_candidate(const std::string& pinyin, const std::string& word);
    void unpin_candidate(const std::string& pinyin, const std::string& word);
    void remove_candidate_learning(const std::string& pinyin, const std::string& word);
    void suppress_candidate(const std::string& pinyin, const std::string& word);

    // Invalid or disabled full-pinyin spellings return an empty result; input
    // errors do not escape this Engine hot path. Direct PinyinSegmenter calls
    // retain their lower-level std::invalid_argument contract.
    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 16U) const;

    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const PinyinSettings& settings) const;

    // Invalid or disabled full-pinyin spellings likewise produce no candidates
    // without throwing an input-validation exception.
    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const PinyinSettings& settings) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const SettingsSnapshot& settings) const;

    [[nodiscard]] std::vector<LexiconCandidate> lookup_word(
        std::string_view word,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<LexiconCandidate> lookup_pinyin(
        std::string_view pinyin,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<EngineCandidate> query_segment(
        const ParsedComposition& composition,
        std::size_t syllable_offset,
        std::size_t limit = 30U) const;

    [[nodiscard]] std::optional<ParsedComposition> parse_composition(
        const std::string& input,
        const std::string& schema,
        const PinyinSettings& settings = {}) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const ShuangpinDecoder& shuangpin() const noexcept;

private:
    struct PrefixQueryCache {
        std::shared_mutex state_mutex;
        std::shared_mutex entries_mutex;
        std::unordered_map<std::string, std::vector<LexiconCandidate>> entries;
        std::atomic<std::size_t> lexicon_entry_count{};
    };

    [[nodiscard]] std::vector<LexiconCandidate> query_exact_unlocked(
        const std::string& pinyin,
        std::size_t limit) const;
    [[nodiscard]] std::vector<LexiconCandidate> query_prefix_unlocked(
        const std::string& pinyin_prefix,
        std::size_t limit,
        std::size_t scan_limit,
        std::size_t max_syllables) const;
    // Candidates that cover everything typed so far by joining real dictionary
    // words. A real multi-character word must anchor the join, so this can
    // extend a word but cannot assemble a sentence out of characters. Passing a
    // trailing prefix lets the last link finish the syllable still being typed,
    // which is how a half-typed phrase keeps showing the whole thing.
    [[nodiscard]] std::vector<EngineCandidate> compose_full_coverage_unlocked(
        const std::vector<std::string>& syllables,
        std::size_t limit,
        const std::string& trailing_prefix = {},
        const std::string& canonical_prefix = {},
        std::size_t scan_limit = 0U) const;
    // Entries whose per-syllable initials match a simplified spelling of the
    // input, with any syllable the user spelled out matching exactly.
    [[nodiscard]] std::vector<LexiconCandidate> query_simplified_unlocked(
        const std::string& key,
        const std::vector<std::string>& syllable_filter,
        std::size_t limit,
        std::size_t scan_limit) const;
    // Entries that finish the one unfinished syllable at the end of the input,
    // and nothing beyond it.
    [[nodiscard]] std::vector<LexiconCandidate> query_completions_unlocked(
        const std::vector<std::string>& complete_syllables,
        const std::string& trailing_prefix,
        const std::string& canonical_prefix,
        std::size_t limit,
        std::size_t scan_limit) const;

    std::variant<std::monostate, DevLexicon, BinaryLexicon> lexicon_;
    mutable std::shared_ptr<PrefixQueryCache> prefix_query_cache_;
    PinyinSegmenter pinyin_;
    ShuangpinDecoder shuangpin_;
    UserModel user_model_;
};

}  // namespace piinput
