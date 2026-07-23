#include "piinput/engine.h"
#include "piinput/full_pinyin_variants.h"
#include "piinput/incremental_decoder.h"
#include "piinput/pinyin_prefix.h"

#include <algorithm>
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

Engine::Engine()
    : prefix_query_cache_(std::make_shared<PrefixQueryCache>()) {}

Engine::Engine(const Engine& other)
    : prefix_query_cache_(std::make_shared<PrefixQueryCache>()) {
    if (!other.prefix_query_cache_) {
        user_model_ = other.user_model_;
        return;
    }
    std::shared_lock lock(other.prefix_query_cache_->state_mutex);
    lexicon_ = other.lexicon_;
    pinyin_ = other.pinyin_;
    shuangpin_ = other.shuangpin_;
    user_model_ = other.user_model_;
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
    const std::size_t scan_limit) const {
    const std::string cache_key = pinyin_prefix + "\n" +
        std::to_string(limit) + "\n" + std::to_string(scan_limit);
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
        result = tsv->query_prefix(pinyin_prefix, limit, scan_limit);
    } else if (const auto* binary = std::get_if<BinaryLexicon>(&lexicon_)) {
        result = binary->query_prefix(pinyin_prefix, limit, scan_limit);
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
    std::shared_lock state_lock(prefix_query_cache_->state_mutex);

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

    IncrementalDecoder::UserScore user_score;
    if (user_model_.entry_count() != 0U) {
        user_score = [this](const std::string_view pinyin, const std::string_view word) {
            return user_model_.score_adjustment(pinyin, word);
        };
    }
    IncrementalDecoder decoder(
        [this](const std::string_view pinyin, const std::size_t query_limit) {
            return query_exact_unlocked(std::string(pinyin), query_limit);
        },
        [this](const std::string_view prefix, const std::size_t query_limit, const std::size_t scan_limit) {
            return query_prefix_unlocked(std::string(prefix), query_limit, scan_limit);
        },
        std::move(user_score));
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
        if (left.word.size() != right.word.size()) {
            return left.word.size() > right.word.size();
        }
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
    if (!prefix_query_cache_) {
        return 0U;
    }
    return prefix_query_cache_->lexicon_entry_count.load();
}

const ShuangpinDecoder& Engine::shuangpin() const noexcept {
    return shuangpin_;
}

}  // namespace piinput
