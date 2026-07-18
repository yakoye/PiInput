#include "liteime/engine.h"
#include "liteime/pinyin_prefix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace liteime {
namespace {

constexpr std::int64_t exact_phrase_bonus = 30'000'000;
constexpr std::int64_t token_penalty = 12'000'000;
constexpr std::int64_t joined_syllable_bonus = 4'000'000;
constexpr std::int64_t incomplete_input_penalty = 8'000'000;
constexpr std::int64_t completion_character_penalty = 100'000;

[[nodiscard]] std::int64_t frequency_score(const std::uint32_t weight) {
    return static_cast<std::int64_t>(std::log1p(static_cast<double>(weight)) * 1'000'000.0);
}

}  // namespace

void Engine::load_lexicon(const std::filesystem::path& path) {
    if (is_binary_lexicon(path)) {
        BinaryLexicon lexicon;
        lexicon.load(path);
        lexicon_ = std::move(lexicon);
        return;
    }
    DevLexicon lexicon;
    lexicon.load_tsv(path);
    lexicon_ = std::move(lexicon);
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

std::vector<PinyinSegmentation> Engine::decode(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit) const {
    if (schema == "full" || schema == "full-pinyin" || schema == "pinyin") {
        return pinyin_.segment(input, limit);
    }
    return shuangpin_.decode(schema, input, limit);
}

std::vector<LexiconCandidate> Engine::query_exact(
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

std::vector<LexiconCandidate> Engine::query_prefix(
    const std::string& pinyin_prefix,
    const std::size_t limit,
    const std::size_t scan_limit) const {
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        return tsv->query_prefix(pinyin_prefix, limit, scan_limit);
    }
    if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        return binary->query_prefix(pinyin_prefix, limit, scan_limit);
    }
    throw std::runtime_error("No lexicon has been loaded");
}

std::vector<EngineCandidate> Engine::query(
    const std::string& input,
    const std::string& schema,
    const std::size_t limit) const {
    if (limit == 0U) {
        return {};
    }
    const auto segmentations = decode(input, schema, 24U);
    std::unordered_map<std::string, EngineCandidate> best;

    auto submit = [&best](EngineCandidate candidate) {
        const std::string key = candidate.word + "\n" + candidate.pinyin;
        const auto found = best.find(key);
        if (found == best.end() || candidate.score > found->second.score) {
            best[key] = std::move(candidate);
        }
    };

    if (segmentations.empty()) {
        const auto prefixes = expand_input_prefix(input, schema, pinyin_, shuangpin_, 24U);
        for (std::size_t prefix_index = 0U; prefix_index < prefixes.size(); ++prefix_index) {
            const auto& prefix = prefixes[prefix_index];
            const auto candidates = query_prefix(
                prefix.canonical_prefix,
                (std::max<std::size_t>)(limit * 16U, 64U),
                4096U);
            for (const auto& candidate : candidates) {
                const std::size_t remaining = candidate.pinyin.size() - prefix.canonical_prefix.size();
                submit(EngineCandidate{
                    candidate.word,
                    candidate.pinyin,
                    candidate.weight,
                    frequency_score(candidate.weight) +
                        user_model_.score_adjustment(candidate.pinyin, candidate.word) +
                        prefix.score - incomplete_input_penalty -
                        static_cast<std::int64_t>(remaining) * completion_character_penalty -
                        static_cast<std::int64_t>(prefix_index * 20U),
                });
            }
        }
    }

    for (std::size_t segmentation_index = 0U; segmentation_index < segmentations.size(); ++segmentation_index) {
        const auto& segmentation = segmentations[segmentation_index];
        const int segmentation_penalty = static_cast<int>(segmentation_index * 20U);

        const auto exact_candidates = query_exact(segmentation.canonical, limit * 4U);
        for (const auto& candidate : exact_candidates) {
            submit(EngineCandidate{
                candidate.word,
                candidate.pinyin,
                candidate.weight,
                frequency_score(candidate.weight) +
                    user_model_.score_adjustment(candidate.pinyin, candidate.word) +
                    segmentation.score - segmentation_penalty +
                    (segmentation.syllables.size() > 1U ? exact_phrase_bonus : 0),
            });
        }

        if (segmentation.syllables.size() <= 1U) {
            continue;
        }

        struct SentencePath {
            std::string text;
            std::uint32_t aggregate_weight{};
            std::int64_t score{};
        };
        std::vector<std::vector<SentencePath>> states(segmentation.syllables.size() + 1U);
        states[0U].push_back(SentencePath{});
        constexpr std::size_t max_word_syllables = 8U;
        constexpr std::size_t beam_width = 32U;

        for (std::size_t start_position = 0U; start_position < segmentation.syllables.size(); ++start_position) {
            if (states[start_position].empty()) {
                continue;
            }
            const std::size_t max_end = (std::min)(
                segmentation.syllables.size(),
                start_position + max_word_syllables);
            std::vector<std::string> span_syllables;
            for (std::size_t end_position = start_position + 1U; end_position <= max_end; ++end_position) {
                span_syllables.push_back(segmentation.syllables[end_position - 1U]);
                const std::string span_pinyin = PinyinSegmenter::join(span_syllables);
                const auto words = query_exact(span_pinyin, 8U);
                if (words.empty()) {
                    continue;
                }
                auto& destination = states[end_position];
                for (const auto& path : states[start_position]) {
                    for (const auto& word : words) {
                        SentencePath next = path;
                        next.text.append(word.word);
                        const std::uint64_t total_weight = static_cast<std::uint64_t>(next.aggregate_weight) + word.weight;
                        next.aggregate_weight = static_cast<std::uint32_t>((std::min)(
                            total_weight,
                            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
                        const std::size_t syllable_count = end_position - start_position;
                        next.score += frequency_score(word.weight) - token_penalty +
                            user_model_.score_adjustment(span_pinyin, word.word) +
                            static_cast<std::int64_t>(syllable_count - 1U) * joined_syllable_bonus;
                        destination.push_back(std::move(next));
                    }
                }
                std::stable_sort(destination.begin(), destination.end(), [](const SentencePath& left, const SentencePath& right) {
                    if (left.score != right.score) {
                        return left.score > right.score;
                    }
                    return left.text < right.text;
                });
                if (destination.size() > beam_width) {
                    destination.resize(beam_width);
                }
            }
        }

        for (const auto& path : states.back()) {
            submit(EngineCandidate{
                path.text,
                segmentation.canonical,
                path.aggregate_weight,
                path.score + segmentation.score - segmentation_penalty,
            });
        }
    }

    std::vector<EngineCandidate> results;
    results.reserve(best.size());
    for (auto& [key, candidate] : best) {
        (void)key;
        results.push_back(std::move(candidate));
    }
    std::stable_sort(results.begin(), results.end(), [](const EngineCandidate& left, const EngineCandidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.base_weight != right.base_weight) {
            return left.base_weight > right.base_weight;
        }
        if (left.word.size() != right.word.size()) {
            return left.word.size() > right.word.size();
        }
        if (left.word != right.word) {
            return left.word < right.word;
        }
        return left.pinyin < right.pinyin;
    });
    if (results.size() > limit) {
        results.resize(limit);
    }
    return results;
}

std::size_t Engine::entry_count() const noexcept {
    if (const auto* tsv = std::get_if<DevLexicon>(&lexicon_)) {
        return tsv->entry_count();
    }
    if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        return binary->entry_count();
    }
    return 0U;
}

const ShuangpinDecoder& Engine::shuangpin() const noexcept {
    return shuangpin_;
}

}  // namespace liteime
