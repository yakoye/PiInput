#pragma once

#include "piinput/candidate_evidence.h"
#include "piinput/datetime_candidates.h"
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
#include <functional>
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
    // Symbols reachable by typing their name as ordinary pinyin: pai gives π,
    // qiuhe gives ∑. Keyed by the reading, so double pinyin needs no separate
    // table -- the keys are decoded to syllables first and the syllables are
    // what is looked up here.
    void set_symbol_shortcuts(
        std::unordered_map<std::string, std::vector<std::string>> shortcuts);

    // What "now" means when riq/riqi or sj/shij/shijian are typed. Defaults to
    // the system clock; tests pin it so the expected strings can be written down.
    using Clock = std::function<std::tm()>;
    void set_clock(Clock clock);
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
    [[nodiscard]] bool lexicon_memory_mapped() const noexcept;
    [[nodiscard]] std::size_t lexicon_mapped_bytes() const noexcept;
    [[nodiscard]] const ShuangpinDecoder& shuangpin() const noexcept;

private:
    // Placed just after the top candidate rather than first: someone typing
    // "pai" usually wants 拍 or 派, and a symbol that displaced them would be
    // in the way far more often than it helped.
    static constexpr std::size_t symbol_shortcut_position = 1U;
    // A candidate longer than this shares a row badly. The candidate window
    // aligns columns across rows and shrinks them all proportionally when the
    // total will not fit, so one twenty-character timestamp squeezes every
    // short word on its row down to nothing -- which is how a full list of 96
    // candidates rendered as two items and a stretch of blank. Long ones go to
    // the end instead, where they get rows of their own.
    static constexpr std::size_t inline_candidate_codepoints = 12U;
    // Below this many candidates the row looks like the dictionary gave up, and
    // words merely starting with the input are worth appending. Above it there
    // is already more than a screen of real matches, and running the extra
    // prefix scan on every keystroke cost enough to show up in the p95.
    static constexpr std::size_t prefix_fill_threshold = 12U;
    // riq/riqi and sj/shij/shijian spell out the current date and time in every
    // supported format. Generated per keystroke rather than stored, so they
    // cannot go stale the way a cached string would.
    [[nodiscard]] std::vector<std::string> generated_candidates_for(
        const std::string& key) const;

public:
    // The formats behind a datetime_group candidate, regenerated at the moment
    // the user opens the list so the clock is current rather than whatever it
    // read when the candidates were first built.
    [[nodiscard]] std::vector<std::string> datetime_formats(
        const std::string& reading) const;

private:
    void splice_symbol_shortcuts(
        std::vector<EngineCandidate>& results,
        const std::string& input,
        const std::vector<std::string>& syllables,
        bool allow_short_datetime_aliases,
        std::size_t result_limit) const;

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
    std::unordered_map<std::string, std::vector<std::string>> symbol_shortcuts_;
    Clock clock_;
};

}  // namespace piinput
