#include "piinput/engine.h"
#include "piinput/full_pinyin_variants.h"
#include "piinput/settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

class TemporaryLexicon final {
public:
    explicit TemporaryLexicon(const std::string& stem) {
        static std::atomic<unsigned long long> sequence{0U};
#ifdef _WIN32
        const auto process_id = static_cast<unsigned long long>(_getpid());
#else
        const auto process_id = static_cast<unsigned long long>(getpid());
#endif
        const auto timestamp = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() /
            (stem + "-" + std::to_string(process_id) + "-" + std::to_string(timestamp) + "-" +
                std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)) + ".tsv");
    }

    ~TemporaryLexicon() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    TemporaryLexicon(const TemporaryLexicon&) = delete;
    TemporaryLexicon& operator=(const TemporaryLexicon&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::vector<std::string>& values,
    const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool contains_canonical(
    const std::vector<piinput::PinyinSegmentation>& values,
    const std::string& expected) {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) {
        return value.canonical == expected;
    });
}

[[nodiscard]] bool same_segmentations(
    const std::vector<piinput::PinyinSegmentation>& left,
    const std::vector<piinput::PinyinSegmentation>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index].syllables != right[index].syllables ||
            left[index].canonical != right[index].canonical ||
            left[index].score != right[index].score) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_candidates(
    const std::vector<piinput::EngineCandidate>& left,
    const std::vector<piinput::EngineCandidate>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index].word != right[index].word || left[index].pinyin != right[index].pinyin ||
            left[index].base_weight != right[index].base_weight || left[index].score != right[index].score) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool contains_word(
    const std::vector<piinput::EngineCandidate>& candidates,
    const std::string& word) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.word == word;
    });
}

void test_canonical_spellings() {
    const piinput::PinyinSettings enabled;

    for (const auto* input : {"jugelizi", "jvgelizi", "JVGELIZI"}) {
        const auto variants = piinput::normalize_full_pinyin_variants(input, enabled, 8U);
        check(contains(variants, "jugelizi"), std::string(input) + " normalizes to canonical ju");
    }

    for (const auto* input : {"ju", "jv", "qu", "qv", "xu", "xv", "yu", "yv"}) {
        std::string expected(input);
        if (expected[1] == 'v') {
            expected[1] = 'u';
        }
        check(contains(piinput::normalize_full_pinyin_variants(input, enabled, 8U), expected),
            std::string(input) + " uses canonical j/q/x/y+u spelling");
    }

    for (const auto* input : {"lv", "l\xC3\xBC", "lu:"}) {
        check(contains(piinput::normalize_full_pinyin_variants(input, enabled, 8U), "lv"),
            std::string(input) + " normalizes to lv");
    }
    for (const auto* input : {"nv", "n\xC3\xBC", "nu:"}) {
        check(contains(piinput::normalize_full_pinyin_variants(input, enabled, 8U), "nv"),
            std::string(input) + " normalizes to nv");
    }

    // The current core syllable table stores these extended syllables as lue/nue.
    for (const auto* input : {"lve", "l\xC3\xBC" "e", "lu:e"}) {
        check(contains(piinput::normalize_full_pinyin_variants(input, enabled, 8U), "lue"),
            std::string(input) + " normalizes to core canonical lue");
    }
    for (const auto* input : {"nve", "n\xC3\xBC" "e", "nu:e"}) {
        check(contains(piinput::normalize_full_pinyin_variants(input, enabled, 8U), "nue"),
            std::string(input) + " normalizes to core canonical nue");
    }

    check(contains(piinput::normalize_full_pinyin_variants("lu", enabled, 8U), "lu"),
        "ordinary lu keeps its original meaning");
    check(!contains(piinput::normalize_full_pinyin_variants("lu", enabled, 8U), "lv"),
        "ordinary lu is not rewritten to lv");
    check(contains(piinput::normalize_full_pinyin_variants("JV GE'LI  ZI", enabled, 8U), "ju'ge'li'zi"),
        "spaces and apostrophes preserve manual syllable boundaries");
}

void test_settings_matrix() {
    for (const bool uv : {false, true}) {
        for (const bool colon : {false, true}) {
            piinput::PinyinSettings settings;
            settings.uv_compatibility = uv;
            settings.accept_u_colon = colon;

            check(!piinput::normalize_full_pinyin_variants("jv", settings, 8U).empty() == uv,
                "jv follows uv_compatibility setting");
            check(!piinput::normalize_full_pinyin_variants("lu:", settings, 8U).empty() == colon,
                "u-colon follows accept_u_colon independently");
            check(contains(piinput::normalize_full_pinyin_variants("lv", settings, 8U), "lv"),
                "standard ASCII lv remains accepted for every setting combination");
            check(contains(piinput::normalize_full_pinyin_variants("l\xC3\xBC", settings, 8U), "lv"),
                "UTF-8 l-u-umlaut remains accepted for every setting combination");
        }
    }
}

void test_limits_deduplication_and_invalid_input() {
    const piinput::PinyinSettings settings;
    check(piinput::normalize_full_pinyin_variants("jv", settings, 0U).empty(),
        "zero variant limit returns no variants");

    const auto limited = piinput::normalize_full_pinyin_variants("jvqvxvyv", settings, 1U);
    check(limited.size() == 1U && limited.front() == "juquxuyu",
        "many compatibility points remain bounded by a one-variant limit");
    const auto repeated = piinput::normalize_full_pinyin_variants("jvqvxvyv", settings, 8U);
    check(repeated == piinput::normalize_full_pinyin_variants("jvqvxvyv", settings, 8U),
        "variant order is deterministic");
    check(repeated.size() <= 8U, "variant count never exceeds its limit");

    const auto boundary_variants = piinput::normalize_full_pinyin_variants("lvenve", settings, 8U);
    check(boundary_variants == std::vector<std::string>{"luenue", "luenve", "lvenue", "lvenve"},
        "continuous l/n+ve points retain both extended-syllable and syllable-boundary interpretations");
    check(piinput::normalize_full_pinyin_variants("lvenvelvenve", settings, 3U).size() == 3U,
        "multiple branching points stop expanding at the requested limit");

    std::string invalid_utf8{"ju"};
    invalid_utf8.push_back(static_cast<char>(0xFF));
    for (const auto& invalid : std::vector<std::string>{
             "", ":", "ju:", "j::u", "ju!", "'", "   ", invalid_utf8}) {
        check(piinput::normalize_full_pinyin_variants(invalid, settings, 8U).empty(),
            "invalid input is rejected without throwing");
    }
}

void test_legal_normalized_variants_match_segmenter_grammar() {
    const piinput::PinyinSettings settings;
    const piinput::PinyinSegmenter segmenter;
    for (const auto* input : {
             "ju", "jv", "lv", "l\xC3\xBC", "lu:", "lve", "l\xC3\xBC" "e", "lu:e",
             "nv", "n\xC3\xBC", "nu:", "nve", "n\xC3\xBC" "e", "nu:e"}) {
        const auto variants = piinput::normalize_full_pinyin_variants(input, settings, 8U);
        check(!variants.empty(), std::string(input) + " has a legal normalized spelling");
        for (const auto& variant : variants) {
            check(!segmenter.segment(variant, 8U).empty(),
                variant + " remains accepted by the lower-level segmenter grammar");
        }
    }
}

void write_engine_fixture(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "word\tpinyin\tweight\n"
           << "举个例子\tju'ge'li'zi\t1000\n"
           << "绿\tlv\t900\n"
           << "略\tlue\t800\n"
           << "女\tnv\t700\n"
           << "虐\tnue\t600\n"
           << "路\tlu\t500\n"
           << "合流\tlue'nue\t400\n"
           << "合流\tlv'e'nv'e\t400\n"
           << "秋\tqiu\t300\n";
}

void test_engine_integration_and_shuangpin_isolation() {
    const TemporaryLexicon fixture("piinput-full-pinyin-variants");
    write_engine_fixture(fixture.path());
    piinput::Engine engine;
    engine.load_lexicon(fixture.path());

    check(engine.decode("ju", "full", {}).empty(), "brace-initialized zero decode limit is unambiguous");
    check(engine.query("ju", "full", {}).empty(), "brace-initialized zero query limit is unambiguous");

    const auto standard_decode = engine.decode("jugelizi", "full", 16U);
    const auto compatible_decode = engine.decode("jvgelizi", "full", 16U);
    check(contains_canonical(standard_decode, "ju'ge'li'zi"), "standard spelling segments to fixture pinyin");
    check(same_segmentations(standard_decode, compatible_decode),
        "standard and compatible spellings have identical segmentations");

    const auto standard_candidates = engine.query("jugelizi", "full", 10U);
    const auto compatible_candidates = engine.query("jvgelizi", "full", 10U);
    check(!standard_candidates.empty() && !compatible_candidates.empty() &&
            standard_candidates.front().word == "举个例子" &&
            compatible_candidates.front().word == "举个例子",
        "standard and compatible spellings return the same leading candidate");
    check(std::count_if(compatible_candidates.begin(), compatible_candidates.end(), [](const auto& candidate) {
        return candidate.word == "举个例子";
    }) == 1, "query de-duplicates a word reached through compatible spellings");

    for (const auto* input : {"lv", "l\xC3\xBC", "lu:"}) {
        check(contains_word(engine.query(input, "full", 10U), "绿"),
            std::string(input) + " reaches canonical lv candidate");
    }
    for (const auto* input : {"lve", "l\xC3\xBC" "e", "lu:e"}) {
        check(contains_word(engine.query(input, "full", 10U), "略"),
            std::string(input) + " reaches canonical lue candidate");
    }
    for (const auto* input : {"nv", "n\xC3\xBC", "nu:"}) {
        check(contains_word(engine.query(input, "full", 10U), "女"),
            std::string(input) + " reaches canonical nv candidate");
    }
    for (const auto* input : {"nve", "n\xC3\xBC" "e", "nu:e"}) {
        check(contains_word(engine.query(input, "full", 10U), "虐"),
            std::string(input) + " reaches canonical nue candidate");
    }
    const auto lu_candidates = engine.query("lu", "full", 10U);
    check(contains_word(lu_candidates, "路") && !contains_word(lu_candidates, "绿"),
        "ordinary lu remains distinct from lv in Engine queries");

    piinput::PinyinSettings disabled;
    disabled.uv_compatibility = false;
    bool disabled_jv_threw = false;
    try {
        check(engine.decode("jvgelizi", "full", 16U, disabled).empty(),
            "settings overload rejects jv when compatibility is disabled");
        check(engine.query("jvgelizi", "full", 10U, disabled).empty(),
            "query settings overload rejects disabled compatible spelling");
    } catch (...) {
        disabled_jv_threw = true;
    }
    check(!disabled_jv_threw, "disabled full-pinyin compatibility returns empty without throwing");
    check(!engine.query("lv", "full", 10U, disabled).empty(),
        "lv remains usable when compatibility is disabled");
    disabled.accept_u_colon = false;
    check(engine.query("lu:", "full", 10U, disabled).empty(),
        "query rejects u-colon when its independent setting is disabled");

    std::string truncated_utf8{"ju"};
    truncated_utf8.push_back(static_cast<char>(0xC3));
    for (const auto& invalid : std::vector<std::string>{"ju!", truncated_utf8}) {
        bool threw = false;
        try {
            check(engine.decode(invalid, "full", 16U).empty(), "Engine decode rejects invalid full pinyin");
            check(engine.query(invalid, "full", 10U).empty(), "Engine query rejects invalid full pinyin");
        } catch (...) {
            threw = true;
        }
        check(!threw, "Engine full-pinyin hot path never throws for invalid input");
    }

    piinput::PinyinSegmenter segmenter;
    for (const auto& invalid : std::vector<std::string>{"ju!", truncated_utf8}) {
        bool threw_invalid_argument = false;
        try {
            (void)segmenter.segment(invalid, 8U);
        } catch (const std::invalid_argument&) {
            threw_invalid_argument = true;
        }
        check(threw_invalid_argument, "direct PinyinSegmenter retains its invalid_argument contract");
    }

    const auto baseline_merge = engine.query("luenue", "full", 10U);
    const auto merged_variants = engine.query("lvenve", "full", 10U);
    const auto baseline_word = std::find_if(baseline_merge.begin(), baseline_merge.end(), [](const auto& candidate) {
        return candidate.word == "合流";
    });
    const auto merged_word = std::find_if(merged_variants.begin(), merged_variants.end(), [](const auto& candidate) {
        return candidate.word == "合流";
    });
    check(std::count_if(merged_variants.begin(), merged_variants.end(), [](const auto& candidate) {
        return candidate.word == "合流";
    }) == 1, "query merges the same word reached through multiple full-pinyin variants");
    check(baseline_word != baseline_merge.end() && merged_word != merged_variants.end() &&
            baseline_word->score == merged_word->score,
        "variant merging keeps the best original score without accumulation or offset");

    piinput::PinyinSettings enabled;
    piinput::PinyinSettings shuangpin_disabled = enabled;
    shuangpin_disabled.uv_compatibility = false;
    shuangpin_disabled.accept_u_colon = false;
    const auto flypy_before = engine.decode("qq", "flypy", 16U, enabled);
    const auto flypy_after = engine.decode("qq", "flypy", 16U, shuangpin_disabled);
    check(same_segmentations(flypy_before, flypy_after),
        "full-pinyin spelling settings never alter Xiaohe decoding");
    const auto flypy_candidates_before = engine.query("qq", "flypy", 10U, enabled);
    const auto flypy_candidates_after = engine.query("qq", "flypy", 10U, shuangpin_disabled);
    check(!flypy_candidates_before.empty() && flypy_candidates_before.front().word == "秋" &&
            same_candidates(flypy_candidates_before, flypy_candidates_after),
        "full-pinyin spelling settings never alter Xiaohe candidate queries");
}

}  // namespace

int main() {
    test_canonical_spellings();
    test_settings_matrix();
    test_limits_deduplication_and_invalid_input();
    test_legal_normalized_variants_match_segmenter_grammar();
    test_engine_integration_and_shuangpin_isolation();

    if (failures == 0) {
        std::cout << "All full-pinyin variant tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
