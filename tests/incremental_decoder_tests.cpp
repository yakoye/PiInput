#include "piinput/engine.h"
#include "piinput/full_pinyin_variants.h"
#include "piinput/incremental_decoder.h"
#include "piinput/pinyin_prefix.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains_word(
    const std::vector<piinput::EngineCandidate>& candidates,
    const std::string& word) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.word == word;
    });
}

[[nodiscard]] std::size_t syllable_count(const std::string_view canonical) {
    if (canonical.empty()) {
        return 0U;
    }
    return 1U + static_cast<std::size_t>(
        std::count(canonical.begin(), canonical.end(), '\''));
}

[[nodiscard]] bool candidate_matches_input_prefix(
    const piinput::EngineCandidate& candidate,
    const std::string& input,
    const std::string& schema,
    const piinput::Engine& engine) {
    if (candidate.consumed_syllables != syllable_count(candidate.pinyin)) {
        return false;
    }
    std::vector<std::string> variants{input};
    if (schema == "full") {
        variants = piinput::normalize_full_pinyin_variants(
            input, piinput::PinyinSettings{}, 128U);
    }
    for (const auto& variant : variants) {
        const auto exact = engine.decode(variant, schema, 128U);
        if (std::any_of(exact.begin(), exact.end(), [&](const auto& parse) {
                const bool canonical_boundary =
                    candidate.pinyin == parse.canonical ||
                    (candidate.pinyin.starts_with(parse.canonical) &&
                        candidate.pinyin.size() > parse.canonical.size() &&
                        candidate.pinyin[parse.canonical.size()] == '\'');
                return canonical_boundary &&
                    candidate.consumed_syllables >= parse.syllables.size();
            })) {
            return true;
        }
        const auto prefixes = piinput::expand_input_prefix(
            variant, schema, piinput::PinyinSegmenter{}, engine.shuangpin(), 128U);
        if (std::any_of(prefixes.begin(), prefixes.end(), [&](const auto& prefix) {
                const std::size_t minimum_consumed =
                    prefix.complete_syllables.size() +
                    (prefix.trailing_prefix.empty() ? 0U : 1U);
                return candidate.pinyin.starts_with(prefix.canonical_prefix) &&
                    candidate.consumed_syllables >= minimum_consumed;
            })) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::filesystem::path write_lexicon() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-incremental-decoder.tsv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "word\tpinyin\tweight\n"
           << "明\tming\t900\n"
           << "天\ttian\t900\n"
           << "明天\tming'tian\t1800\n"
           << "明天上午\tming'tian'shang'wu\t1800\n"
           << "如\tru\t850\n"
           << "果\tguo\t850\n"
           << "如果\tru'guo\t1700\n"
           << "如果能不断地\tru'guo'neng'bu'duan'de\t1700\n"
           << "比如\tbi'ru\t1600\n"
           << "我\two\t1000\n"
           << "今天\tjin'tian\t1600\n"
           << "下午\txia'wu\t1500\n"
           << "要\tyao\t1000\n"
           << "是\tshi\t1000\n"
           << "不\tbu\t1000\n"
           << "知\tzhi\t1000\n"
           << "知道\tzhi'dao\t1500\n"
           << "真诚地恋爱\tzhen'cheng'de'lian'ai\t1500\n"
           << "去\tqu\t900\n"
           << "超市\tchao'shi\t1500\n"
           << "买\tmai\t900\n"
           << "点\tdian\t900\n"
           << "水果\tshui'guo\t1500\n"
           << "举个例子\tju'ge'li'zi\t2200\n"
           << "举\tju\t800\n"
           << "个\tge\t800\n"
           << "例子\tli'zi\t1400\n"
           << "固件\tgu'jian\t1500\n"
           << "开发\tkai'fa\t1500\n"
           << "需要\txu'yao\t1500\n"
           << "熟悉\tshu'xi\t1500\n"
           << "底层\tdi'ceng\t1500\n"
           << "寄存器\tji'cun'qi\t1600\n"
           << "配置\tpei'zhi\t1500\n"
           << "和\the\t1000\n"
           << "链路\tlian'lu\t1500\n"
           << "状态机\tzhuang'tai'ji\t1600\n"
           << "西安\txi'an\t1200\n"
           << "先\txian\t1200\n"
           << "错误边界\tming'tianx\t9999\n";
    return path;
}

void test_public_parse_contract() {
    static_assert(std::is_copy_constructible_v<piinput::Engine>);
    static_assert(std::is_copy_assignable_v<piinput::Engine>);
    static_assert(std::is_nothrow_move_constructible_v<piinput::Engine>);
    static_assert(std::is_nothrow_move_assignable_v<piinput::Engine>);
    const piinput::IncrementalParse parse{{"ming"}, "t", "ming't", 42};
    check(parse.complete_syllables.size() == 1U, "parse exposes complete syllables");
    check(parse.trailing_prefix == "t" && parse.canonical_prefix == "ming't",
        "parse exposes trailing and canonical prefixes");
    check(parse.parse_score == 42, "parse exposes score");
    const piinput::IncrementalDecodeOptions defaults;
    check(defaults.beam_width == 32U && defaults.prefix_scan_limit == 4096U &&
        defaults.result_limit == 90U && defaults.max_word_syllables == 8U,
        "incremental options have documented defaults");
}

void test_query_scope_cache_and_canonical_parse_deduplication() {
    std::size_t exact_calls = 0U;
    std::size_t prefix_calls = 0U;
    std::size_t prefix_scan_budget = 0U;
    std::vector<std::string> prefix_callback_order;
    std::vector<std::size_t> prefix_scan_limits;
    std::unordered_set<std::string> exact_keys;
    std::unordered_set<std::string> prefix_keys;
    piinput::IncrementalDecoder decoder(
        [&](const std::string_view key, const std::size_t) {
            ++exact_calls;
            exact_keys.emplace(key);
            if (key == "ming") {
                return std::vector<piinput::LexiconCandidate>{{"明", "ming", 900U}};
            }
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view key, const std::size_t, const std::size_t scan_limit) {
            ++prefix_calls;
            prefix_scan_budget += scan_limit;
            prefix_callback_order.emplace_back(key);
            prefix_scan_limits.push_back(scan_limit);
            prefix_keys.emplace(key);
            if (key == "ming't" || key == "ming'ti") {
                return std::vector<piinput::LexiconCandidate>{{"明天", "ming'tian", 1800U}};
            }
            if (key == "t" || key == "ti") {
                return std::vector<piinput::LexiconCandidate>{{"天", "tian", 900U}};
            }
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    const std::vector<piinput::IncrementalParse> parses{
        {{"ming"}, "t", "ming't", 40},
        {{"ming"}, "t", "ming't", 20},
        {{"ming"}, "ti", "ming'ti", 30},
    };
    piinput::IncrementalDecodeStats stats;
    const auto results = decoder.decode(parses, {}, &stats);
    check(!results.empty(), "cache fixture produces candidates");
    check(exact_calls == exact_keys.size(),
        "each exact key is queried once per decode (calls=" + std::to_string(exact_calls) +
            ", unique=" + std::to_string(exact_keys.size()) + ")");
    check(prefix_calls == prefix_keys.size(),
        "each prefix key is queried once per decode (calls=" + std::to_string(prefix_calls) +
            ", unique=" + std::to_string(prefix_keys.size()) + ")");
    check(stats.input_parse_count == 3U && stats.unique_parse_count == 2U,
        "canonical duplicate parses are removed");
    check(stats.exact_query_calls == stats.exact_unique_keys &&
        stats.prefix_query_calls == stats.prefix_unique_keys,
        "decode statistics report one callback per unique query key");
    check(prefix_scan_budget <= piinput::IncrementalDecodeOptions{}.prefix_scan_limit,
        "prefix scan budget is shared by the whole decode (budget=" +
            std::to_string(prefix_scan_budget) + ")");
    check(std::is_sorted(prefix_callback_order.begin(), prefix_callback_order.end()),
        "prefix keys are queried in deterministic sorted order");
    check(std::all_of(prefix_scan_limits.begin(), prefix_scan_limits.end(), [](const auto value) {
        return value != 0U;
    }), "each prefix key receives a nonzero scan share when the budget is sufficient");
    check(std::all_of(prefix_scan_limits.begin(), prefix_scan_limits.end(), [](const auto value) {
        return value <= piinput::IncrementalDecodeOptions{}.prefix_scan_limit;
    }), "each prefix key scan stays within the shared scan budget");
}

void test_destination_paths_never_exceed_beam() {
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t) {
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U; index < 8U; ++index) {
                    words.push_back({
                        "候选" + std::to_string(index),
                        "a",
                        static_cast<std::uint32_t>(100U - index),
                    });
                }
            } else if (key == "ba") {
                words.push_back({"吧", "ba", 100U});
            }
            return words;
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.beam_width = 3U;
    piinput::IncrementalDecodeStats stats;
    const auto results = decoder.decode(
        {{{"a", "ba"}, {}, "a'ba", 0}}, options, &stats);
    check(results.size() == options.beam_width, "beam retains only configured path count");
    check(stats.max_source_paths <= options.beam_width &&
        stats.max_destination_paths <= options.beam_width,
        "source and destination path counts obey the hard beam boundary (source=" +
            std::to_string(stats.max_source_paths) + ", destination=" +
            std::to_string(stats.max_destination_paths) + ")");
}

void test_result_limit_bounds_effective_beam() {
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t) {
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U; index < 8U; ++index) {
                    words.push_back({
                        "候选" + std::to_string(index),
                        "a",
                        static_cast<std::uint32_t>(100U - index),
                    });
                }
            } else if (key == "ba") {
                words.push_back({"吧", "ba", 100U});
            }
            return words;
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.beam_width = 32U;
    options.result_limit = 3U;
    piinput::IncrementalDecodeStats stats;
    (void)decoder.decode(
        {{{"a", "ba"}, {}, "a'ba", 0}}, options, &stats);
    check(stats.max_source_paths <= options.result_limit &&
        stats.max_destination_paths <= options.result_limit,
        "result limit bounds the effective beam without weakening the configured maximum");
}

void test_exact_word_edge_score_is_computed_once_per_span() {
    std::size_t tail_score_calls = 0U;
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t) {
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U; index < 8U; ++index) {
                    words.push_back({
                        "前" + std::to_string(index),
                        "a",
                        static_cast<std::uint32_t>(100U - index),
                    });
                }
            } else if (key == "ba") {
                words.push_back({"尾", "ba", 100U});
            }
            return words;
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view pinyin, const std::string_view word) {
            if (pinyin == "ba" && word == "尾") {
                ++tail_score_calls;
            }
            return 0;
        });
    const auto results = decoder.decode(
        {{{"a", "ba"}, {}, "a'ba", 0}}, {});
    check(results.size() == 8U, "exact edge score fixture keeps all source paths");
    check(tail_score_calls == 1U,
        "exact span-word edge score is computed once before Cartesian expansion "
        "(calls=" + std::to_string(tail_score_calls) + ")");
}

void test_terminal_prefix_cross_product_is_result_bounded() {
    std::size_t exact_requested_limit = 0U;
    piinput::IncrementalDecoder decoder(
        [&](const std::string_view key, const std::size_t limit) {
            exact_requested_limit = (std::max)(exact_requested_limit, limit);
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U; index < limit; ++index) {
                    words.push_back({
                        "前" + std::to_string(index),
                        "a",
                        static_cast<std::uint32_t>(1000U - index),
                    });
                }
            }
            return words;
        },
        [](const std::string_view key, const std::size_t limit, const std::size_t) {
            std::vector<piinput::LexiconCandidate> words;
            for (std::size_t index = 0U; index < limit; ++index) {
                words.push_back({
                    "尾" + std::to_string(index),
                    key == "b" ? "ba" : "a'ba",
                    static_cast<std::uint32_t>(1000U - index),
                });
            }
            return words;
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.beam_width = 32U;
    options.result_limit = 3U;
    piinput::IncrementalDecodeStats stats;
    const auto results = decoder.decode(
        {{{"a"}, "b", "a'b", 0}}, options, &stats);
    check(results.size() == options.result_limit,
        "terminal prefix fixture respects result limit");
    check(stats.submitted_candidates <= 18U,
        "terminal prefix combinations are bounded by result limit (submitted=" +
            std::to_string(stats.submitted_candidates) + ")");
    check(exact_requested_limit <= 8U,
        "exact edge query limit is result-bounded (requested=" +
            std::to_string(exact_requested_limit) + ")");
}

void test_terminal_prefix_cap_matches_exhaustive_oracle() {
    const std::vector<std::vector<std::uint32_t>> source_weight_matrices{
        {1000U, 900U, 900U, 800U, 700U, 600U, 500U, 400U},
        {1000U, 1000U, 1000U, 1000U, 900U, 900U, 800U, 800U},
        {800U, 1000U, 700U, 900U, 600U, 500U, 400U, 300U},
    };
    for (std::size_t matrix = 0U; matrix < source_weight_matrices.size(); ++matrix) {
        const auto& source_weights = source_weight_matrices[matrix];
        auto make_decoder = [&]() {
            return piinput::IncrementalDecoder(
                [&](const std::string_view key, const std::size_t) {
                    std::vector<piinput::LexiconCandidate> words;
                    if (key == "a") {
                        for (std::size_t index = 0U; index < source_weights.size(); ++index) {
                            words.push_back({
                                "源" + std::to_string(index),
                                "a",
                                source_weights[index],
                            });
                        }
                        words.push_back({"源1", "a", 100U});
                    }
                    return words;
                },
                [](const std::string_view key, const std::size_t, const std::size_t) {
                    if (key != "b") {
                        return std::vector<piinput::LexiconCandidate>{};
                    }
                    return std::vector<piinput::LexiconCandidate>{
                        {"尾0", "ba", 1000U},
                        {"尾1", "ba", 1000U},
                        {"尾2", "ba", 900U},
                    };
                },
                [](const std::string_view, const std::string_view word) {
                    return word.ends_with("2") ? 50 : 0;
                });
        };
        piinput::IncrementalDecodeOptions capped_options;
        capped_options.beam_width = 8U;
        capped_options.result_limit = 3U;
        const auto capped = make_decoder().decode(
            {{{"a"}, "b", "a'b", 0}}, capped_options);

        auto exhaustive_options = capped_options;
        exhaustive_options.result_limit = 64U;
        auto exhaustive = make_decoder().decode(
            {{{"a"}, "b", "a'b", 0}}, exhaustive_options);
        if (exhaustive.size() > capped_options.result_limit) {
            exhaustive.resize(capped_options.result_limit);
        }
        bool matches = capped.size() == exhaustive.size();
        for (std::size_t index = 0U; matches && index < capped.size(); ++index) {
            matches = capped[index].word == exhaustive[index].word &&
                capped[index].pinyin == exhaustive[index].pinyin &&
                capped[index].base_weight == exhaustive[index].base_weight &&
                capped[index].score == exhaustive[index].score &&
                capped[index].word_count == exhaustive[index].word_count;
        }
        check(matches, "terminal prefix cap matches exhaustive top-N oracle matrix " +
            std::to_string(matrix));
    }
}

void test_complete_exact_keeps_user_score_headroom() {
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t limit) {
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U;
                     index < (std::min)(limit, std::size_t{40U}); ++index) {
                    words.push_back({
                        "候选" + std::to_string(index),
                        "a",
                        static_cast<std::uint32_t>(1000U - index),
                    });
                }
            }
            return words;
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [](const std::string_view, const std::string_view word) {
            return word == "候选33" ? 100'000'000 : 0;
        });
    piinput::IncrementalDecodeOptions options;
    options.result_limit = 10U;
    const auto results = decoder.decode(
        {{{"a"}, {}, "a", 0}}, options);
    check(std::any_of(results.begin(), results.end(), [](const auto& candidate) {
        return candidate.word == "候选33";
    }), "complete exact query retains headroom for a learned candidate below rank 32");
}

void test_incomplete_prefix_keeps_user_score_headroom() {
    std::size_t requested_limit = 0U;
    piinput::IncrementalDecoder decoder(
        [](const std::string_view, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view key, const std::size_t limit, const std::size_t) {
            requested_limit = limit;
            std::vector<piinput::LexiconCandidate> words;
            if (key == "a") {
                for (std::size_t index = 0U;
                     index < (std::min)(limit, std::size_t{40U}); ++index) {
                    words.push_back({
                        "前缀候选" + std::to_string(index),
                        "ai",
                        static_cast<std::uint32_t>(1000U - index),
                    });
                }
            }
            return words;
        },
        [](const std::string_view, const std::string_view word) {
            return word == "前缀候选33" ? 100'000'000 : 0;
        });
    piinput::IncrementalDecodeOptions options;
    options.result_limit = 10U;
    piinput::IncrementalDecodeStats stats;
    const auto results = decoder.decode(
        {{{}, "a", "a", 0}}, options, &stats);
    check(requested_limit >= 34U,
        "incomplete prefix query retains candidate headroom before user scoring");
    check(!results.empty() && results.front().word == "前缀候选33",
        "learned incomplete prefix candidate below rank 32 can become first");
    check(stats.submitted_candidates <= options.result_limit * 16U,
        "learned prefix headroom keeps terminal submissions result-bounded");
}

void test_prefix_scan_budget_is_independent_from_output_headroom() {
    std::size_t requested_limit = 0U;
    std::size_t requested_scan_limit = 0U;
    piinput::IncrementalDecoder decoder(
        [](const std::string_view, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view key, const std::size_t limit, const std::size_t scan_limit) {
            requested_limit = limit;
            requested_scan_limit = scan_limit;
            if (key == "r" && scan_limit >= 512U) {
                return std::vector<piinput::LexiconCandidate>{{"如果", "ru'guo", 1000U}};
            }
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.result_limit = 10U;
    options.prefix_scan_limit = 4096U;
    const auto results = decoder.decode(
        {{{}, "r", "r", 0}}, options);
    check(requested_limit == 40U,
        "prefix output retrieval keeps bounded learning headroom");
    check(requested_scan_limit == options.prefix_scan_limit,
        "single prefix key receives the configured shared scan budget");
    check(!results.empty() && results.front().word == "如果",
        "target beyond output headroom remains discoverable through the scan budget");
}

void test_direct_exact_ties_prefer_longer_words_then_dictionary_order() {
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t) {
            if (key == "a") {
                return std::vector<piinput::LexiconCandidate>{
                    {"安", "a", 1000U},
                    {"长长", "a", 1000U},
                    {"长安", "a", 1000U},
                };
            }
            return std::vector<piinput::LexiconCandidate>{};
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    const auto results = decoder.decode(
        {{{"a"}, {}, "a", 0}}, {});
    check(results.size() >= 3U, "direct exact tie fixture returns all candidates");
    if (results.size() >= 3U) {
        check(results[0U].word == "长安" &&
            results[1U].word == "长长" &&
            results[2U].word == "安",
            "direct exact ties sort by score, weight, word length, then dictionary order");
    }
}

void test_engine_direct_exact_tie_compatibility() {
    const auto path =
        std::filesystem::temp_directory_path() / "piinput-direct-exact-order.tsv";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "安\ta\t1000\n"
               << "长长\ta\t1000\n"
               << "长安\ta\t1000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(path);
    const auto results = engine.query("a", "full", 10U);
    check(results.size() >= 3U, "engine direct exact tie fixture returns all candidates");
    if (results.size() >= 3U) {
        check(results[0U].word == "长安" &&
            results[1U].word == "长长" &&
            results[2U].word == "安",
            "engine preserves direct exact word-length and dictionary-order ties");
    }
    std::filesystem::remove(path);
}

void test_prefix_query_cache_is_invalidated_by_lexicon_reload() {
    const auto old_path =
        std::filesystem::temp_directory_path() / "piinput-prefix-cache-old.tsv";
    const auto new_path =
        std::filesystem::temp_directory_path() / "piinput-prefix-cache-new.tsv";
    const auto learned_path =
        std::filesystem::temp_directory_path() / "piinput-prefix-cache-learned.tsv";
    auto write_entry = [](const std::filesystem::path& path, const std::string& word) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << word << "\tming\t1000\n";
        for (std::size_t index = 0U; index < 256U; ++index) {
            output << word << "前" << index << "\tming\t900\n"
                   << word << "后" << index << "\ttian\t900\n";
        }
    };
    write_entry(old_path, "旧");
    write_entry(new_path, "新");
    {
        std::ofstream output(learned_path, std::ios::binary | std::ios::trunc);
        output << "word\tpinyin\tweight\n"
               << "甲\tming\t1000\n"
               << "乙\tming\t1000\n";
    }
    piinput::Engine engine;
    engine.load_lexicon(old_path);
    check(contains_word(engine.query("m", "full", 10U), "旧"),
        "prefix query cache fixture primes the first lexicon");
    piinput::Engine copied_engine = engine;
    engine.load_lexicon(new_path);
    const auto reloaded = engine.query("m", "full", 10U);
    check(contains_word(reloaded, "新") && !contains_word(reloaded, "旧"),
        "lexicon reload invalidates cached prefix query results");
    const auto copied = copied_engine.query("m", "full", 10U);
    check(contains_word(copied, "旧") && !contains_word(copied, "新"),
        "copied engines keep independent prefix cache and lexicon state");

    piinput::Engine moved_engine(std::move(copied_engine));
    check(copied_engine.entry_count() == 0U &&
        copied_engine.query("m", "full", 10U).empty(),
        "move construction leaves the source Engine safely empty");
    check(contains_word(moved_engine.query("m", "full", 10U), "旧"),
        "move construction transfers the lexicon and cache");
    copied_engine.record_selection("ming", "甲");
    piinput::Engine copied_empty = copied_engine;
    check(copied_empty.entry_count() == 0U &&
        copied_empty.query("m", "full", 10U).empty(),
        "copying a moved-from Engine produces a safely empty Engine");
    copied_empty.load_lexicon(learned_path);
    const auto copied_learned = copied_empty.query("ming", "full", 10U);
    check(!copied_learned.empty() && copied_learned.front().word == "甲",
        "copying a moved-from Engine preserves newly recorded learning");
    copied_engine.load_lexicon(learned_path);
    const auto learned = copied_engine.query("ming", "full", 10U);
    check(!learned.empty() && learned.front().word == "甲",
        "loading a lexicon into a moved-from Engine preserves newly recorded learning");
    copied_engine.load_lexicon(new_path);
    check(contains_word(copied_engine.query("m", "full", 10U), "新"),
        "moved-from Engine can load and query a new lexicon");

    piinput::Engine move_assigned;
    move_assigned = std::move(moved_engine);
    check(moved_engine.entry_count() == 0U &&
        moved_engine.query("m", "full", 10U).empty(),
        "move assignment leaves the source Engine safely empty");
    check(contains_word(move_assigned.query("m", "full", 10U), "旧"),
        "move assignment transfers the lexicon and cache");

    std::atomic<bool> reload_done{false};
    std::atomic<bool> concurrent_results_valid{true};
    std::atomic<std::size_t> concurrent_queries{0U};
    std::thread reader([&] {
        while (!reload_done.load()) {
            const auto candidates = engine.query("mingtian", "full", 30U);
            const bool mixes_generations = std::any_of(
                candidates.begin(), candidates.end(), [](const auto& candidate) {
                    return candidate.word.find("旧") != std::string::npos &&
                        candidate.word.find("新") != std::string::npos;
                });
            if (candidates.empty() || mixes_generations) {
                concurrent_results_valid.store(false);
            }
            ++concurrent_queries;
        }
    });
    while (concurrent_queries.load() == 0U) {
        std::this_thread::yield();
    }
    for (std::size_t reload = 0U; reload < 200U; ++reload) {
        engine.load_lexicon(reload % 2U == 0U ? old_path : new_path);
    }
    reload_done.store(true);
    reader.join();
    check(concurrent_queries.load() != 0U && concurrent_results_valid.load(),
        "each multi-edge query stays within one atomically published lexicon generation");

    std::filesystem::remove(old_path);
    std::filesystem::remove(new_path);
    std::filesystem::remove(learned_path);
}

void test_low_scan_budget_retains_complete_prefix_parses() {
    std::vector<piinput::IncrementalParse> parses;
    for (std::size_t parse_index = 0U; parse_index < 24U; ++parse_index) {
        std::vector<std::string> syllables;
        for (std::size_t syllable = 0U; syllable < 7U; ++syllable) {
            syllables.push_back(
                "p" + std::to_string(parse_index) + "_" + std::to_string(syllable));
        }
        parses.push_back({
            std::move(syllables),
            "t",
            "parse-" + std::to_string(parse_index),
            static_cast<int>(100U - parse_index),
        });
    }
    std::size_t total_scan_budget = 0U;
    std::size_t minimum_scan_share = (std::numeric_limits<std::size_t>::max)();
    piinput::IncrementalDecoder decoder(
        [](const std::string_view, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view, const std::size_t, const std::size_t scan_limit) {
            total_scan_budget += scan_limit;
            minimum_scan_share = (std::min)(minimum_scan_share, scan_limit);
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.prefix_scan_limit = 128U;
    piinput::IncrementalDecodeStats stats;
    (void)decoder.decode(parses, options, &stats);
    check(stats.unique_parse_count == 24U,
        "low-scan fixture begins with all canonical parses");
    check(stats.retained_parse_count >= 16U &&
        stats.retained_parse_count < stats.unique_parse_count,
        "low scan budget deterministically retains only fully funded prefix parses");
    check(total_scan_budget <= options.prefix_scan_limit && minimum_scan_share >= 1U,
        "each retained prefix key has nonzero scan without exceeding total budget");
}

void test_low_scan_budget_keeps_short_parses_that_fit() {
    std::vector<piinput::IncrementalParse> parses;
    for (std::size_t parse_index = 0U; parse_index < 24U; ++parse_index) {
        parses.push_back({
            {"short-" + std::to_string(parse_index)},
            "t",
            "short-parse-" + std::to_string(parse_index),
            static_cast<int>(100U - parse_index),
        });
    }
    std::size_t total_scan_budget = 0U;
    piinput::IncrementalDecoder decoder(
        [](const std::string_view, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        [&](const std::string_view, const std::size_t, const std::size_t scan_limit) {
            total_scan_budget += scan_limit;
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.prefix_scan_limit = 128U;
    piinput::IncrementalDecodeStats stats;
    (void)decoder.decode(parses, options, &stats);
    check(stats.unique_parse_count == 24U && stats.retained_parse_count == 24U,
        "all short prefix parses are retained when their actual keys fit the scan budget");
    check(total_scan_budget <= options.prefix_scan_limit,
        "short-parse callbacks stay within the shared scan budget");
}

void test_max_word_syllable_limit_does_not_overflow() {
    piinput::IncrementalDecoder decoder(
        [](const std::string_view key, const std::size_t) {
            if (key == "a") {
                return std::vector<piinput::LexiconCandidate>{{"阿", "a", 100U}};
            }
            if (key == "ba") {
                return std::vector<piinput::LexiconCandidate>{{"吧", "ba", 100U}};
            }
            return std::vector<piinput::LexiconCandidate>{};
        },
        [](const std::string_view, const std::size_t, const std::size_t) {
            return std::vector<piinput::LexiconCandidate>{};
        },
        {});
    piinput::IncrementalDecodeOptions options;
    options.max_word_syllables = (std::numeric_limits<std::size_t>::max)();
    check(!decoder.decode({{{"a", "ba"}, {}, "a'ba", 0}}, options).empty(),
        "SIZE_MAX max_word_syllables still expands exact edges");
}

void test_cross_start_prefix_and_complete_input(piinput::Engine& engine) {
    const auto flypy_mingtian = engine.query("mkt", "flypy", 20U);
    check(contains_word(flypy_mingtian, "明天"),
        "Flypy mkt completes a word spanning start zero");
    check(!flypy_mingtian.empty() && flypy_mingtian.front().word == "明天",
        "Flypy mkt prefers the shortest reasonable completion");
    const auto flypy_ruguo = engine.query("rug", "flypy", 20U);
    check(contains_word(flypy_ruguo, "如果"),
        "Flypy rug completes 如果");
    check(!flypy_ruguo.empty() && flypy_ruguo.front().word == "如果",
        "Flypy rug does not reward untyped future syllables");
    check(contains_word(engine.query("mingt", "full", 20U), "明天"),
        "full pinyin mingt completes 明天");
    check(contains_word(engine.query("rug", "full", 20U), "如果"),
        "full pinyin rug completes 如果");
    check(contains_word(engine.query("mingtian", "full", 20U), "明天"),
        "complete full pinyin still decodes");
}

void test_arbitrary_length_and_variants(piinput::Engine& engine) {
    const auto long_partial = engine.query("biruwoycuibuv", "flypy", 30U);
    check(!long_partial.empty(), "long Flypy partial has candidates");
    check(std::all_of(long_partial.begin(), long_partial.end(), [](const auto& candidate) {
        return candidate.word.starts_with("比如我要是不");
    }), "long partial candidates consume the complete sentence prefix");
    check(!long_partial.empty() && long_partial.front().word == "比如我要是不知道",
        "long partial prefers a concise completion over untyped future syllables");
    check(contains_word(engine.query("jvgeliz", "full", 20U), "举个例子"),
        "full pinyin v variant completes 举个例子");
    check(contains_word(engine.query("jugeliz", "full", 20U), "举个例子"),
        "full pinyin u spelling completes 举个例子");
}

void test_settings_limits_and_safety(piinput::Engine& engine) {
    auto settings = piinput::default_settings();
    settings.pinyin.incomplete_candidates = false;
    check(engine.query("mkt", "flypy", 20U, settings).empty(),
        "disabled incomplete candidates rejects trailing edge");
    check(contains_word(engine.query("mktm", "flypy", 20U, settings), "明天"),
        "disabled incomplete candidates keeps complete decoding");

    settings = piinput::default_settings();
    settings.pinyin.prefix_scan_limit = 0U;
    check(engine.query("mkt", "flypy", 20U, settings).empty(), "zero prefix scan disables prefix edges");
    settings.pinyin.prefix_scan_limit = 4096U;
    settings.candidates.max_items = 1U;
    check(engine.query("mingtian", "full", 20U, settings).size() == 1U,
        "snapshot max_items caps requested limit");

    piinput::Engine empty;
    check(empty.query("ming", "full", 10U).empty(), "missing lexicon is safe");
    check(engine.query("", "full", 10U).empty(), "empty input is safe");
    check(engine.query("123", "full", 10U).empty(), "invalid input is safe");
    check(engine.query("ming", "full", 0U).empty(), "zero result limit is safe");
}

void test_boundary_filter_and_determinism(piinput::Engine& engine) {
    const auto candidates = engine.query("mkt", "flypy", 30U);
    check(!contains_word(candidates, "错误边界"), "raw string prefix cannot cross a syllable boundary");

    for (const auto width : {8U, 32U, 128U}) {
        auto settings = piinput::default_settings();
        settings.pinyin.prefix_beam_width = width;
        check(contains_word(engine.query("biruwoycuibuv", "flypy", 30U, settings), "比如我要是不知道"),
            "beam width retains legal long completion");
    }

    const auto baseline = engine.query("mingt", "full", 30U);
#ifdef NDEBUG
    constexpr int stability_iterations = 100;
#else
    constexpr int stability_iterations = 10;
#endif
    for (int iteration = 0; iteration < stability_iterations; ++iteration) {
        const auto repeated = engine.query("mingt", "full", 30U);
        check(repeated.size() == baseline.size(), "repeat count is stable");
        for (std::size_t index = 0; index < baseline.size() && index < repeated.size(); ++index) {
            check(repeated[index].word == baseline[index].word &&
                repeated[index].pinyin == baseline[index].pinyin &&
                repeated[index].score == baseline[index].score,
                "repeat order and score are stable");
        }
    }
}

void test_prefix_sentence_table(piinput::Engine& engine) {
    const auto path = std::filesystem::path(PIINPUT_SOURCE_DIR) / "tests" / "data" / "prefix_sentences.tsv";
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "prefix sentence table opens");
    std::string line;
    std::size_t rows = 0U;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::stringstream stream(line);
        std::vector<std::string> fields;
        for (std::string field; std::getline(stream, field, '\t');) {
            fields.push_back(std::move(field));
        }
        ++rows;
        if (fields.size() != 5U) {
            check(false, "prefix sentence row " + std::to_string(rows) +
                " has five tab-separated fields");
            continue;
        }
        const auto& schema = fields[0U];
        const auto& encoded = fields[1U];
        const auto& canonical = fields[2U];
        const auto& target = fields[3U];
        std::size_t promised = 0U;
        const auto promised_parse = std::from_chars(
            fields[4U].data(), fields[4U].data() + fields[4U].size(), promised);
        if (promised_parse.ec != std::errc{} ||
            promised_parse.ptr != fields[4U].data() + fields[4U].size() ||
            promised == 0U || promised > encoded.size()) {
            check(false, "prefix sentence row " + std::to_string(rows) +
                " has a valid promised_min_prefix");
            continue;
        }
        const auto decoded = engine.decode(encoded, schema, 128U);
        check(std::any_of(decoded.begin(), decoded.end(), [&](const auto& parse) {
            return parse.canonical == canonical;
        }), schema + " fixture decodes to declared canonical pinyin: " + encoded);
        for (std::size_t length = 1U; length <= encoded.size(); ++length) {
            const std::string prefix = encoded.substr(0U, length);
            const auto results = engine.query(prefix, schema, 30U);
            if (length >= promised) {
                check(!results.empty(), "promised prefix is non-empty: " + prefix);
                check(std::all_of(results.begin(), results.end(), [&](const auto& candidate) {
                    return candidate_matches_input_prefix(candidate, prefix, schema, engine);
                }), "promised prefix candidates consume compatible pinyin: " + prefix);
            }
        }
        check(contains_word(engine.query(encoded, schema, 30U), target),
            "complete sentence contains table target: " + target);
    }
    check(rows >= 10U, "prefix sentence table has at least ten rows");
}

void test_bounded_performance(piinput::Engine& engine) {
#ifdef NDEBUG
    constexpr int warmup_iterations = 20;
    constexpr int measured_iterations = 100;
#else
    constexpr int warmup_iterations = 5;
    constexpr int measured_iterations = 20;
#endif
    for (int warmup = 0; warmup < warmup_iterations; ++warmup) {
        (void)engine.query("biruwoycuibuv", "flypy", 30U);
    }
    std::vector<double> microseconds;
    microseconds.reserve(measured_iterations);
    std::size_t result_guard = 0U;
    for (int iteration = 0; iteration < measured_iterations; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        const auto results = engine.query(
            iteration % 2 == 0 ? "mkt" : "biruwoycuibuv", "flypy", 30U);
        const auto stopped = std::chrono::steady_clock::now();
        result_guard += results.size();
        microseconds.push_back(
            std::chrono::duration<double, std::micro>(stopped - started).count());
    }
    std::sort(microseconds.begin(), microseconds.end());
    const auto p95_index = static_cast<std::size_t>(
        0.95 * static_cast<double>(microseconds.size() - 1U));
    const double p95_us = microseconds[p95_index];
#ifdef NDEBUG
    check(p95_us < 10'000.0, "small-fixture incremental P95 stays below 10 ms");
#endif
    std::cout << "incremental benchmark: iterations=" << measured_iterations
              << " p95_us=" << p95_us << " result_guard=" << result_guard << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    if (only.empty() || only == "contract") std::cerr << "stage: contract\n";
    test_public_parse_contract();
    if (only.empty() || only == "cache") std::cerr << "stage: cache\n";
    if (only.empty() || only == "cache") {
        test_query_scope_cache_and_canonical_parse_deduplication();
        test_destination_paths_never_exceed_beam();
        test_result_limit_bounds_effective_beam();
        test_exact_word_edge_score_is_computed_once_per_span();
        test_terminal_prefix_cross_product_is_result_bounded();
        test_terminal_prefix_cap_matches_exhaustive_oracle();
        test_complete_exact_keeps_user_score_headroom();
        test_incomplete_prefix_keeps_user_score_headroom();
        test_prefix_scan_budget_is_independent_from_output_headroom();
        test_direct_exact_ties_prefer_longer_words_then_dictionary_order();
        test_engine_direct_exact_tie_compatibility();
        test_prefix_query_cache_is_invalidated_by_lexicon_reload();
        test_low_scan_budget_retains_complete_prefix_parses();
        test_low_scan_budget_keeps_short_parses_that_fit();
        test_max_word_syllable_limit_does_not_overflow();
    }
    const auto path = write_lexicon();
    piinput::Engine engine;
    engine.load_lexicon(path);
    if (only.empty() || only == "cross") std::cerr << "stage: cross-start\n";
    if (only.empty() || only == "cross") test_cross_start_prefix_and_complete_input(engine);
    if (only.empty() || only == "arbitrary") std::cerr << "stage: arbitrary\n";
    if (only.empty() || only == "arbitrary") test_arbitrary_length_and_variants(engine);
    if (only.empty() || only == "settings") std::cerr << "stage: settings\n";
    if (only.empty() || only == "settings") test_settings_limits_and_safety(engine);
    if (only.empty() || only == "boundaries") std::cerr << "stage: boundaries\n";
    if (only.empty() || only == "boundaries") test_boundary_filter_and_determinism(engine);
    if (only.empty() || only == "table") std::cerr << "stage: table\n";
    if (only.empty() || only == "table") test_prefix_sentence_table(engine);
    if (only.empty() || only == "performance") std::cerr << "stage: performance\n";
    if (only.empty() || only == "performance") test_bounded_performance(engine);
    std::filesystem::remove(path);
    if (failures != 0) {
        std::cerr << failures << " incremental decoder test(s) failed\n";
        return 1;
    }
    std::cout << "All incremental decoder tests passed\n";
    return 0;
}
