#include "piinput/engine.h"
#include "piinput/full_pinyin_variants.h"
#include "piinput/incremental_decoder.h"
#include "piinput/pinyin_prefix.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace piinput {
namespace {

[[nodiscard]] bool is_full_pinyin_schema(const std::string_view schema) {
    return schema == "full" || schema == "full-pinyin" || schema == "pinyin";
}

struct FullPinyinDecodeResult {
    std::vector<std::string> variants;
    std::vector<PinyinSegmentation> segmentations;
};

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
        return shuangpin_.decode(schema, input, limit);
    }

    return decode_full_pinyin(input, settings, limit, pinyin_).segmentations;
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
    return query(input, schema, limit, default_settings());
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

    constexpr std::size_t parse_limit = 24U;
    std::vector<IncrementalParse> parses;
    std::unordered_set<std::string> seen_parses;
    bool merge_full_variant_words = false;
    auto add_parse = [&parses, &seen_parses](IncrementalParse parse) {
        const std::string key = parse.canonical_prefix + "\n" + parse.trailing_prefix;
        if (seen_parses.insert(key).second) parses.push_back(std::move(parse));
    };

    try {
        if (is_full_pinyin_schema(schema)) {
            const auto variants = normalize_full_pinyin_variants(input, settings.pinyin, parse_limit);
            merge_full_variant_words = variants.size() > 1U;
            for (const auto& variant : variants) {
                for (const auto& segmentation : pinyin_.segment(variant, parse_limit)) {
                    add_parse(IncrementalParse{
                        segmentation.syllables, {}, segmentation.canonical, segmentation.score});
                }
                if (!settings.pinyin.incomplete_candidates) continue;
                for (const auto& prefix : expand_input_prefix(
                         variant, schema, pinyin_, shuangpin_, parse_limit)) {
                    add_parse(IncrementalParse{
                        prefix.complete_syllables,
                        prefix.trailing_prefix,
                        prefix.canonical_prefix,
                        prefix.score,
                    });
                }
            }
        } else {
            for (const auto& segmentation : shuangpin_.decode(schema, input, parse_limit)) {
                add_parse(IncrementalParse{
                    segmentation.syllables, {}, segmentation.canonical, segmentation.score});
            }
            if (settings.pinyin.incomplete_candidates) {
                for (const auto& prefix : expand_input_prefix(
                         input, schema, pinyin_, shuangpin_, parse_limit)) {
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

    IncrementalDecoder decoder(
        [this](const std::string_view pinyin, const std::size_t query_limit) {
            return query_exact(std::string(pinyin), query_limit);
        },
        [this](const std::string_view prefix, const std::size_t query_limit, const std::size_t scan_limit) {
            return query_prefix(std::string(prefix), query_limit, scan_limit);
        },
        [this](const std::string_view pinyin, const std::string_view word) {
            return user_model_.score_adjustment(std::string(pinyin), std::string(word));
        });
    const auto decoded = decoder.decode(parses, IncrementalDecodeOptions{
        static_cast<std::size_t>(settings.pinyin.prefix_beam_width),
        static_cast<std::size_t>(settings.pinyin.prefix_scan_limit),
        result_limit,
        8U,
        settings.pinyin.incomplete_candidates,
    });

    std::unordered_map<std::string, EngineCandidate> best;
    auto better = [](const EngineCandidate& left, const EngineCandidate& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.base_weight != right.base_weight) return left.base_weight > right.base_weight;
        if (left.consumed_syllables != right.consumed_syllables) {
            return left.consumed_syllables > right.consumed_syllables;
        }
        if (left.word_count != right.word_count) return left.word_count < right.word_count;
        if (left.word != right.word) return left.word < right.word;
        return left.pinyin < right.pinyin;
    };
    for (const auto& candidate : decoded) {
        EngineCandidate converted{
            candidate.word,
            candidate.pinyin,
            candidate.base_weight,
            candidate.score,
            candidate.consumed_syllables,
            candidate.word_count,
        };
        const std::string key = merge_full_variant_words
            ? converted.word : converted.word + "\n" + converted.pinyin;
        const auto found = best.find(key);
        if (found == best.end() || better(converted, found->second)) {
            best[key] = std::move(converted);
        }
    }
    std::vector<EngineCandidate> results;
    results.reserve(best.size());
    for (auto& [key, candidate] : best) {
        (void)key;
        results.push_back(std::move(candidate));
    }
    std::sort(results.begin(), results.end(), better);
    if (results.size() > result_limit) results.resize(result_limit);
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

}  // namespace piinput
