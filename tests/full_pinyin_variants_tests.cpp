#include "piinput/engine.h"
#include "piinput/full_pinyin_variants.h"
#include "piinput/settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

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

void write_engine_fixture(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "word\tpinyin\tweight\n"
           << "举个例子\tju'ge'li'zi\t1000\n"
           << "绿\tlv\t900\n"
           << "略\tlue\t800\n"
           << "女\tnv\t700\n"
           << "虐\tnue\t600\n"
           << "路\tlu\t500\n";
}

void test_engine_integration_and_shuangpin_isolation() {
    const auto path = std::filesystem::temp_directory_path() / "piinput-full-pinyin-variants.tsv";
    write_engine_fixture(path);
    piinput::Engine engine;
    engine.load_lexicon(path);

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
    check(engine.decode("jvgelizi", "full", disabled, 16U).empty(),
        "settings overload rejects jv when compatibility is disabled");
    check(engine.query("jvgelizi", "full", disabled, 10U).empty(),
        "query settings overload rejects disabled compatible spelling");
    check(!engine.query("lv", "full", disabled, 10U).empty(),
        "lv remains usable when compatibility is disabled");
    disabled.accept_u_colon = false;
    check(engine.query("lu:", "full", disabled, 10U).empty(),
        "query rejects u-colon when its independent setting is disabled");

    piinput::PinyinSettings enabled;
    piinput::PinyinSettings shuangpin_disabled = enabled;
    shuangpin_disabled.uv_compatibility = false;
    shuangpin_disabled.accept_u_colon = false;
    const auto flypy_before = engine.decode("jv", "flypy", enabled, 16U);
    const auto flypy_after = engine.decode("jv", "flypy", shuangpin_disabled, 16U);
    check(same_segmentations(flypy_before, flypy_after),
        "full-pinyin spelling settings never alter Xiaohe decoding");

    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_canonical_spellings();
    test_settings_matrix();
    test_limits_deduplication_and_invalid_input();
    test_engine_integration_and_shuangpin_isolation();

    if (failures == 0) {
        std::cout << "All full-pinyin variant tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
