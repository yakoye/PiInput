#include "piinput/engine.h"
#include "piinput/settings.h"
#include "piinput/user_model.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

void check(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::filesystem::path temporary_file(const std::string_view name) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("piinput-user-model-" + std::to_string(nonce) + "-" + std::string(name));
}

std::filesystem::path write_phrase_fixture() {
    const auto path = temporary_file("phrase-fixture.tsv");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "丝\tsi\t980\n"
           << "思\tsi\t960\n"
           << "死\tsi\t940\n"
           << "滑\thua\t970\n"
           << "华\thua\t950\n"
           << "话\thua\t930\n"
           << "死话\tsi'hua\t2000\n"
           << "四话\tsi'hua\t1900\n"
           << "私话\tsi'hua\t1800\n";
    return path;
}

const piinput::UserPhrase& require_phrase(
    const std::vector<piinput::UserPhrase>& phrases,
    const std::string_view word) {
    const auto found = std::find_if(phrases.begin(), phrases.end(), [&](const auto& phrase) {
        return phrase.word == word;
    });
    check(found != phrases.end(), "expected user phrase is present");
    return *found;
}

std::size_t candidate_position(
    const std::vector<piinput::EngineCandidate>& candidates,
    const std::string_view word) {
    const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& item) {
        return item.word == word;
    });
    check(found != candidates.end(), "expected engine candidate is present");
    return static_cast<std::size_t>(std::distance(candidates.begin(), found));
}

void test_exact_index_and_learning_tiers() {
    piinput::UserModel model;
    model.record_selection("si'hua", "丝滑");
    model.record_selection("si'hua", "四话");
    model.record_selection("si'hua", "四话");
    model.record_selection("ye'peng'yu", "叶鹏玉");

    const auto sihua = model.query_exact("si'hua");
    check(sihua.size() == 2U, "exact pinyin index returns only matching phrases");
    check(require_phrase(sihua, "丝滑").selection_count == 1U,
        "one selection is persisted as tier one");
    check(require_phrase(sihua, "丝滑").learning_tier == 1U,
        "one selection maps to learning tier one");
    check(require_phrase(sihua, "四话").learning_tier == 2U,
        "two selections map to learning tier two");
    check(model.query_exact("ye'peng'yu").size() == 1U,
        "a different canonical pinyin has its own bucket");
    check(model.query_exact("missing").empty(),
        "missing canonical pinyin does not scan or leak other entries");
}

void test_composed_pin_suppress_and_remove_semantics() {
    piinput::UserModel model;
    model.record_composed_phrase("deng'zhen'duo", "邓振铎");
    auto phrase = require_phrase(model.query_exact("deng'zhen'duo"), "邓振铎");
    check(phrase.user_created && phrase.selection_count == 1U,
        "a composed phrase is a first-tier user-created entry");

    model.pin("deng'zhen'duo", "邓振铎");
    phrase = require_phrase(model.query_exact("deng'zhen'duo"), "邓振铎");
    check(phrase.pinned && !phrase.suppressed,
        "pinning clears suppression and preserves the phrase");

    model.unpin("deng'zhen'duo", "邓振铎");
    phrase = require_phrase(model.query_exact("deng'zhen'duo"), "邓振铎");
    check(!phrase.pinned && phrase.user_created,
        "unpinning keeps learning and the user-created identity");

    model.remove_learning("deng'zhen'duo", "邓振铎");
    check(model.query_exact("deng'zhen'duo").empty(),
        "removing learning removes an otherwise unreferenced user-created phrase");

    model.suppress("si'hua", "死话");
    phrase = require_phrase(model.query_exact("si'hua"), "死话");
    check(phrase.suppressed && phrase.selection_count == 0U,
        "a suppression tombstone remains queryable with zero selections");
}

void test_count_saturates_without_overflow() {
    const auto path = temporary_file("saturated.tsv");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "pinyin\tword\tcount\tlast_used\tpinned\tsuppressed\tuser_created\n"
               << "si'hua\t丝滑\t" << (std::numeric_limits<std::uint32_t>::max)()
               << "\t1\t0\t0\t1\n";
    }
    piinput::UserModel model;
    model.load(path);
    model.record_selection("si'hua", "丝滑");
    check(require_phrase(model.query_exact("si'hua"), "丝滑").selection_count ==
            (std::numeric_limits<std::uint32_t>::max)(),
        "selection count saturates instead of wrapping");
    std::filesystem::remove(path);
}

void test_legacy_and_new_persistence_roundtrip() {
    const auto legacy = temporary_file("legacy.tsv");
    {
        std::ofstream output(legacy, std::ios::binary | std::ios::trunc);
        output << "si'hua\t丝滑\t1\t10\n"
               << "ming'tian\t明天\t2\t20\t1\n"
               << "hou'xuan\t候选\t0\t30\t0\t1\n";
    }
    piinput::UserModel model;
    model.load(legacy);
    check(!require_phrase(model.query_exact("si'hua"), "丝滑").user_created,
        "legacy rows default user_created to false");
    check(require_phrase(model.query_exact("ming'tian"), "明天").pinned,
        "legacy pinned state loads");
    check(require_phrase(model.query_exact("hou'xuan"), "候选").suppressed,
        "legacy suppression state loads");

    model.record_composed_phrase("ye'peng'yu", "叶鹏玉");
    const auto current = temporary_file("current.tsv");
    model.save(current);
    piinput::UserModel loaded;
    loaded.load(current);
    check(require_phrase(loaded.query_exact("ye'peng'yu"), "叶鹏玉").user_created,
        "new user_created field survives save and reload");
    check(require_phrase(loaded.query_exact("hou'xuan"), "候选").suppressed,
        "zero-count suppression tombstone survives save and reload");

    std::filesystem::remove(legacy);
    std::filesystem::remove(current);
}

void test_corrupt_rows_are_isolated_and_reported() {
    const auto path = temporary_file("corrupt-rows.tsv");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "pinyin\tword\tcount\tlast_used\tpinned\tsuppressed\tuser_created\n"
               << "si'hua\t丝滑\t1\t10\t0\t0\t1\n"
               << "broken row without tabs\n"
               << "ming'tian\t明天\tnot-a-number\t20\t0\t0\t0\n"
               << "ye'peng'yu\t叶鹏玉\t2\t30\t1\t0\t1\n";
    }

    piinput::UserModel model;
    const auto diagnostics = model.load(path);
    check(diagnostics.size() == 2U,
        "each malformed row is reported without discarding valid rows");
    check(diagnostics[0].find("line 3") != std::string::npos &&
          diagnostics[1].find("line 4") != std::string::npos,
        "load diagnostics identify the damaged source lines");
    check(require_phrase(model.query_exact("si'hua"), "丝滑").user_created,
        "valid data before a malformed row is retained");
    check(require_phrase(model.query_exact("ye'peng'yu"), "叶鹏玉").pinned,
        "valid data after malformed rows is retained");
    std::filesystem::remove(path);
}

void test_concurrent_saves_use_unique_temporary_files() {
    const auto directory = temporary_file("concurrent-save-dir");
    std::filesystem::create_directories(directory);
    const auto path = directory / "user_model.tsv";
    piinput::UserModel model;
    for (int index = 0; index < 500; ++index) {
        model.record_composed_phrase(
            "pin'yin'" + std::to_string(index),
            "word" + std::to_string(index));
    }

    std::exception_ptr first_error;
    std::exception_ptr second_error;
    std::thread first([&] {
        try { model.save(path); } catch (...) { first_error = std::current_exception(); }
    });
    std::thread second([&] {
        try { model.save(path); } catch (...) { second_error = std::current_exception(); }
    });
    first.join();
    second.join();
    check(first_error == nullptr && second_error == nullptr,
        "concurrent snapshots do not collide on one fixed temporary filename");

    piinput::UserModel loaded;
    const auto diagnostics = loaded.load(path);
    check(diagnostics.empty() && loaded.entry_count() == 500U,
        "the atomically replaced destination remains a complete model");
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        check(item.path() == path,
            "successful atomic saves leave no temporary files behind");
    }
    std::filesystem::remove_all(directory);
}

void test_engine_generates_composed_user_phrase_without_static_entry() {
    const auto lexicon = write_phrase_fixture();
    piinput::Engine engine;
    engine.load_lexicon(lexicon);
    auto settings = piinput::default_settings();
    settings.candidates.items_per_row = 6U;
    settings.candidates.max_items = 30U;

    const auto before = engine.query("sihua", "full", 30U, settings);
    check(std::none_of(before.begin(), before.end(), [](const auto& candidate) {
        return candidate.word == "丝滑" &&
            candidate.evidence.kind == piinput::CandidateKind::user_phrase;
    }), "the static fixture has no learned source even if dynamic characters can form the text");

    engine.record_composed_phrase("si'hua", "丝滑");
    const auto learned_once = engine.query("sihua", "full", 30U, settings);
    const auto once_position = candidate_position(learned_once, "丝滑");
    check(once_position == 0U,
        "one confirmed composed selection promotes the phrase to first place in its group");
    const auto found = std::find_if(learned_once.begin(), learned_once.end(), [](const auto& candidate) {
        return candidate.word == "丝滑";
    });
    check(found != learned_once.end() &&
            found->evidence.kind == piinput::CandidateKind::user_phrase,
        "the engine preserves user phrase evidence for deterministic mixing");

    engine.record_composed_phrase("si'hua", "丝滑");
    const auto learned_twice = engine.query("sihua", "full", 30U, settings);
    const auto twice_position = candidate_position(learned_twice, "丝滑");
    check(twice_position == 0U,
        "later confirmed selections keep the learned phrase first without destabilizing it");

    engine.record_composed_phrase("si'hua", "丝滑");
    const auto learned_three_times = engine.query("sihua", "full", 30U, settings);
    check(candidate_position(learned_three_times, "丝滑") == 0U,
        "repeated confirmed selections keep the phrase first");

    engine.remove_candidate_learning("si'hua", "丝滑");
    engine.record_composed_phrase("si'hua", "丝滑");
    engine.pin_candidate("si'hua", "丝滑");
    const auto pinned = engine.query("sihua", "full", 30U, settings);
    check(candidate_position(pinned, "丝滑") == 0U,
        "pinning explicitly places a phrase first regardless of learning tier");
    engine.unpin_candidate("si'hua", "丝滑");
    engine.remove_candidate_learning("si'hua", "丝滑");

    engine.record_composed_phrase("si'hua", "思华");
    engine.record_composed_phrase("si'hua", "死滑");
    engine.record_composed_phrase("si'hua", "四华");
    const auto crowded = engine.query("sihua", "full", 30U, settings);
    const auto first_single = candidate_position(crowded, "丝");
    check(candidate_position(crowded, "思华") < first_single &&
            candidate_position(crowded, "死滑") < first_single &&
            candidate_position(crowded, "四华") < first_single,
        "all confirmed real phrases remain ahead of single-character fallbacks across rows");

    engine.suppress_candidate("si'hua", "丝滑");
    const auto suppressed = engine.query("sihua", "full", 30U, settings);
    check(std::none_of(suppressed.begin(), suppressed.end(), [](const auto& candidate) {
        return candidate.word == "丝滑";
    }), "suppression is applied after merging all candidate sources");

    std::filesystem::remove(lexicon);
}

}  // namespace

int main() {
    try {
        test_exact_index_and_learning_tiers();
        test_composed_pin_suppress_and_remove_semantics();
        test_count_saturates_without_overflow();
        test_legacy_and_new_persistence_roundtrip();
        test_corrupt_rows_are_isolated_and_reported();
        test_concurrent_saves_use_unique_temporary_files();
        test_engine_generates_composed_user_phrase_without_static_entry();
        std::cout << "PiInput user model tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
