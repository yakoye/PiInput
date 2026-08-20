#include "piinput/incremental_decoder.h"
#include "piinput/pinyin.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace piinput {
namespace {

constexpr std::int64_t exact_phrase_bonus = 30'000'000;
constexpr std::int64_t token_penalty = 12'000'000;
constexpr std::int64_t joined_syllable_bonus = 4'000'000;
constexpr std::int64_t incomplete_input_penalty = 8'000'000;
constexpr std::int64_t completion_character_penalty = 100'000;
constexpr std::int64_t single_word_completion_bonus = 30'000'000;
// Completing the syllable that the user has started is useful; inventing
// additional, entirely untyped syllables is much less certain.  Keep concise
// boundary completions (rug -> 如果/入股/乳沟) ahead of a high-frequency long
// phrase that merely shares the same prefix (如鲠在喉).
constexpr std::int64_t untyped_syllable_penalty = 20'000'000;
constexpr std::int64_t reopened_syllable_penalty = 60'000'000;
constexpr std::int64_t nonfinal_particle_penalty = 6'000'000;
constexpr std::int64_t supplementary_character_penalty = 5'000'000;

// Every completion word for an unfinished syllable is combined with the decoded
// sentence prefixes that precede it. The sentence beam itself stays wide so
// segmentation quality is unchanged, but pairing a completion with the 9th-best
// reading of the same leading syllables only produces near-duplicates far below
// the visible rows, and the full cross product dominated the keystroke budget.
constexpr std::size_t terminal_prefix_path_budget = 8U;

[[nodiscard]] std::int64_t untyped_syllable_cost(
    const std::size_t untyped_syllables) noexcept {
    // One extra syllable may be needed to finish a common lexical unit
    // (zh -> 知道).  Penalize only the second and later speculative syllables.
    return static_cast<std::int64_t>(
        untyped_syllables > 1U ? untyped_syllables - 1U : 0U) *
        untyped_syllable_penalty;
}

[[nodiscard]] std::int64_t reopened_syllable_cost(
    const std::size_t reopened_syllables) noexcept {
    // Looking back one complete syllable is needed for ordinary two-syllable
    // words (m + t -> 明天).  Reopening a longer, already stable suffix during
    // an incomplete keystroke tends to produce unrelated phrases.
    return static_cast<std::int64_t>(
        reopened_syllables > 1U ? reopened_syllables - 1U : 0U) *
        reopened_syllable_penalty;
}
// Pinyin boundary length is only a weak prior. A strong scale makes the
// longest legal syllable split override clear dictionary evidence, e.g.
// qian'gao -> qiang'ao and cha'na -> chan'a.
constexpr std::int64_t parse_score_scale = 100'000;

[[nodiscard]] std::int64_t frequency_score(const std::uint32_t weight) {
    return static_cast<std::int64_t>(std::log1p(static_cast<double>(weight)) * 1'000'000.0);
}

[[nodiscard]] bool is_sentence_final_particle(const std::string_view word) noexcept {
    return word == "吧" || word == "吗" || word == "呢" || word == "啊" ||
           word == "呀" || word == "哇" || word == "啦" || word == "呗" ||
           word == "嘛";
}

[[nodiscard]] std::int64_t character_rarity_penalty(
    const std::string_view word) noexcept {
    return std::any_of(word.begin(), word.end(), [](const char value) {
        return static_cast<unsigned char>(value) >= 0xF0U;
    }) ? supplementary_character_penalty : 0;
}

[[nodiscard]] std::int64_t scaled_parse_score(const int score) noexcept {
    return static_cast<std::int64_t>(score) * parse_score_scale;
}

[[nodiscard]] std::string join_range(
    const std::vector<std::string>& syllables,
    const std::size_t begin,
    const std::size_t end) {
    std::size_t size = end > begin ? end - begin - 1U : 0U;
    for (std::size_t index = begin; index < end; ++index) {
        size += syllables[index].size();
    }
    std::string result;
    result.reserve(size);
    for (std::size_t index = begin; index < end; ++index) {
        if (index != begin) {
            result.push_back('\'');
        }
        result.append(syllables[index]);
    }
    return result;
}

[[nodiscard]] std::size_t utf8_codepoint_count(const std::string_view text) noexcept {
    std::size_t count = 0U;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::optional<std::size_t> matching_candidate_syllable_count(
    const LexiconCandidate& candidate,
    const std::vector<std::string>& complete,
    const std::size_t start,
    const std::string_view trailing) {
    static const std::unordered_set<std::string_view> standard_syllables(
        PinyinSegmenter::standard_syllables().begin(),
        PinyinSegmenter::standard_syllables().end());
    const std::size_t exact_count = complete.size() - start;
    if (trailing.empty()) {
        return std::nullopt;
    }
    std::size_t syllable_count = 0U;
    std::size_t syllable_start = 0U;
    while (syllable_start <= candidate.pinyin.size()) {
        const std::size_t separator = candidate.pinyin.find('\'', syllable_start);
        const std::size_t syllable_end = separator == std::string::npos
            ? candidate.pinyin.size() : separator;
        if (syllable_end == syllable_start) {
            return std::nullopt;
        }
        const std::string_view syllable(
            candidate.pinyin.data() + syllable_start,
            syllable_end - syllable_start);
        if (!standard_syllables.contains(syllable)) {
            return std::nullopt;
        }
        if (syllable_count < exact_count) {
            if (syllable != complete[start + syllable_count]) {
                return std::nullopt;
            }
        } else if (syllable_count == exact_count && !syllable.starts_with(trailing)) {
            return std::nullopt;
        }
        ++syllable_count;
        if (separator == std::string::npos) {
            break;
        }
        syllable_start = separator + 1U;
    }
    if (syllable_count < exact_count + 1U) {
        return std::nullopt;
    }
    return syllable_count;
}

struct SentencePath {
    std::string text;
    std::uint32_t aggregate_weight{};
    std::int64_t score{};
    std::size_t word_count{};
    std::size_t single_character_tokens{};
};

struct ScoredExactWord {
    const LexiconCandidate* word{};
    std::int64_t edge_score{};
};

struct RankedPrefixWord {
    const LexiconCandidate* word{};
    std::size_t syllable_count{};
    std::size_t untyped_syllables{};
    std::size_t reopened_syllables{};
    std::size_t remaining_characters{};
    std::int64_t edge_score{};
};

[[nodiscard]] bool path_better(const SentencePath& left, const SentencePath& right) {
    if (left.score != right.score) return left.score > right.score;
    if (left.aggregate_weight != right.aggregate_weight) return left.aggregate_weight > right.aggregate_weight;
    if (left.word_count != right.word_count) return left.word_count < right.word_count;
    return left.text < right.text;
}

// Admission test that runs before the candidate path text is built. `paths` is
// kept sorted worst-last, so a full beam rejects anything the last entry
// already beats -- including duplicates, because a duplicate this path could
// replace would itself rank at or above the last entry. Ties on the numeric
// fields fall through to the full comparison, which needs the text.
[[nodiscard]] bool path_can_enter(
    const std::vector<SentencePath>& paths,
    const std::int64_t score,
    const std::uint32_t aggregate_weight,
    const std::size_t word_count,
    const std::size_t beam_width) noexcept {
    if (paths.size() < beam_width) return true;
    const SentencePath& worst = paths.back();
    if (score != worst.score) return score > worst.score;
    if (aggregate_weight != worst.aggregate_weight) {
        return aggregate_weight > worst.aggregate_weight;
    }
    if (word_count != worst.word_count) return word_count < worst.word_count;
    return true;
}

void insert_bounded_path(
    std::vector<SentencePath>& paths,
    SentencePath path,
    const std::size_t beam_width) {
    const auto duplicate = std::find_if(
        paths.begin(), paths.end(), [&](const SentencePath& existing) {
            return existing.text == path.text;
        });
    if (duplicate != paths.end()) {
        if (!path_better(path, *duplicate)) {
            return;
        }
        paths.erase(duplicate);
    }
    const auto position = std::upper_bound(
        paths.begin(), paths.end(), path,
        [](const SentencePath& value, const SentencePath& existing) {
            return path_better(value, existing);
        });
    if (paths.size() == beam_width && position == paths.end()) {
        return;
    }
    paths.insert(position, std::move(path));
    if (paths.size() > beam_width) {
        paths.pop_back();
    }
}

[[nodiscard]] bool candidate_better(
    const IncrementalCandidate& left,
    const IncrementalCandidate& right) {
    if (left.score != right.score) return left.score > right.score;
    if (left.base_weight != right.base_weight) return left.base_weight > right.base_weight;
    if (left.word.size() != right.word.size()) {
        return left.word.size() > right.word.size();
    }
    if (left.consumed_syllables != right.consumed_syllables) {
        return left.consumed_syllables > right.consumed_syllables;
    }
    if (left.word_count != right.word_count) return left.word_count < right.word_count;
    if (left.word != right.word) return left.word < right.word;
    return left.pinyin < right.pinyin;
}

}  // namespace

IncrementalDecoder::IncrementalDecoder(
    ExactQuery exact_query,
    PrefixQuery prefix_query,
    UserScore user_score)
    : exact_query_(std::move(exact_query)),
      prefix_query_(std::move(prefix_query)),
      user_score_(std::move(user_score)) {}

std::vector<IncrementalCandidate> IncrementalDecoder::decode(
    const std::vector<IncrementalParse>& parses,
    const IncrementalDecodeOptions& options,
    IncrementalDecodeStats* const stats) const {
    if (stats != nullptr) {
        *stats = {};
        stats->input_parse_count = parses.size();
    }
    if (parses.empty() || options.result_limit == 0U || options.beam_width == 0U ||
        options.max_word_syllables == 0U || !exact_query_ || !prefix_query_) {
        return {};
    }

    std::unordered_map<std::string, IncrementalCandidate> best;
    std::unordered_map<std::string, std::vector<LexiconCandidate>> exact_cache;
    std::unordered_map<std::string, std::vector<LexiconCandidate>> prefix_cache;
    const std::size_t state_beam_width = (std::min)(
        options.beam_width, options.result_limit);
    const std::size_t exact_candidate_limit = (std::min)(
        options.beam_width, (std::max<std::size_t>)(8U, options.result_limit));
    std::vector<const IncrementalParse*> unique_parses;
    std::unordered_map<std::string, std::size_t> parse_indexes;
    for (const auto& parse : parses) {
        const std::string parse_key =
            parse.canonical_prefix + "\n" + parse.trailing_prefix;
        const auto [found, inserted] = parse_indexes.emplace(
            parse_key, unique_parses.size());
        if (inserted) {
            unique_parses.push_back(&parse);
        } else if (parse.parse_score > unique_parses[found->second]->parse_score) {
            unique_parses[found->second] = &parse;
        }
    }
    if (stats != nullptr) {
        stats->unique_parse_count = unique_parses.size();
    }
    auto collect_prefix_keys = [&options](const IncrementalParse& parse) {
        std::vector<std::string> keys;
        const std::size_t complete_count = parse.complete_syllables.size();
        const std::size_t earliest = complete_count >= options.max_word_syllables - 1U
            ? complete_count - (options.max_word_syllables - 1U) : 0U;
        keys.reserve(complete_count - earliest + 1U);
        for (std::size_t start = earliest; start <= complete_count; ++start) {
            std::string key = join_range(parse.complete_syllables, start, complete_count);
            if (!key.empty()) {
                key.push_back('\'');
            }
            key.append(parse.trailing_prefix);
            keys.push_back(std::move(key));
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        return keys;
    };
    std::vector<const IncrementalParse*> retained_parses;
    retained_parses.reserve(unique_parses.size());
    std::unordered_set<std::string> funded_prefix_keys;
    std::size_t retained_prefix_parses = 0U;
    for (const auto* parse : unique_parses) {
        if (parse->trailing_prefix.empty()) {
            retained_parses.push_back(parse);
            continue;
        }
        if (!options.incomplete_candidates || options.prefix_scan_limit == 0U ||
            retained_prefix_parses == options.beam_width) {
            continue;
        }
        const auto keys = collect_prefix_keys(*parse);
        const std::size_t new_key_count = static_cast<std::size_t>(std::count_if(
            keys.begin(), keys.end(), [&](const auto& key) {
                return !funded_prefix_keys.contains(key);
            }));
        if (new_key_count > options.prefix_scan_limit - funded_prefix_keys.size()) {
            continue;
        }
        retained_parses.push_back(parse);
        ++retained_prefix_parses;
        for (const auto& key : keys) {
            funded_prefix_keys.insert(key);
        }
    }
    unique_parses = std::move(retained_parses);
    if (stats != nullptr) {
        stats->retained_parse_count = unique_parses.size();
    }
    const std::size_t direct_exact_limit = options.result_limit >
            (std::numeric_limits<std::size_t>::max)() / 4U
        ? (std::numeric_limits<std::size_t>::max)()
        : options.result_limit * 4U;
    for (const auto* parse : unique_parses) {
        if (!parse->trailing_prefix.empty() || parse->complete_syllables.empty()) {
            continue;
        }
        const std::string key = PinyinSegmenter::join(parse->complete_syllables);
        auto [cached, inserted] = exact_cache.try_emplace(key);
        if (inserted) {
            if (stats != nullptr) {
                ++stats->exact_query_calls;
            }
            cached->second = exact_query_(key, direct_exact_limit);
        }
    }
    std::unordered_map<std::string, std::size_t> prefix_key_priorities;
    if (options.incomplete_candidates && options.prefix_scan_limit != 0U) {
        for (const auto* parse : unique_parses) {
            if (parse->trailing_prefix.empty()) {
                continue;
            }
            for (auto& key : collect_prefix_keys(*parse)) {
                std::size_t priority = 1U;
                if (key == parse->trailing_prefix) {
                    priority = 32U;
                } else if (key == parse->canonical_prefix) {
                    priority = 2U;
                }
                auto [found, inserted] = prefix_key_priorities.emplace(key, priority);
                if (!inserted) found->second = (std::max)(found->second, priority);
            }
        }
    }
    std::vector<std::string> prefix_keys;
    prefix_keys.reserve(prefix_key_priorities.size());
    for (const auto& [key, priority] : prefix_key_priorities) {
        (void)priority;
        prefix_keys.push_back(key);
    }
    std::sort(prefix_keys.begin(), prefix_keys.end());
    if (!prefix_keys.empty()) {
        std::vector<std::size_t> scan_limits(prefix_keys.size(), 0U);
        if (options.prefix_scan_limit >= prefix_keys.size()) {
            std::fill(scan_limits.begin(), scan_limits.end(), 1U);
            const std::size_t distributable =
                options.prefix_scan_limit - prefix_keys.size();
            std::size_t priority_total = 0U;
            for (const auto& key : prefix_keys) {
                priority_total += prefix_key_priorities.at(key);
            }
            std::size_t distributed = 0U;
            for (std::size_t index = 0U; index < prefix_keys.size(); ++index) {
                const std::size_t share = distributable *
                    prefix_key_priorities.at(prefix_keys[index]) / priority_total;
                scan_limits[index] += share;
                distributed += share;
            }
            std::vector<std::size_t> priority_order(prefix_keys.size());
            for (std::size_t index = 0U; index < priority_order.size(); ++index) {
                priority_order[index] = index;
            }
            std::stable_sort(
                priority_order.begin(), priority_order.end(),
                [&](const std::size_t left, const std::size_t right) {
                    return prefix_key_priorities.at(prefix_keys[left]) >
                        prefix_key_priorities.at(prefix_keys[right]);
                });
            std::size_t leftover = distributable - distributed;
            for (std::size_t index = 0U; index < leftover; ++index) {
                ++scan_limits[priority_order[index]];
            }
        }
        const std::size_t prefix_query_headroom = options.result_limit >
                (std::numeric_limits<std::size_t>::max)() / 4U
            ? (std::numeric_limits<std::size_t>::max)()
            : options.result_limit * 4U;
        for (std::size_t index = 0U; index < prefix_keys.size(); ++index) {
            const std::size_t query_scan_limit = scan_limits[index];
            if (query_scan_limit == 0U) {
                continue;
            }
            auto [cached, inserted] = prefix_cache.try_emplace(prefix_keys[index]);
            if (inserted) {
                if (stats != nullptr) {
                    ++stats->prefix_query_calls;
                }
                const std::size_t query_limit = (std::min)(
                    query_scan_limit, prefix_query_headroom);
                cached->second = prefix_query_(
                    prefix_keys[index], query_limit, query_scan_limit);
            }
        }
    }
    // The prefix-completion stage submits a path/word cross product, so the
    // lookup key is rebuilt thousands of times per keystroke. Reusing one
    // buffer keeps that off the allocator.
    std::string submit_key;
    auto submit = [&best, &submit_key, stats](IncrementalCandidate candidate) {
        if (stats != nullptr) {
            ++stats->submitted_candidates;
        }
        submit_key.clear();
        submit_key.append(candidate.word);
        submit_key.push_back('\n');
        submit_key.append(candidate.pinyin);
        const auto found = best.find(submit_key);
        if (found != best.end()) {
            if (candidate_better(candidate, found->second)) {
                found->second = std::move(candidate);
            }
            return;
        }
        best.emplace(submit_key, std::move(candidate));
    };

    for (std::size_t parse_index = 0U; parse_index < unique_parses.size(); ++parse_index) {
        const auto& parse = *unique_parses[parse_index];
        const std::size_t complete_count = parse.complete_syllables.size();
        if (parse.trailing_prefix.empty() && complete_count != 0U) {
            const std::string canonical = PinyinSegmenter::join(parse.complete_syllables);
            const auto cached = exact_cache.find(canonical);
            if (cached != exact_cache.end()) {
                const std::int64_t phrase_bonus =
                    complete_count > 1U ? exact_phrase_bonus : 0;
                for (const auto& word : cached->second) {
                    const bool single_character = utf8_codepoint_count(word.word) == 1U;
                    submit(IncrementalCandidate{
                        word.word,
                        word.pinyin,
                        word.weight,
                        frequency_score(word.weight) - character_rarity_penalty(word.word) +
                            (user_score_ ? user_score_(word.pinyin, word.word) : 0) +
                            scaled_parse_score(parse.parse_score) -
                            static_cast<std::int64_t>(parse_index * 20U) + phrase_bonus,
                        complete_count,
                        1U,
                        CandidateEvidence{
                            complete_count == 1U && single_character
                                ? CandidateKind::single_character
                                : CandidateKind::exact_lexicon,
                            complete_count,
                            1U,
                            single_character ? 1U : 0U,
                            true,
                        },
                    });
                }
            }
        }
        std::vector<std::vector<SentencePath>> states(complete_count + 1U);
        for (auto& state : states) {
            state.reserve((std::min)(state_beam_width, std::size_t{128U}));
        }
        states[0U].push_back(SentencePath{});
        std::vector<ScoredExactWord> scored_words;
        scored_words.reserve(exact_candidate_limit);

        for (std::size_t start = 0U; start < complete_count; ++start) {
            if (states[start].empty()) continue;
            if (stats != nullptr) {
                stats->max_source_paths = (std::max)(
                    stats->max_source_paths, states[start].size());
            }
            const std::size_t remaining = complete_count - start;
            const std::size_t max_end = start +
                (std::min)(options.max_word_syllables, remaining);
            for (std::size_t end = start + 1U; end <= max_end; ++end) {
                const std::string span = join_range(parse.complete_syllables, start, end);
                const auto [cached, inserted] = exact_cache.try_emplace(span);
                if (inserted) {
                    if (stats != nullptr) {
                        ++stats->exact_query_calls;
                    }
                    cached->second = exact_query_(span, exact_candidate_limit);
                }
                const auto& words = cached->second;
                auto& destination = states[end];
                const std::size_t graph_word_count =
                    (std::min)(words.size(), exact_candidate_limit);
                scored_words.clear();
                const std::size_t syllable_count = end - start;
                for (std::size_t word_index = 0U;
                     word_index < graph_word_count; ++word_index) {
                    const auto& word = words[word_index];
                    scored_words.push_back(ScoredExactWord{
                        &word,
                        frequency_score(word.weight) - token_penalty -
                            character_rarity_penalty(word.word) -
                            (end < complete_count && is_sentence_final_particle(word.word)
                                ? nonfinal_particle_penalty : 0) +
                            (user_score_ ? user_score_(span, word.word) : 0) +
                            static_cast<std::int64_t>(syllable_count - 1U) *
                                joined_syllable_bonus,
                    });
                }
                for (const auto& path : states[start]) {
                    for (const auto& scored_word : scored_words) {
                        const auto& word = *scored_word.word;
                        const std::uint64_t aggregate =
                            static_cast<std::uint64_t>(path.aggregate_weight) + word.weight;
                        const auto next_weight = static_cast<std::uint32_t>((std::min)(
                            aggregate,
                            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
                        const std::int64_t next_score = path.score + scored_word.edge_score;
                        // Concatenating the sentence text for every path/word
                        // pair dominated the keystroke budget at the real
                        // 90-candidate page size. Reject first, build second.
                        if (!path_can_enter(destination, next_score, next_weight,
                                path.word_count + 1U, state_beam_width)) {
                            continue;
                        }
                        SentencePath next = path;
                        next.text.append(word.word);
                        next.aggregate_weight = next_weight;
                        next.score = next_score;
                        ++next.word_count;
                        if (utf8_codepoint_count(word.word) == 1U) {
                            ++next.single_character_tokens;
                        }
                        insert_bounded_path(destination, std::move(next), state_beam_width);
                        if (stats != nullptr) {
                            stats->max_destination_paths = (std::max)(
                                stats->max_destination_paths, destination.size());
                        }
                    }
                }
            }
        }

        if (parse.trailing_prefix.empty()) {
            for (const auto& path : states.back()) {
                if (path.word_count == 0U) continue;
                submit(IncrementalCandidate{
                    path.text,
                    PinyinSegmenter::join(parse.complete_syllables),
                    path.aggregate_weight,
                    path.score + scaled_parse_score(parse.parse_score) -
                        static_cast<std::int64_t>(parse_index * 20U),
                    complete_count,
                    path.word_count,
                    CandidateEvidence{
                        CandidateKind::decoded_sentence,
                        complete_count,
                        path.word_count,
                        path.single_character_tokens,
                        true,
                    },
                });
            }
            continue;
        }
        if (!options.incomplete_candidates || options.prefix_scan_limit == 0U) {
            continue;
        }

        const std::size_t earliest = complete_count >= options.max_word_syllables - 1U
            ? complete_count - (options.max_word_syllables - 1U) : 0U;
        std::vector<RankedPrefixWord> ranked_prefix_words;
        for (std::size_t start = earliest; start <= complete_count; ++start) {
            if (states[start].empty()) continue;
            std::string key = join_range(parse.complete_syllables, start, complete_count);
            if (!key.empty()) key.push_back('\'');
            key.append(parse.trailing_prefix);
            const auto cached = prefix_cache.find(key);
            if (cached == prefix_cache.end()) {
                continue;
            }
            ranked_prefix_words.clear();
            ranked_prefix_words.reserve(cached->second.size());
            const std::size_t matched_syllable_count =
                complete_count - start + 1U;
            for (const auto& word : cached->second) {
                const auto word_syllable_count = matching_candidate_syllable_count(
                    word, parse.complete_syllables, start, parse.trailing_prefix);
                if (!word_syllable_count.has_value()) {
                    continue;
                }
                const std::size_t remaining_characters = word.pinyin.size() - key.size();
                const std::size_t untyped_syllables =
                    *word_syllable_count - matched_syllable_count;
                const std::size_t reopened_syllables =
                    start == 0U ? 0U : complete_count - start;
                ranked_prefix_words.push_back(RankedPrefixWord{
                    &word,
                    *word_syllable_count,
                    untyped_syllables,
                    reopened_syllables,
                    remaining_characters,
                    frequency_score(word.weight) - token_penalty -
                        character_rarity_penalty(word.word) +
                        (user_score_ ? user_score_(word.pinyin, word.word) : 0) +
                        static_cast<std::int64_t>(matched_syllable_count - 1U) *
                            joined_syllable_bonus,
                });
            }
            std::stable_sort(
                ranked_prefix_words.begin(), ranked_prefix_words.end(),
                [](const auto& left, const auto& right) {
                const std::int64_t left_score = left.edge_score -
                    untyped_syllable_cost(left.untyped_syllables) -
                    reopened_syllable_cost(left.reopened_syllables) -
                    static_cast<std::int64_t>(left.remaining_characters) *
                        completion_character_penalty;
                const std::int64_t right_score = right.edge_score -
                    untyped_syllable_cost(right.untyped_syllables) -
                    reopened_syllable_cost(right.reopened_syllables) -
                    static_cast<std::int64_t>(right.remaining_characters) *
                        completion_character_penalty;
                if (left_score != right_score) return left_score > right_score;
                if (left.word->weight != right.word->weight) {
                    return left.word->weight > right.word->weight;
                }
                if (left.word->word.size() != right.word->word.size()) {
                    return left.word->word.size() > right.word->word.size();
                }
                if (left.word->word != right.word->word) {
                    return left.word->word < right.word->word;
                }
                return left.word->pinyin < right.word->pinyin;
                });
            const std::size_t prefix_candidate_limit = (std::min)(
                options.result_limit, options.beam_width);
            if (ranked_prefix_words.size() > prefix_candidate_limit) {
                ranked_prefix_words.resize(prefix_candidate_limit);
            }
            // The consumed-syllable prefix only depends on `start`; building it
            // inside the path/word cross product repeated the same join and two
            // string allocations thousands of times per keystroke.
            std::string consumed_pinyin = join_range(parse.complete_syllables, 0U, start);
            if (start != 0U) consumed_pinyin.push_back('\'');
            std::string candidate_pinyin;
            const std::size_t terminal_path_limit = (std::min)(
                states[start].size(),
                (std::min)(state_beam_width, terminal_prefix_path_budget));
            for (const auto& ranked_word : ranked_prefix_words) {
                const auto& word = *ranked_word.word;
                candidate_pinyin.assign(consumed_pinyin);
                candidate_pinyin.append(word.pinyin);
                for (std::size_t path_index = 0U;
                     path_index < terminal_path_limit; ++path_index) {
                    const auto& path = states[start][path_index];
                    std::uint64_t aggregate = static_cast<std::uint64_t>(path.aggregate_weight) + word.weight;
                    aggregate = (std::min)(
                        aggregate, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
                    submit(IncrementalCandidate{
                        path.text + word.word,
                        candidate_pinyin,
                        static_cast<std::uint32_t>(aggregate),
                        path.score + ranked_word.edge_score + scaled_parse_score(parse.parse_score) -
                            static_cast<std::int64_t>(parse_index * 20U) - incomplete_input_penalty -
                            untyped_syllable_cost(ranked_word.untyped_syllables) -
                            reopened_syllable_cost(ranked_word.reopened_syllables) -
                            static_cast<std::int64_t>(ranked_word.remaining_characters) *
                                completion_character_penalty +
                            (path.word_count == 0U && matched_syllable_count > 1U
                                ? single_word_completion_bonus
                                : 0),
                        start + ranked_word.syllable_count,
                        path.word_count + 1U,
                        CandidateEvidence{
                            CandidateKind::incomplete_completion,
                            start + ranked_word.syllable_count,
                            path.word_count + 1U,
                            path.single_character_tokens +
                                (utf8_codepoint_count(word.word) == 1U ? 1U : 0U),
                            true,
                        },
                    });
                }
            }
        }
    }

    std::vector<IncrementalCandidate> results;
    results.reserve(best.size());
    for (auto& [key, candidate] : best) {
        (void)key;
        results.push_back(std::move(candidate));
    }
    std::sort(results.begin(), results.end(), candidate_better);
    if (results.size() > options.result_limit) results.resize(options.result_limit);
    if (stats != nullptr) {
        stats->exact_unique_keys = exact_cache.size();
        stats->prefix_unique_keys = prefix_cache.size();
        stats->result_count = results.size();
    }
    return results;
}

}  // namespace piinput
