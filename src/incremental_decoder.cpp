#include "piinput/incremental_decoder.h"
#include "piinput/pinyin.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

[[nodiscard]] std::int64_t frequency_score(const std::uint32_t weight) {
    return static_cast<std::int64_t>(std::log1p(static_cast<double>(weight)) * 1'000'000.0);
}

[[nodiscard]] std::vector<std::string_view> split_syllables(const std::string_view pinyin) {
    std::vector<std::string_view> result;
    std::size_t start = 0U;
    while (start <= pinyin.size()) {
        const std::size_t separator = pinyin.find('\'', start);
        const std::size_t end = separator == std::string_view::npos ? pinyin.size() : separator;
        if (end == start) {
            return {};
        }
        result.push_back(pinyin.substr(start, end - start));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1U;
    }
    return result;
}

[[nodiscard]] std::string join_range(
    const std::vector<std::string>& syllables,
    const std::size_t begin,
    const std::size_t end) {
    std::vector<std::string> range(
        syllables.begin() + static_cast<std::ptrdiff_t>(begin),
        syllables.begin() + static_cast<std::ptrdiff_t>(end));
    return PinyinSegmenter::join(range);
}

[[nodiscard]] bool candidate_matches_boundary(
    const LexiconCandidate& candidate,
    const std::vector<std::string>& complete,
    const std::size_t start,
    const std::string_view trailing) {
    const auto candidate_syllables = split_syllables(candidate.pinyin);
    static const std::unordered_set<std::string_view> standard_syllables(
        PinyinSegmenter::standard_syllables().begin(),
        PinyinSegmenter::standard_syllables().end());
    if (std::any_of(candidate_syllables.begin(), candidate_syllables.end(), [](const auto syllable) {
            return !standard_syllables.contains(syllable);
        })) {
        return false;
    }
    const std::size_t exact_count = complete.size() - start;
    if (trailing.empty() || candidate_syllables.size() < exact_count + 1U) {
        return false;
    }
    for (std::size_t index = 0U; index < exact_count; ++index) {
        if (candidate_syllables[index] != complete[start + index]) {
            return false;
        }
    }
    return candidate_syllables[exact_count].starts_with(trailing);
}

struct SentencePath {
    std::string text;
    std::uint32_t aggregate_weight{};
    std::int64_t score{};
    std::size_t word_count{};
};

[[nodiscard]] bool path_better(const SentencePath& left, const SentencePath& right) {
    if (left.score != right.score) return left.score > right.score;
    if (left.aggregate_weight != right.aggregate_weight) return left.aggregate_weight > right.aggregate_weight;
    if (left.word_count != right.word_count) return left.word_count < right.word_count;
    return left.text < right.text;
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
    std::vector<std::string> prefix_keys;
    if (options.incomplete_candidates && options.prefix_scan_limit != 0U) {
        for (const auto* parse : unique_parses) {
            if (parse->trailing_prefix.empty()) {
                continue;
            }
            for (auto& key : collect_prefix_keys(*parse)) {
                prefix_keys.push_back(std::move(key));
            }
        }
        std::sort(prefix_keys.begin(), prefix_keys.end());
        prefix_keys.erase(
            std::unique(prefix_keys.begin(), prefix_keys.end()), prefix_keys.end());
    }
    if (!prefix_keys.empty()) {
        const std::size_t scan_share = options.prefix_scan_limit / prefix_keys.size();
        const std::size_t scan_remainder = options.prefix_scan_limit % prefix_keys.size();
        const std::size_t prefix_candidate_limit = (std::min)(
            options.result_limit, options.beam_width);
        const std::size_t scan_headroom = prefix_candidate_limit >
                (std::numeric_limits<std::size_t>::max)() / 16U
            ? (std::numeric_limits<std::size_t>::max)()
            : prefix_candidate_limit * 16U;
        for (std::size_t index = 0U; index < prefix_keys.size(); ++index) {
            const std::size_t query_scan_limit = (std::min)(
                scan_share + (index < scan_remainder ? 1U : 0U),
                scan_headroom);
            if (query_scan_limit == 0U) {
                continue;
            }
            const std::size_t query_limit = (std::min)(
                prefix_candidate_limit, query_scan_limit);
            auto [cached, inserted] = prefix_cache.try_emplace(prefix_keys[index]);
            if (inserted) {
                if (stats != nullptr) {
                    ++stats->prefix_query_calls;
                }
                cached->second = prefix_query_(
                    prefix_keys[index], query_limit, query_scan_limit);
            }
        }
    }
    auto submit = [&best, stats](IncrementalCandidate candidate) {
        if (stats != nullptr) {
            ++stats->submitted_candidates;
        }
        const std::string key = candidate.word + "\n" + candidate.pinyin;
        const auto found = best.find(key);
        if (found == best.end() || candidate_better(candidate, found->second)) {
            best[key] = std::move(candidate);
        }
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
                    submit(IncrementalCandidate{
                        word.word,
                        word.pinyin,
                        word.weight,
                        frequency_score(word.weight) +
                            (user_score_ ? user_score_(word.pinyin, word.word) : 0) +
                            parse.parse_score -
                            static_cast<std::int64_t>(parse_index * 20U) + phrase_bonus,
                        complete_count,
                        1U,
                    });
                }
            }
        }
        std::vector<std::vector<SentencePath>> states(complete_count + 1U);
        states[0U].push_back(SentencePath{});

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
                for (const auto& path : states[start]) {
                    for (std::size_t word_index = 0U;
                         word_index < graph_word_count; ++word_index) {
                        const auto& word = words[word_index];
                        SentencePath next = path;
                        next.text.append(word.word);
                        const std::uint64_t aggregate = static_cast<std::uint64_t>(next.aggregate_weight) + word.weight;
                        next.aggregate_weight = static_cast<std::uint32_t>((std::min)(
                            aggregate, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
                        const std::size_t syllable_count = end - start;
                        next.score += frequency_score(word.weight) - token_penalty +
                            (user_score_ ? user_score_(span, word.word) : 0) +
                            static_cast<std::int64_t>(syllable_count - 1U) * joined_syllable_bonus;
                        ++next.word_count;
                        insert_bounded_path(destination, std::move(next), options.beam_width);
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
                    path.score + parse.parse_score -
                        static_cast<std::int64_t>(parse_index * 20U),
                    complete_count,
                    path.word_count,
                });
            }
            continue;
        }
        if (!options.incomplete_candidates || options.prefix_scan_limit == 0U) {
            continue;
        }

        const std::size_t earliest = complete_count >= options.max_word_syllables - 1U
            ? complete_count - (options.max_word_syllables - 1U) : 0U;
        for (std::size_t start = earliest; start <= complete_count; ++start) {
            if (states[start].empty()) continue;
            std::string key = join_range(parse.complete_syllables, start, complete_count);
            if (!key.empty()) key.push_back('\'');
            key.append(parse.trailing_prefix);
            const auto cached = prefix_cache.find(key);
            if (cached == prefix_cache.end()) {
                continue;
            }
            const auto& words = cached->second;
            for (const auto& word : words) {
                if (!candidate_matches_boundary(word, parse.complete_syllables, start, parse.trailing_prefix)) {
                    continue;
                }
                const auto word_syllables = split_syllables(word.pinyin);
                const std::size_t remaining_characters = word.pinyin.size() - key.size();
                const std::size_t terminal_path_limit = (std::min)(
                    states[start].size(),
                    (std::min)(options.result_limit, options.beam_width));
                for (std::size_t path_index = 0U;
                     path_index < terminal_path_limit; ++path_index) {
                    const auto& path = states[start][path_index];
                    std::uint64_t aggregate = static_cast<std::uint64_t>(path.aggregate_weight) + word.weight;
                    aggregate = (std::min)(
                        aggregate, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
                    const std::int64_t edge_score = frequency_score(word.weight) - token_penalty +
                        (user_score_ ? user_score_(word.pinyin, word.word) : 0) +
                        static_cast<std::int64_t>(word_syllables.size() - 1U) * joined_syllable_bonus;
                    submit(IncrementalCandidate{
                        path.text + word.word,
                        join_range(parse.complete_syllables, 0U, start) +
                            (start == 0U ? std::string{} : std::string{"'"}) + word.pinyin,
                        static_cast<std::uint32_t>(aggregate),
                        path.score + edge_score + parse.parse_score -
                            static_cast<std::int64_t>(parse_index * 20U) - incomplete_input_penalty -
                            static_cast<std::int64_t>(remaining_characters) * completion_character_penalty,
                        start + word_syllables.size(),
                        path.word_count + 1U,
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
