#pragma once

#include "piinput/binary_lexicon.h"
#include "piinput/lexicon.h"
#include "piinput/pinyin.h"
#include "piinput/settings.h"
#include "piinput/shuangpin.h"
#include "piinput/user_model.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace piinput {

struct EngineCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t base_weight{};
    std::int64_t score{};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
};

class Engine final {
public:
    Engine();
    Engine(const Engine& other);
    Engine& operator=(const Engine& other);
    Engine(Engine&& other) noexcept = default;
    Engine& operator=(Engine&& other) noexcept = default;

    void load_lexicon(const std::filesystem::path& path);
    void load_user_model(const std::filesystem::path& path);
    void save_user_model(const std::filesystem::path& path) const;
    void record_selection(const std::string& pinyin, const std::string& word);

    // Invalid or disabled full-pinyin spellings return an empty result; input
    // errors do not escape this Engine hot path. Direct PinyinSegmenter calls
    // retain their lower-level std::invalid_argument contract.
    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 16U) const;

    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const PinyinSettings& settings) const;

    // Invalid or disabled full-pinyin spellings likewise produce no candidates
    // without throwing an input-validation exception.
    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const PinyinSettings& settings) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit,
        const SettingsSnapshot& settings) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const ShuangpinDecoder& shuangpin() const noexcept;

private:
    struct PrefixQueryCache {
        std::shared_mutex state_mutex;
        std::shared_mutex entries_mutex;
        std::unordered_map<std::string, std::vector<LexiconCandidate>> entries;
        std::atomic<std::size_t> lexicon_entry_count{};
    };

    [[nodiscard]] std::vector<LexiconCandidate> query_exact_unlocked(
        const std::string& pinyin,
        std::size_t limit) const;
    [[nodiscard]] std::vector<LexiconCandidate> query_prefix_unlocked(
        const std::string& pinyin_prefix,
        std::size_t limit,
        std::size_t scan_limit) const;

    std::variant<std::monostate, DevLexicon, BinaryLexicon> lexicon_;
    mutable std::shared_ptr<PrefixQueryCache> prefix_query_cache_;
    PinyinSegmenter pinyin_;
    ShuangpinDecoder shuangpin_;
    UserModel user_model_;
};

}  // namespace piinput
