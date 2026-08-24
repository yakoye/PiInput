#include "piinput/engine.h"

#include <ctime>
#include "piinput/full_pinyin_variants.h"
#include "piinput/incremental_decoder.h"
#include "piinput/pinyin_prefix.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace piinput {
namespace {

[[nodiscard]] bool is_full_pinyin_schema(const std::string_view schema) {
    return schema == "full" || schema == "full-pinyin" || schema == "pinyin";
}

enum class DatetimeShortcutKind {
    none,
    date,
    time,
};

[[nodiscard]] DatetimeShortcutKind datetime_shortcut_kind(
    const std::string_view key) noexcept {
    if (key == "riq" || key == "riqi" || key == "date") {
        return DatetimeShortcutKind::date;
    }
    if (key == "sj" || key == "shij" || key == "shijian" || key == "time") {
        return DatetimeShortcutKind::time;
    }
    return DatetimeShortcutKind::none;
}

[[nodiscard]] bool is_canonical_datetime_shortcut(
    const std::string_view key) noexcept {
    return key == "riqi" || key == "date" || key == "shijian" || key == "time";
}

struct FullPinyinDecodeResult {
    std::vector<std::string> variants;
    std::vector<PinyinSegmentation> segmentations;
};

[[nodiscard]] std::size_t utf8_codepoint_count(
    const std::string_view text) noexcept {
    std::size_t count = 0U;
    for (const unsigned char ch : text) {
        if ((ch & 0xC0U) != 0x80U) ++count;
    }
    return count;
}

[[nodiscard]] std::size_t pinyin_syllable_count(
    const std::string_view pinyin) noexcept {
    if (pinyin.empty()) return 0U;
    return 1U + static_cast<std::size_t>(
        std::count(pinyin.begin(), pinyin.end(), '\''));
}

// The syllable at `index` inside a canonical pinyin string, empty when the
// string does not reach that far.
[[nodiscard]] std::string_view nth_pinyin_syllable(
    const std::string_view pinyin,
    const std::size_t index) noexcept {
    std::size_t start = 0U;
    for (std::size_t position = 0U;; ++position) {
        const std::size_t separator = pinyin.find('\'', start);
        const std::size_t stop = separator == std::string_view::npos
            ? pinyin.size() : separator;
        if (position == index) return pinyin.substr(start, stop - start);
        if (separator == std::string_view::npos) return {};
        start = separator + 1U;
    }
}

// Syllables that are also how a longer syllable starts: chen before cheng, za
// before zai, xian before xiang. Reaching one of these does not mean the user
// is finished with it, so the reading where it is still growing has to stay
// alive alongside the one where it is complete.
[[nodiscard]] bool syllable_can_grow(const std::string& syllable) {
    static const std::unordered_set<std::string> growable = [] {
        std::unordered_set<std::string> result;
        const auto& all = PinyinSegmenter::standard_syllables();
        for (const auto& candidate : all) {
            for (const auto& longer : all) {
                if (longer.size() > candidate.size() &&
                    longer.starts_with(candidate)) {
                    result.insert(candidate);
                    break;
                }
            }
        }
        return result;
    }();
    return growable.contains(syllable);
}

[[nodiscard]] FullPinyinDecodeResult decode_full_pinyin(
    const std::string_view input,
    const PinyinSettings& settings,
    const std::size_t limit,
    const PinyinSegmenter& pinyin) {
    FullPinyinDecodeResult decoded;
    decoded.variants = normalize_full_pinyin_variants(input, settings, limit);
    std::unordered_set<std::string> seen;
    for (const auto& variant : decoded.variants) {
        for (auto& segmentation : pinyin.segment(variant, limit)) {
            if (!seen.insert(segmentation.canonical).second) {
                continue;
            }
            decoded.segmentations.push_back(std::move(segmentation));
            if (decoded.segmentations.size() == limit) {
                return decoded;
            }
        }
    }
    return decoded;
}

}  // namespace

Engine::Engine()
    : prefix_query_cache_(std::make_shared<PrefixQueryCache>()) {}

Engine::Engine(const Engine& other)
    : prefix_query_cache_(std::make_shared<PrefixQueryCache>()) {
    if (!other.prefix_query_cache_) {
        user_model_ = other.user_model_;
        symbol_shortcuts_ = other.symbol_shortcuts_;
        clock_ = other.clock_;
        return;
    }
    std::shared_lock lock(other.prefix_query_cache_->state_mutex);
    lexicon_ = other.lexicon_;
    pinyin_ = other.pinyin_;
    shuangpin_ = other.shuangpin_;
    user_model_ = other.user_model_;
    // These are configuration, not cache. Leaving them out of the copy made a
    // copied engine quietly lose every symbol and date shortcut -- it still
    // answered, just without the features, which is the kind of difference
    // nothing would report.
    symbol_shortcuts_ = other.symbol_shortcuts_;
    clock_ = other.clock_;
    prefix_query_cache_->lexicon_entry_count.store(
        other.prefix_query_cache_->lexicon_entry_count.load());
}

Engine& Engine::operator=(const Engine& other) {
    if (this != &other) {
        Engine copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void Engine::load_lexicon(const std::filesystem::path& path) {
    if (!prefix_query_cache_) {
        prefix_query_cache_ = std::make_shared<PrefixQueryCache>();
        lexicon_ = std::monostate{};
        pinyin_ = PinyinSegmenter{};
        shuangpin_ = ShuangpinDecoder{};
    }
    std::variant<std::monostate, DevLexicon, BinaryLexicon> loaded;
    std::size_t loaded_entry_count = 0U;
    if (is_binary_lexicon(path)) {
        BinaryLexicon lexicon;
        lexicon.load(path);
        loaded_entry_count = lexicon.entry_count();
        loaded = std::move(lexicon);
    } else {
        DevLexicon lexicon;
        lexicon.load_tsv(path);
        loaded_entry_count = lexicon.entry_count();
        loaded = std::move(lexicon);
    }
    std::unique_lock state_lock(prefix_query_cache_->state_mutex);
    lexicon_ = std::move(loaded);
    {
        std::unique_lock entries_lock(prefix_query_cache_->entries_mutex);
        prefix_query_cache_->entries.clear();
    }
    prefix_query_cache_->lexicon_entry_count.store(loaded_entry_count);
}


void Engine::load_user_model(const std::filesystem::path& path) {
    user_model_.load(path);
}

void Engine::save_user_model(const std::filesystem::path& path) const {
    user_model_.save(path);
}

void Engine::record_selection(const std::string& pinyin, const std::string& word) {
    user_model_.record_selection(pinyin, word);
}

void Engine::record_composed_phrase(const std::string& pinyin, const std::string& word) {
    user_model_.record_composed_phrase(pinyin, word);
}

void Engine::pin_candidate(const std::string& pinyin, const std::string& word) {
    user_model_.pin(pinyin, word);
}

void Engine::unpin_candidate(const std::string& pinyin, const std::string& word) {
    user_model_.unpin(pinyin, word);
}

void Engine::remove_candidate_learning(const std::string& pinyin, const std::string& word) {
    user_model_.remove_learning(pinyin, word);
}

void Engine::suppress_candidate(const std::string& pinyin, const std::string& word) {
    user_model_.suppress(pinyin, word);
}

std::vector<PinyinSegmentation> Engine::decode(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit) const {
    return decode(input, schema, limit, default_settings().pinyin);
}

std::vector<PinyinSegmentation> Engine::decode(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit,
    const PinyinSettings& settings) const {
    if (limit == 0U) {
        return {};
    }
    if (!is_full_pinyin_schema(schema)) {
        return shuangpin_.decode(schema, input, limit, settings.uv_compatibility);
    }

    return decode_full_pinyin(input, settings, limit, pinyin_).segmentations;
}

std::vector<LexiconCandidate> Engine::query_exact_unlocked(
    const std::string& pinyin,
    const std::size_t limit) const {
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        return tsv->query_exact(pinyin, limit);
    }
    if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        return binary->query_exact(pinyin, limit);
    }
    throw std::runtime_error("No lexicon has been loaded");
}

std::vector<LexiconCandidate> Engine::query_prefix_unlocked(
    const std::string& pinyin_prefix,
    const std::size_t limit,
    const std::size_t scan_limit,
    const std::size_t max_syllables) const {
    const std::string cache_key = pinyin_prefix + "\n" +
        std::to_string(limit) + "\n" + std::to_string(scan_limit) + "\n" +
        std::to_string(max_syllables);
    {
        std::shared_lock cache_lock(prefix_query_cache_->entries_mutex);
        const auto cached = prefix_query_cache_->entries.find(cache_key);
        if (cached != prefix_query_cache_->entries.end()) {
            return cached->second;
        }
    }
    std::unique_lock cache_lock(prefix_query_cache_->entries_mutex);
    const auto cached = prefix_query_cache_->entries.find(cache_key);
    if (cached != prefix_query_cache_->entries.end()) {
        return cached->second;
    }
    std::vector<LexiconCandidate> result;
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        result = tsv->query_prefix(pinyin_prefix, limit, scan_limit, max_syllables);
    } else if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        result = binary->query_prefix(pinyin_prefix, limit, scan_limit, max_syllables);
    } else {
        throw std::runtime_error("No lexicon has been loaded");
    }
    constexpr std::size_t cache_capacity = 128U;
    if (prefix_query_cache_->entries.size() == cache_capacity) {
        prefix_query_cache_->entries.clear();
    }
    prefix_query_cache_->entries.try_emplace(cache_key, result);
    return result;
}

std::vector<EngineCandidate> Engine::compose_full_coverage_unlocked(
    const std::vector<std::string>& syllables,
    const std::size_t limit,
    const std::string& trailing_prefix,
    const std::string& canonical_prefix,
    const std::size_t scan_limit) const {
    // A real multi-character word anchors every join, so the shortest useful
    // input is that word plus one more syllable.
    constexpr std::size_t anchor_syllables = 2U;
    constexpr std::size_t max_span_syllables = 8U;
    constexpr std::size_t max_words = 4U;
    constexpr std::size_t beam_width = 4U;
    constexpr std::size_t entries_per_span = 4U;
    // Finishing the last syllable needs a wider look than an exact span: the
    // completion pool is ordered by raw frequency, so the character that
    // actually continues the phrase can sit behind several common ones.
    constexpr std::size_t completion_entries = 12U;
    // One single character may close a join -- that is what a half-typed
    // phrase looks like. Two or more would be chaining characters together.
    constexpr std::size_t max_single_character_links = 1U;
    const std::size_t count = syllables.size();
    const bool completing = !trailing_prefix.empty() && !canonical_prefix.empty() &&
        scan_limit != 0U;
    if (limit == 0U) return {};
    // Without a trailing prefix the anchor word must leave a syllable behind to
    // join to; with one, that leftover syllable is the one being typed.
    if (count < anchor_syllables + (completing ? 0U : 1U)) return {};

    struct ComposedPath {
        std::string word;
        std::string pinyin;
        std::size_t word_count{};
        std::size_t single_characters{};
        bool contextual{};
        std::uint32_t weakest_weight{};
        std::uint64_t total_weight{};
    };
    // Fewer joins first: a single dictionary entry beats a join, and a two-word
    // join beats a three-word one. The weakest link decides next, so a common
    // pairing outranks one leaning on a rare entry. Total weight breaks the
    // remaining ties -- sharing the same weak first word, 备份用户 must still
    // come out ahead of 备份拥护.
    const auto stronger = [](const ComposedPath& left, const ComposedPath& right) {
        // A continuation the dictionary itself vouches for beats a merely
        // frequent character that nothing connects to what was typed.
        if (left.contextual != right.contextual) return left.contextual;
        // A join made only of real words beats one leaning on a character.
        if (left.single_characters != right.single_characters) {
            return left.single_characters < right.single_characters;
        }
        if (left.word_count != right.word_count) return left.word_count < right.word_count;
        if (left.weakest_weight != right.weakest_weight) {
            return left.weakest_weight > right.weakest_weight;
        }
        if (left.total_weight != right.total_weight) {
            return left.total_weight > right.total_weight;
        }
        return left.word < right.word;
    };

    // Extending one path with one dictionary entry, subject to the rule that a
    // single character may only close a join a real word already anchors.
    const auto extend = [&](const ComposedPath& base, const LexiconCandidate& word,
                            const bool closes_input,
                            const bool contextual = false) -> std::optional<ComposedPath> {
        if (base.word_count >= max_words) return std::nullopt;
        if (utf8_codepoint_count(word.word) < 2U) {
            // No character may lead a join, sit in its middle, or follow
            // another character. Those are the shapes that chain characters
            // into a sentence the user never picked.
            if (base.word_count == 0U || !closes_input) return std::nullopt;
            if (base.single_characters >= max_single_character_links) return std::nullopt;
            return ComposedPath{
                base.word + word.word,
                base.pinyin.empty() ? word.pinyin : base.pinyin + "'" + word.pinyin,
                base.word_count + 1U,
                base.single_characters + 1U,
                base.contextual || contextual,
                (std::min)(base.weakest_weight, word.weight),
                base.total_weight + word.weight,
            };
        }
        return ComposedPath{
            base.word + word.word,
            base.pinyin.empty() ? word.pinyin : base.pinyin + "'" + word.pinyin,
            base.word_count + 1U,
            base.single_characters,
            base.contextual || contextual,
            base.word_count == 0U ? word.weight
                                  : (std::min)(base.weakest_weight, word.weight),
            base.total_weight + word.weight,
        };
    };

    std::vector<std::vector<ComposedPath>> states(count + 1U);
    states[0U].push_back(ComposedPath{});
    for (std::size_t position = 0U; position < count; ++position) {
        if (states[position].empty()) continue;
        const std::size_t longest =
            (std::min)(max_span_syllables, count - position);
        // A one-syllable span can only be a single character, which the rules
        // above accept just at the end of the input. When a trailing prefix is
        // present that end has not been typed yet, so it is handled below.
        const std::size_t shortest =
            !completing && position + 1U == count ? 1U : anchor_syllables;
        for (std::size_t length = shortest; length <= longest; ++length) {
            std::string canonical;
            for (std::size_t index = 0U; index < length; ++index) {
                if (!canonical.empty()) canonical.push_back('\'');
                canonical.append(syllables[position + index]);
            }
            const bool closes_input = !completing && position + length == count;
            auto& destination = states[position + length];
            for (const auto& word : query_exact_unlocked(canonical, entries_per_span)) {
                for (const auto& base : states[position]) {
                    if (auto next = extend(base, word, closes_input)) {
                        destination.push_back(*std::move(next));
                    }
                }
                std::stable_sort(destination.begin(), destination.end(), stronger);
                if (destination.size() > beam_width) destination.resize(beam_width);
            }
        }
    }

    // The syllable still being typed closes the join. Anchoring it on a word
    // the user already finished is what keeps the whole phrase on screen while
    // that last syllable is only half entered.
    std::vector<ComposedPath> completed;
    if (completing) {
        // A longer entry that already starts with everything typed is the
        // dictionary's own evidence for how this syllable carries on. Without
        // it the completion pool is ranked by raw frequency alone, and the
        // common character beats the one that actually follows what was typed:
        // 卸载我 would outrank 卸载完 because 我 is the more common character.
        std::vector<std::string> contextual_syllables;
        for (const auto& word : query_prefix_unlocked(
                 canonical_prefix, completion_entries, scan_limit, count + 2U)) {
            const auto syllable = nth_pinyin_syllable(word.pinyin, count);
            if (syllable.empty() || !syllable.starts_with(trailing_prefix)) continue;
            if (std::find(contextual_syllables.begin(), contextual_syllables.end(),
                    syllable) == contextual_syllables.end()) {
                contextual_syllables.emplace_back(syllable);
            }
        }
        const std::size_t earliest =
            count > max_span_syllables ? count - max_span_syllables : 0U;
        for (std::size_t position = (std::max)(earliest, std::size_t{1U});
             position <= count; ++position) {
            if (states[position].empty()) continue;
            const std::vector<std::string> tail(
                syllables.begin() + static_cast<std::ptrdiff_t>(position),
                syllables.end());
            std::string tail_canonical;
            for (const auto& syllable : tail) {
                if (!tail_canonical.empty()) tail_canonical.push_back('\'');
                tail_canonical.append(syllable);
            }
            if (!tail_canonical.empty()) tail_canonical.push_back('\'');
            tail_canonical.append(trailing_prefix);
            // Only the join that reaches the syllable being typed can use the
            // contextual reading; earlier anchors are finishing a different
            // syllable than the entry vouched for.
            if (position == count) {
                for (const auto& syllable : contextual_syllables) {
                    for (const auto& word : query_exact_unlocked(
                             std::string(syllable), entries_per_span)) {
                        for (const auto& base : states[position]) {
                            if (auto next = extend(base, word, true, true)) {
                                completed.push_back(*std::move(next));
                            }
                        }
                    }
                }
            }
            for (const auto& word : query_completions_unlocked(
                     tail, trailing_prefix, tail_canonical, completion_entries,
                     scan_limit)) {
                for (const auto& base : states[position]) {
                    if (auto next = extend(base, word, true)) {
                        completed.push_back(*std::move(next));
                    }
                }
            }
        }
        std::stable_sort(completed.begin(), completed.end(), stronger);
        if (completed.size() > beam_width) completed.resize(beam_width);
    }

    // Joins must stay few enough that ordinary dictionary words keep their
    // place on the first row.
    // Two joins is the quiet default; while a syllable is unfinished the
    // contextual reading and the frequent one both deserve a slot.
    const std::size_t max_results = completing ? 3U : 2U;
    if (!completing) {
        std::stable_sort(states[count].begin(), states[count].end(), stronger);
    }
    const auto& finished = completing ? completed : states[count];
    std::vector<EngineCandidate> results;
    for (const auto& path : finished) {
        // One word covering everything is an ordinary exact match, not a join.
        if (path.word_count < 2U) continue;
        if (results.size() >= (std::min)(limit, max_results)) break;
        // The completion spans the syllable being typed, so it consumes one
        // more than the caller finished.
        const std::size_t consumed = completing ? count + 1U : count;
        results.push_back(EngineCandidate{
            path.word, path.pinyin, path.weakest_weight,
            static_cast<std::int64_t>(path.total_weight), consumed, path.word_count,
            CandidateEvidence{
                completing ? CandidateKind::incomplete_completion
                           : CandidateKind::decoded_sentence,
                consumed, path.word_count, path.single_characters, true}});
    }
    return results;
}

std::vector<LexiconCandidate> Engine::query_simplified_unlocked(
    const std::string& key,
    const std::vector<std::string>& syllable_filter,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        return tsv->query_simplified(key, syllable_filter, limit, scan_limit);
    }
    if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        return binary->query_simplified(key, syllable_filter, limit, scan_limit);
    }
    throw std::runtime_error("No lexicon has been loaded");
}

std::vector<LexiconCandidate> Engine::query_completions_unlocked(
    const std::vector<std::string>& complete_syllables,
    const std::string& trailing_prefix,
    const std::string& canonical_prefix,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    // A zero scan budget is the documented switch that turns completion of the
    // unfinished syllable off entirely, enumeration included.
    if (trailing_prefix.empty() || limit == 0U || scan_limit == 0U) return {};
    const std::string cache_key = "completions\n" + canonical_prefix + "\n" +
        std::to_string(limit) + "\n" + std::to_string(scan_limit);
    {
        std::shared_lock cache_lock(prefix_query_cache_->entries_mutex);
        const auto cached = prefix_query_cache_->entries.find(cache_key);
        if (cached != prefix_query_cache_->entries.end()) return cached->second;
    }

    std::string base;
    for (const auto& syllable : complete_syllables) {
        if (!base.empty()) base.push_back('\'');
        base.append(syllable);
    }
    std::vector<LexiconCandidate> pooled;
    // Finishing one syllable is a small, enumerable set. A prefix scan spends
    // its whole budget inside the phrases hanging off the first matching
    // syllable and never reaches the later ones, so typing "b" alone offered
    // only "ba" words and never 不.
    for (const auto& syllable : PinyinSegmenter::standard_syllables()) {
        if (pooled.size() >= scan_limit) break;
        if (!syllable.starts_with(trailing_prefix)) continue;
        std::string canonical = base;
        if (!canonical.empty()) canonical.push_back('\'');
        canonical.append(syllable);
        for (auto& word : query_exact_unlocked(canonical, limit)) {
            pooled.push_back(std::move(word));
        }
    }
    // Dictionary syllables outside the standard table still arrive through the
    // bounded scan, which cannot exceed the syllable the user is typing.
    for (auto& word : query_prefix_unlocked(
             canonical_prefix, limit, scan_limit, complete_syllables.size() + 1U)) {
        pooled.push_back(std::move(word));
    }
    std::stable_sort(pooled.begin(), pooled.end(),
        [](const LexiconCandidate& left, const LexiconCandidate& right) {
            if (left.weight != right.weight) return left.weight > right.weight;
            if (left.pinyin.size() != right.pinyin.size()) {
                return left.pinyin.size() < right.pinyin.size();
            }
            if (left.pinyin != right.pinyin) return left.pinyin < right.pinyin;
            return left.word < right.word;
        });

    std::vector<LexiconCandidate> result;
    result.reserve((std::min)(limit, pooled.size()));
    std::unordered_set<std::string> seen;
    for (auto& word : pooled) {
        if (result.size() >= limit) break;
        if (!seen.insert(word.word + "\n" + word.pinyin).second) continue;
        result.push_back(std::move(word));
    }

    std::unique_lock cache_lock(prefix_query_cache_->entries_mutex);
    constexpr std::size_t cache_capacity = 128U;
    if (prefix_query_cache_->entries.size() >= cache_capacity) {
        prefix_query_cache_->entries.clear();
    }
    prefix_query_cache_->entries.try_emplace(cache_key, result);
    return result;
}

void Engine::set_clock(Clock clock) { clock_ = std::move(clock); }

std::vector<std::string> Engine::datetime_formats(const std::string& reading) const {
    return generated_candidates_for(reading);
}

std::vector<std::string> Engine::generated_candidates_for(const std::string& key) const {
    const DatetimeShortcutKind kind = datetime_shortcut_kind(key);
    if (kind == DatetimeShortcutKind::none) return {};

    std::tm local{};
    if (clock_) {
        local = clock_();
    } else {
        const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
        if (localtime_s(&local, &now) != 0) return {};
#else
        if (localtime_r(&now, &local) == nullptr) return {};
#endif
    }
    return kind == DatetimeShortcutKind::date
        ? date_candidates(local) : time_candidates(local);
}

void Engine::set_symbol_shortcuts(
    std::unordered_map<std::string, std::vector<std::string>> shortcuts) {
    symbol_shortcuts_ = std::move(shortcuts);
}

// Two ways in. The reading covers every schema at once: double pinyin keys are
// decoded to syllables before they get here, so "pk" in 小鹤 arrives as pai just
// like the full-pinyin spelling does. The raw input covers the English names --
// up, down, left -- which are not pinyin and so never appear as syllables.
void Engine::splice_symbol_shortcuts(
    std::vector<EngineCandidate>& results,
    const std::string& input,
    const std::vector<std::string>& syllables,
    const bool allow_short_datetime_aliases,
    const std::size_t result_limit) const {


    std::string reading;
    for (const auto& syllable : syllables) reading.append(syllable);

    std::vector<std::string> wanted;
    // The date and time formats do not go into the row. There are six or seven
    // of them and several are twenty characters wide, so putting them inline
    // either fills the first row with timestamps or, once they are moved out of
    // the way, leaves only one reachable. One entry opens the rest as a list,
    // which is what every other input method does with them.
    std::string datetime_reading;
    std::string datetime_label;
    const auto collect = [&](const std::string& key) {
        if (key.empty()) return;
        const bool datetime_allowed = allow_short_datetime_aliases ||
            is_canonical_datetime_shortcut(key);
        if (datetime_allowed && datetime_label.empty() &&
            !generated_candidates_for(key).empty()) {
            datetime_reading = key;
            datetime_label = datetime_group_label(
                datetime_shortcut_kind(key) == DatetimeShortcutKind::date);
        }
        const auto found = symbol_shortcuts_.find(key);
        if (found == symbol_shortcuts_.end()) return;
        for (const auto& symbol : found->second) {
            if (std::find(wanted.begin(), wanted.end(), symbol) == wanted.end()) {
                wanted.push_back(symbol);
            }
        }
    };
    collect(reading);
    if (input != reading) collect(input);
    if (wanted.empty() && datetime_label.empty()) return;

    // A symbol the dictionary already offered stays where the ranking put it.
    std::erase_if(wanted, [&](const std::string& symbol) {
        return std::any_of(results.begin(), results.end(),
            [&](const EngineCandidate& candidate) { return candidate.word == symbol; });
    });

    const auto make = [&](const std::string& text) {
        EngineCandidate candidate{};
        candidate.word = text;
        candidate.pinyin = reading.empty() ? input : reading;
        candidate.consumed_syllables = syllables.empty() ? 1U : syllables.size();
        candidate.word_count = 1U;
        candidate.evidence = CandidateEvidence{
            CandidateKind::symbol, candidate.consumed_syllables, 1U, 0U, true};
        return candidate;
    };

    // Short ones go next to the top word, where they are one keypress away.
    // Long ones go to the end rather than into the first row, which they would
    // otherwise take over -- both by filling it and by squeezing whatever else
    // is on it down to an unreadable width.
    std::vector<EngineCandidate> inline_candidates;
    std::vector<EngineCandidate> trailing;
    if (!datetime_label.empty()) {
        // Labelled rather than showing one of the formats, so the row says
        // there is a list to open instead of looking like the answer itself.
        EngineCandidate group = make(datetime_label);
        group.pinyin = datetime_reading;
        group.evidence.kind = CandidateKind::datetime_group;
        inline_candidates.push_back(std::move(group));
    }
    for (const auto& text : wanted) {
        if (utf8_codepoint_count(text) <= inline_candidate_codepoints) {
            inline_candidates.push_back(make(text));
        } else {
            trailing.push_back(make(text));
        }
    }
    // The caller asked for at most result_limit candidates, and that has to hold
    // afterwards. Room is made by dropping dictionary matches from the end --
    // the far side of the list nobody scrolls to -- while keeping the top word
    // ahead of the shortcuts. If even that is not enough the shortcuts
    // themselves are truncated, inline ones first because they are the useful
    // ones; a small limit simply cannot hold seven date formats.
    const std::size_t ahead = (std::min)(results.size(), symbol_shortcut_position);
    const std::size_t budget = result_limit > ahead ? result_limit - ahead : 0U;
    const std::size_t take_inline = (std::min)(inline_candidates.size(), budget);
    const std::size_t take_trailing =
        (std::min)(trailing.size(), budget - take_inline);
    inline_candidates.resize(take_inline);
    trailing.resize(take_trailing);
    const std::size_t added = take_inline + take_trailing;
    if (added == 0U) return;
    const std::size_t keep_ordinary = result_limit - added;
    if (results.size() > keep_ordinary) results.resize(keep_ordinary);

    const std::size_t at = (std::min)(symbol_shortcut_position, results.size());
    results.insert(results.begin() + static_cast<std::ptrdiff_t>(at),
        std::make_move_iterator(inline_candidates.begin()),
        std::make_move_iterator(inline_candidates.end()));
    results.insert(results.end(),
        std::make_move_iterator(trailing.begin()),
        std::make_move_iterator(trailing.end()));
}

std::vector<EngineCandidate> Engine::query(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit) const {
    return query(input, schema, limit, default_settings());
}

std::vector<LexiconCandidate> Engine::lookup_word(
    const std::string_view word,
    const std::size_t limit) const {
    if (word.empty() || limit == 0U || !prefix_query_cache_) {
        return {};
    }
    std::shared_lock state_lock(prefix_query_cache_->state_mutex);
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        return tsv->query_word(word, limit);
    }
    if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        return binary->query_word(word, limit);
    }
    return {};
}

std::vector<LexiconCandidate> Engine::lookup_pinyin(
    const std::string_view pinyin,
    const std::size_t limit) const {
    if (pinyin.empty() || limit == 0U || !prefix_query_cache_) {
        return {};
    }
    std::shared_lock state_lock(prefix_query_cache_->state_mutex);
    return query_exact_unlocked(std::string(pinyin), limit);
}

std::vector<EngineCandidate> Engine::query_segment(
    const ParsedComposition& composition,
    const std::size_t syllable_offset,
    const std::size_t limit) const {
    if (limit == 0U || !prefix_query_cache_) return {};
    if (syllable_offset > composition.syllables.size()) return {};
    const bool trailing_only = syllable_offset == composition.syllables.size();
    if (trailing_only && composition.trailing_prefix.empty()) return {};
    std::shared_lock state_lock(prefix_query_cache_->state_mutex);
    // The unfinished syllable at the end is a segment like any other: it is
    // resolved by completing it to exactly one syllable. Without this the
    // selection can never consume it, so nothing the user staged before it
    // could ever be committed.
    if (trailing_only) {
        const auto completions = query_completions_unlocked(
            {}, composition.trailing_prefix, composition.trailing_prefix, limit,
            static_cast<std::size_t>(PinyinSettings{}.prefix_scan_limit));
        std::vector<EngineCandidate> results;
        results.reserve((std::min)(limit, completions.size()));
        std::unordered_set<std::string> seen_trailing;
        for (const auto& word : completions) {
            if (results.size() >= limit) break;
            if (!seen_trailing.insert(word.word).second) continue;
            const bool single_character = utf8_codepoint_count(word.word) == 1U;
            results.push_back(EngineCandidate{
                word.word, word.pinyin, word.weight,
                static_cast<std::int64_t>(word.weight), 1U, 1U,
                CandidateEvidence{
                    single_character ? CandidateKind::single_character
                                     : CandidateKind::incomplete_completion,
                    1U, 1U, single_character ? 1U : 0U, true}});
        }
        return results;
    }
    std::vector<EngineCandidate> phrase_results;
    std::vector<EngineCandidate> single_results;
    std::unordered_set<std::string> seen;
    constexpr std::size_t max_span = 8U;
    const std::size_t remaining = composition.syllables.size() - syllable_offset;
    const std::size_t longest = (std::min)(remaining, max_span);
    for (std::size_t length = longest; length != 0U; --length) {
        std::string canonical;
        for (std::size_t index = 0U; index < length; ++index) {
            if (!canonical.empty()) canonical.push_back('\'');
            canonical.append(composition.syllables[syllable_offset + index]);
        }
        const auto words = query_exact_unlocked(canonical, limit);
        for (const auto& word : words) {
            const std::string key = word.word;
            if (!seen.insert(key).second) continue;
            const bool single_character = length == 1U;
            auto candidate = EngineCandidate{
                word.word,
                word.pinyin,
                word.weight,
                static_cast<std::int64_t>(word.weight),
                length,
                1U,
                CandidateEvidence{
                    single_character ? CandidateKind::single_character
                                     : CandidateKind::exact_lexicon,
                    length,
                    1U,
                    single_character ? 1U : 0U,
                    length == remaining && composition.trailing_prefix.empty(),
                },
            };
            (single_character ? single_results : phrase_results).push_back(
                std::move(candidate));
        }
    }
    const auto stable_segment_order = [](const auto& left, const auto& right) {
        if (left.consumed_syllables != right.consumed_syllables) {
            return left.consumed_syllables > right.consumed_syllables;
        }
        if (left.base_weight != right.base_weight) return left.base_weight > right.base_weight;
        if (left.word != right.word) return left.word < right.word;
        return left.pinyin < right.pinyin;
    };
    std::stable_sort(phrase_results.begin(), phrase_results.end(), stable_segment_order);
    std::stable_sort(single_results.begin(), single_results.end(), stable_segment_order);

    std::vector<EngineCandidate> results;
    results.reserve((std::min)(limit, phrase_results.size() + single_results.size()));
    const std::size_t phrase_count = (std::min)(limit, phrase_results.size());
    results.insert(
        results.end(),
        std::make_move_iterator(phrase_results.begin()),
        std::make_move_iterator(phrase_results.begin() + static_cast<std::ptrdiff_t>(phrase_count)));
    const std::size_t single_count = (std::min)(
        single_results.size(), limit - results.size());
    results.insert(
        results.end(),
        std::make_move_iterator(single_results.begin()),
        std::make_move_iterator(single_results.begin() + static_cast<std::ptrdiff_t>(single_count)));
    return results;
}

std::optional<ParsedComposition> Engine::parse_composition(
    const std::string& input,
    const std::string& schema,
    const PinyinSettings& settings) const {
    if (input.empty()) return std::nullopt;
    constexpr std::size_t parse_limit = 24U;
    const auto exact = decode(input, schema, parse_limit, settings);
    if (!exact.empty()) {
        return ParsedComposition{exact.front().syllables, {}, exact.front().canonical};
    }
    try {
        const auto prefixes = expand_input_prefix(
            input, schema, pinyin_, shuangpin_, parse_limit, settings.uv_compatibility);
        if (!prefixes.empty()) {
            return ParsedComposition{
                prefixes.front().complete_syllables,
                prefixes.front().trailing_prefix,
                prefixes.front().canonical_prefix,
            };
        }
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<EngineCandidate> Engine::query(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit,
    const PinyinSettings& settings) const {
    SettingsSnapshot snapshot = default_settings();
    snapshot.pinyin = settings;
    snapshot.candidates.max_items = (std::numeric_limits<std::uint32_t>::max)();
    return query(input, schema, limit, snapshot);
}

std::vector<EngineCandidate> Engine::query(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit,
    const SettingsSnapshot& settings) const {
    const std::size_t result_limit = (std::min)(
        limit, static_cast<std::size_t>(settings.candidates.max_items));
    if (result_limit == 0U || input.empty() || entry_count() == 0U) return {};
    std::shared_lock state_lock(prefix_query_cache_->state_mutex);

    constexpr std::size_t parse_limit = 24U;
    std::vector<IncrementalParse> parses;
    std::unordered_set<std::string> seen_parses;
    auto add_parse = [&parses, &seen_parses](IncrementalParse parse) {
        const std::string key = parse.canonical_prefix + "\n" + parse.trailing_prefix;
        if (seen_parses.insert(key).second) parses.push_back(std::move(parse));
    };

    try {
        if (is_full_pinyin_schema(schema)) {
            const auto variants = normalize_full_pinyin_variants(input, settings.pinyin, parse_limit);
            for (const auto& variant : variants) {
                for (const auto& segmentation : pinyin_.segment(variant, parse_limit)) {
                    add_parse(IncrementalParse{
                        segmentation.syllables, {}, segmentation.canonical, segmentation.score});
                }
                if (!settings.pinyin.incomplete_candidates) continue;
                for (const auto& prefix : expand_input_prefix(
                         variant, schema, pinyin_, shuangpin_, parse_limit,
                         settings.pinyin.uv_compatibility)) {
                    add_parse(IncrementalParse{
                        prefix.complete_syllables,
                        prefix.trailing_prefix,
                        prefix.canonical_prefix,
                        prefix.score,
                    });
                }
            }
        } else {
            for (const auto& segmentation : shuangpin_.decode(
                     schema, input, parse_limit, settings.pinyin.uv_compatibility)) {
                add_parse(IncrementalParse{
                    segmentation.syllables, {}, segmentation.canonical, segmentation.score});
            }
            if (settings.pinyin.incomplete_candidates) {
                for (const auto& prefix : expand_input_prefix(
                         input, schema, pinyin_, shuangpin_, parse_limit,
                         settings.pinyin.uv_compatibility)) {
                    add_parse(IncrementalParse{
                        prefix.complete_syllables,
                        prefix.trailing_prefix,
                        prefix.canonical_prefix,
                        prefix.score,
                    });
                }
            }
        }
    } catch (const std::invalid_argument&) {
        return {};
    }

    struct RankedCandidate {
        EngineCandidate candidate;
        std::size_t group{};
        std::size_t parse_rank{};
        std::optional<UserPhrase> user;
    };

    std::unordered_map<std::string, std::vector<UserPhrase>> user_cache;
    const auto states_for = [&](const std::string& pinyin)
        -> const std::vector<UserPhrase>& {
        const auto [found, inserted] = user_cache.try_emplace(pinyin);
        if (inserted) found->second = user_model_.query_exact(pinyin);
        return found->second;
    };
    const auto state_for = [&](const std::string& pinyin, const std::string& word)
        -> std::optional<UserPhrase> {
        const auto& states = states_for(pinyin);
        const auto found = std::find_if(states.begin(), states.end(),
            [&](const UserPhrase& state) { return state.word == word; });
        return found == states.end() ? std::nullopt
                                     : std::optional<UserPhrase>(*found);
    };
    const auto active_learning = [](const std::optional<UserPhrase>& state) {
        return state.has_value() &&
            (state->pinned || state->selection_count != 0U || state->user_created);
    };
    const auto better = [&](const RankedCandidate& left, const RankedCandidate& right) {
        if (left.group != right.group) return left.group < right.group;
        const bool left_pinned = left.user.has_value() && left.user->pinned;
        const bool right_pinned = right.user.has_value() && right.user->pinned;
        if (left_pinned != right_pinned) return left_pinned;
        const bool left_learned = active_learning(left.user);
        const bool right_learned = active_learning(right.user);
        if (left_learned != right_learned) return left_learned;
        if (left_learned && right_learned) {
            if (left.user->last_used != right.user->last_used) {
                return left.user->last_used > right.user->last_used;
            }
            if (left.user->selection_count != right.user->selection_count) {
                return left.user->selection_count > right.user->selection_count;
            }
        }
        // One dictionary entry outranks a join that covers the same syllables.
        if (left.candidate.word_count != right.candidate.word_count) {
            return left.candidate.word_count < right.candidate.word_count;
        }
        if (left.candidate.base_weight != right.candidate.base_weight) {
            return left.candidate.base_weight > right.candidate.base_weight;
        }
        if (left.candidate.consumed_syllables != right.candidate.consumed_syllables) {
            return left.candidate.consumed_syllables > right.candidate.consumed_syllables;
        }
        // A second reading of the same keys -- xi-an beside xian -- slots in by
        // how common its words are, not ahead of everything the first reading
        // offers. Deciding this before weight buried 西安 past sixty xian
        // characters; deciding it after leaves it around its own frequency.
        // It stays a fixed key in the same tuple every candidate is compared
        // on, so the ordering remains a strict weak one.
        if (left.parse_rank != right.parse_rank) {
            return left.parse_rank < right.parse_rank;
        }
        // Joined candidates all report their weakest link as the base weight,
        // so without this they fell back to code-point order and 备份拥护
        // edged out 备份用户.
        if (left.candidate.score != right.candidate.score) {
            return left.candidate.score > right.candidate.score;
        }
        if (left.candidate.word != right.candidate.word) {
            return left.candidate.word < right.candidate.word;
        }
        return left.candidate.pinyin < right.candidate.pinyin;
    };

    std::unordered_map<std::string, RankedCandidate> best;
    const auto submit = [&](RankedCandidate candidate) {
        if (candidate.user.has_value() && candidate.user->suppressed) return;
        if (!candidate.user.has_value() && user_model_.is_suppressed(
                candidate.candidate.pinyin, candidate.candidate.word)) {
            return;
        }
        const auto found = best.find(candidate.candidate.word);
        if (found == best.end()) {
            best.emplace(candidate.candidate.word, std::move(candidate));
        } else if (better(candidate, found->second)) {
            found->second = std::move(candidate);
        }
    };
    // A join exists to cover what no single entry spans. Once the dictionary
    // itself has an entry for everything typed, more joins are just homophone
    // noise pushing the shorter real words down the row.
    bool entry_spans_input = false;
    // `alternative_reading` marks a second way to cut the same keys -- xi-e
    // beside xie. Those words reach group 0 only by having two characters, and
    // from there they outranked every character the reading the user actually
    // typed produces: 西鄂 (weight 6) sat above 些 (weight 3752167). Ranking
    // them with the characters makes weight decide, which leaves 西安 near its
    // own frequency for xian instead of either first or buried.
    const auto add_exact = [&](const std::string& canonical,
                               const std::size_t consumed,
                               const bool covers_all,
                               const std::size_t parse_rank,
                               const bool alternative_reading = false) {
        if (canonical.empty() || consumed == 0U) return;
        for (const auto& state : states_for(canonical)) {
            if (state.suppressed ||
                (!state.user_created && state.selection_count == 0U && !state.pinned)) {
                continue;
            }
            const bool single = utf8_codepoint_count(state.word) == 1U;
            const std::size_t group = alternative_reading
                ? 2U
                : (single ? 2U : (covers_all ? 0U : 1U));
            submit(RankedCandidate{
                EngineCandidate{
                    state.word, state.pinyin, 0U,
                    static_cast<std::int64_t>(state.learning_tier) * 1000,
                    consumed, 1U,
                    CandidateEvidence{
                        CandidateKind::user_phrase, consumed, 1U,
                        single ? 1U : 0U, covers_all}},
                group, parse_rank, state});
        }
        for (const auto& word : query_exact_unlocked(canonical, result_limit)) {
            const bool single = utf8_codepoint_count(word.word) == 1U;
            if (covers_all && !single) entry_spans_input = true;
            const std::size_t group = alternative_reading
                ? 2U
                : (single ? 2U : (covers_all ? 0U : 1U));
            auto state = state_for(word.pinyin, word.word);
            const CandidateKind kind = state.has_value() && state->user_created
                ? CandidateKind::user_phrase
                : (single ? CandidateKind::single_character
                          : (covers_all ? CandidateKind::exact_lexicon
                                        : CandidateKind::prefix_lexicon));
            submit(RankedCandidate{
                EngineCandidate{
                    word.word, word.pinyin, word.weight,
                    static_cast<std::int64_t>(word.weight), consumed, 1U,
                    CandidateEvidence{
                        kind, consumed, 1U, single ? 1U : 0U, covers_all}},
                group, parse_rank, std::move(state)});
        }
    };

    const IncrementalParse* primary_parse = nullptr;
    std::size_t primary_parse_index = 0U;
    for (std::size_t index = 0U; index < parses.size(); ++index) {
        if (parses[index].trailing_prefix.empty()) {
            primary_parse = &parses[index];
            primary_parse_index = index;
            break;
        }
    }
    if (primary_parse == nullptr && !parses.empty()) {
        primary_parse = &parses.front();
        primary_parse_index = 0U;
    }
    if (primary_parse != nullptr) {
        const auto& parse = *primary_parse;
        const std::size_t parse_index = primary_parse_index;
        const std::size_t complete = parse.complete_syllables.size();
        // A selectable prefix uses the same practical word-span bound as
        // segment selection. For long compositions, querying every historical
        // prefix made each new key progressively slower even though those
        // sentence-length prefixes almost never represent one lexical entry.
        constexpr std::size_t max_prefix_word_span = 8U;
        if (complete > max_prefix_word_span && parse.trailing_prefix.empty()) {
            add_exact(PinyinSegmenter::join(parse.complete_syllables),
                complete, true, parse_index);
        }
        for (std::size_t length = (std::min)(complete, max_prefix_word_span);
             length != 0U; --length) {
            std::string canonical;
            for (std::size_t index = 0U; index < length; ++index) {
                if (!canonical.empty()) canonical.push_back('\'');
                canonical.append(parse.complete_syllables[index]);
            }
            add_exact(canonical, length,
                length == complete && parse.trailing_prefix.empty(), parse_index);
        }
        // No single entry spans everything typed so far, so join real words to
        // keep the whole thing on screen instead of only a candidate for the
        // first two syllables, and finish the syllable still being typed. This
        // runs on every keystroke, including the ones in the middle of a
        // syllable -- that is what stops the candidate list from freezing while
        // a phrase is still being typed.
        const auto emit_joins_and_completions = [&](
                const std::vector<std::string>& syllables,
                const std::string& trailing,
                const std::string& canonical,
                const std::size_t rank) {
            const bool completing = settings.pinyin.incomplete_candidates &&
                !trailing.empty() && !canonical.empty();
            if (trailing.empty() || completing) {
                for (auto& composed : compose_full_coverage_unlocked(
                         syllables,
                         entry_spans_input ? 0U : result_limit,
                         completing ? trailing : std::string{},
                         completing ? canonical : std::string{},
                         completing
                             ? static_cast<std::size_t>(settings.pinyin.prefix_scan_limit)
                             : 0U)) {
                    auto state = state_for(composed.pinyin, composed.word);
                    submit(RankedCandidate{std::move(composed), 0U, rank, std::move(state)});
                }
            }
            if (!completing) return;
            // Finish the syllable being typed, never the ones after it. The
            // user has reached syllable `complete + 1`, so a completion may
            // span that far and no further; without the bound a three-letter
            // input offers six-syllable phrases the user never typed towards.
            for (const auto& word : query_completions_unlocked(
                     syllables, trailing, canonical, result_limit,
                     static_cast<std::size_t>(settings.pinyin.prefix_scan_limit))) {
                const bool single = utf8_codepoint_count(word.word) == 1U;
                const std::size_t consumed = pinyin_syllable_count(word.pinyin);
                auto state = state_for(word.pinyin, word.word);
                const CandidateKind kind = state.has_value() && state->user_created
                    ? CandidateKind::user_phrase
                    : (single ? CandidateKind::single_character
                              : CandidateKind::incomplete_completion);
                // Finishing the syllable makes this entry span everything
                // typed, same as a join does. Ranking it below the joins let
                // 我先咋 and 我先砸 bury the real word 我现在; sharing the
                // group lets the word-count tiebreak put the entry first.
                submit(RankedCandidate{
                    EngineCandidate{
                        word.word, word.pinyin, word.weight,
                        static_cast<std::int64_t>(word.weight), consumed, 1U,
                        CandidateEvidence{
                            kind, consumed, 1U, single ? 1U : 0U, true}},
                    single ? 2U : 0U, rank, std::move(state)});
            }
        };
        emit_joins_and_completions(parse.complete_syllables, parse.trailing_prefix,
            parse.canonical_prefix, parse_index);
        // The last syllable is finished, but in full pinyin a finished syllable
        // is often just the opening of a longer one -- chen before cheng, za
        // before zai -- and the tail may need more than one syllable rolled
        // back: ti'a is how tian looks halfway through. The prefix readings are
        // already parsed, they were simply never given to the lexicon. Run the
        // shortest-tailed ones too, or every keystroke passing through such a
        // syllable collapses the list back to the previous word.
        // Roll the tail back one syllable at a time and read it as still being
        // typed. Never roll back everything: a lone syllable someone just
        // finished is not half of something they have not started, and
        // treating it that way floods a one-syllable input with completions.
        // Full pinyin only. A double-pinyin syllable is two fixed keys, so its
        // reading never grows with the next keystroke -- rolling 小鹤 back to
        // he and completing it offered 小黑, a word the keys cannot spell.
        constexpr std::size_t max_rolled_back_syllables = 2U;
        if (is_full_pinyin_schema(schema) && settings.pinyin.incomplete_candidates &&
            parse.trailing_prefix.empty() && complete > 1U &&
            syllable_can_grow(parse.complete_syllables.back())) {
            for (std::size_t rolled = 1U;
                 rolled <= (std::min)(max_rolled_back_syllables, complete - 1U); ++rolled) {
                const std::size_t kept = complete - rolled;
                std::string trailing;
                for (std::size_t index = kept; index < complete; ++index) {
                    trailing.append(parse.complete_syllables[index]);
                }
                if (!pinyin_.is_valid_prefix(trailing)) continue;
                std::vector<std::string> head(parse.complete_syllables.begin(),
                    parse.complete_syllables.begin() + static_cast<std::ptrdiff_t>(kept));
                std::string canonical = PinyinSegmenter::join(head);
                if (!canonical.empty()) canonical.push_back('\'');
                canonical.append(trailing);
                emit_joins_and_completions(head, trailing, canonical, parse_index);
            }
        }
    }
    // Simplified pinyin: every syllable reduced to its initial, optionally
    // mixed with spelled-out ones. This is its own lookup rather than an
    // extension of the prefix path -- zsjs is not a prefix of
    // zhi'shi'jing'shen, so the ordinary sorted-by-pinyin index cannot find it
    // at all. Full pinyin only; a double-pinyin syllable is already two fixed
    // keys and has nothing left to shorten.
    // Only when nothing reads the input as ordinary pinyin. "wang" is a
    // syllable, and letting it also mean w+a+n+g buried 王 under four-character
    // words nobody asked for. The spellings simplified pinyin exists for --
    // zsjs, wlj, sruf -- have no full-pinyin reading at all.
    const bool reads_as_full_pinyin = std::any_of(parses.begin(), parses.end(),
        [](const IncrementalParse& parse) { return parse.trailing_prefix.empty(); });
    if (settings.pinyin.simplified_pinyin && is_full_pinyin_schema(schema) &&
        !reads_as_full_pinyin) try {
        constexpr std::size_t reading_limit = 8U;
        // Simplified spellings only run when nothing reads the input as
        // ordinary pinyin, so there is no competing source to crowd out --
        // holding back half the list just left it empty. The cap is there to
        // bound the scan, not to reserve room for anyone else.
        const std::size_t simplified_limit = (std::min<std::size_t>)(result_limit, 24U);
        const auto readings = simplified_pinyin_readings(
            PinyinSegmenter::normalize(input), pinyin_, reading_limit);
        for (std::size_t reading_index = 0U; reading_index < readings.size();
             ++reading_index) {
            const auto& reading = readings[reading_index];
            for (const auto& word : query_simplified_unlocked(
                     reading.key, reading.syllables, simplified_limit,
                     static_cast<std::size_t>(settings.pinyin.prefix_scan_limit))) {
                const bool single = utf8_codepoint_count(word.word) == 1U;
                const std::size_t syllables = pinyin_syllable_count(word.pinyin);
                // A word with exactly as many syllables as initials typed is
                // finished; longer ones only matched because the key is also a
                // prefix. The outer ranking compares consumed syllables in
                // descending order, which would hand the row to the longer word
                // the user never finished -- 收入分配 ahead of 输入法 for sruf.
                // Separating them by parse rank settles it before that.
                // Readings are already ordered best-first, but the outer
                // ranking compares consumed syllables in descending order and
                // would hand zhly to 综合利用 (z+h+l+y) over 张靓颖 (zh+l+y).
                // Ranking by reading settles it before that comparison.
                const std::size_t rank = parses.size() + reading_index * 2U +
                    (syllables == reading.key.size() ? 0U : 1U);
                auto state = state_for(word.pinyin, word.word);
                submit(RankedCandidate{
                    EngineCandidate{
                        word.word, word.pinyin, word.weight,
                        static_cast<std::int64_t>(word.weight),
                        syllables, 1U,
                        CandidateEvidence{
                            CandidateKind::simplified_pinyin, syllables, 1U,
                            single ? 1U : 0U, true}},
                    single ? 2U : 1U, rank, std::move(state)});
            }
        }
        // Characters for the first syllable, after the phrases. A simplified
        // spelling has no ordinary reading at all, so nothing else in this
        // function contributes anything: mk offered its 24 phrases and then
        // stopped dead. Falling back to characters lets the user commit the
        // first one and carry on, which is what the character fallback already
        // does for every other kind of input.
        if (!readings.empty()) {
            const std::string initial(1U, readings.front().key.front());
            for (const auto& word : query_completions_unlocked(
                     {}, initial, initial, simplified_limit,
                     static_cast<std::size_t>(settings.pinyin.prefix_scan_limit))) {
                const bool single = utf8_codepoint_count(word.word) == 1U;
                const std::size_t syllables = pinyin_syllable_count(word.pinyin);
                auto state = state_for(word.pinyin, word.word);
                submit(RankedCandidate{
                    EngineCandidate{
                        word.word, word.pinyin, word.weight,
                        static_cast<std::int64_t>(word.weight), syllables, 1U,
                        CandidateEvidence{
                            single ? CandidateKind::single_character
                                   : CandidateKind::prefix_lexicon,
                            syllables, 1U, single ? 1U : 0U, false}},
                    2U, parses.size() + readings.size() * 2U, std::move(state)});
            }
        }
    } catch (const std::invalid_argument&) {
        // normalize() rejects characters that are not pinyin at all. The hot
        // path is contractually non-throwing, and a simplified reading is an
        // extra source of candidates -- if the input cannot be normalised there
        // is simply nothing to add.
    }
    for (std::size_t parse_index = 0U; parse_index < parses.size(); ++parse_index) {
        if (parse_index == primary_parse_index ||
            !parses[parse_index].trailing_prefix.empty() ||
            parses[parse_index].complete_syllables.empty()) {
            continue;
        }
        const auto& parse = parses[parse_index];
        // Long professional terms are exactly where pinyin boundary scoring is
        // least reliable: shu-ru-shu-chu-nei... can score below
        // shu-ru-shu-chun-ei.... An exact dictionary entry is bounded evidence
        // for the whole input, so consult alternate long parses too. Short
        // alternate readings retain the lower group that prevents xi-e words
        // from displacing the common xie characters.
        const bool long_exact = parse.complete_syllables.size() > 8U;
        add_exact(PinyinSegmenter::join(parse.complete_syllables),
            parse.complete_syllables.size(), true, parse_index, !long_exact);
    }

    std::vector<RankedCandidate> ranked;
    ranked.reserve(best.size());
    for (auto& [word, candidate] : best) {
        (void)word;
        ranked.push_back(std::move(candidate));
    }
    std::sort(ranked.begin(), ranked.end(), better);
    if (ranked.size() > result_limit) ranked.resize(result_limit);
    std::vector<EngineCandidate> results;
    results.reserve(ranked.size());
    for (auto& candidate : ranked) {
        results.push_back(std::move(candidate.candidate));
    }
    // A finished syllable only matches words whose whole reading is that
    // syllable, so an input like ri or nv ran out after two or three
    // characters and the row looked like the dictionary had nothing left.
    // Words that merely start with it -- 日本 for ri, 女孩 for nv -- are what
    // every other input method shows there. They are appended rather than
    // ranked in, and only when the exact matches did not already fill the
    // limit, so nothing that was on the first row moves.
    if (primary_parse != nullptr && primary_parse->trailing_prefix.empty() &&
        !primary_parse->canonical_prefix.empty() &&
        results.size() < (std::min)(result_limit, prefix_fill_threshold)) {
        std::unordered_set<std::string> already;
        already.reserve(results.size());
        for (const auto& candidate : results) already.insert(candidate.word);
        const std::size_t room = result_limit - results.size();
        constexpr std::size_t max_prefix_syllables = 4U;
        for (const auto& word : query_prefix_unlocked(
                 primary_parse->canonical_prefix, room,
                 static_cast<std::size_t>(settings.pinyin.prefix_scan_limit),
                 max_prefix_syllables)) {
            if (results.size() >= result_limit) break;
            if (!already.insert(word.word).second) continue;
            // A candidate the user deleted must stay deleted. The ordinary path
            // filters these in submit(); this one bypasses that ranking step and
            // so has to ask as well.
            if (user_model_.is_suppressed(word.pinyin, word.word)) continue;
            const std::size_t spanned = pinyin_syllable_count(word.pinyin);
            EngineCandidate candidate{};
            candidate.word = word.word;
            candidate.pinyin = word.pinyin;
            candidate.base_weight = word.weight;
            candidate.score = static_cast<std::int64_t>(word.weight);
            candidate.consumed_syllables = spanned;
            candidate.word_count = 1U;
            candidate.evidence = CandidateEvidence{
                CandidateKind::prefix_lexicon, spanned, 1U, 0U, false};
            results.push_back(std::move(candidate));
        }
    }

    // After the limit, so a symbol is never the thing that pushed a real word
    // off the end of the row.
    splice_symbol_shortcuts(results, input,
        primary_parse != nullptr ? primary_parse->complete_syllables
                                 : std::vector<std::string>{},
        is_full_pinyin_schema(schema),
        result_limit);
    return results;
}

std::size_t Engine::entry_count() const noexcept {
    if (!prefix_query_cache_) {
        return 0U;
    }
    return prefix_query_cache_->lexicon_entry_count.load();
}

bool Engine::lexicon_memory_mapped() const noexcept {
    const auto* const binary = std::get_if<BinaryLexicon>(&lexicon_);
    return binary != nullptr && binary->memory_mapped();
}

std::size_t Engine::lexicon_mapped_bytes() const noexcept {
    const auto* const binary = std::get_if<BinaryLexicon>(&lexicon_);
    return binary != nullptr ? binary->mapped_bytes() : 0U;
}

const ShuangpinDecoder& Engine::shuangpin() const noexcept {
    return shuangpin_;
}

}  // namespace piinput
