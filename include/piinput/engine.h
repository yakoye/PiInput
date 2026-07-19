#pragma once

#include "piinput/binary_lexicon.h"
#include "piinput/lexicon.h"
#include "piinput/pinyin.h"
#include "piinput/settings.h"
#include "piinput/shuangpin.h"
#include "piinput/user_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace piinput {

struct EngineCandidate {
    std::string word;
    std::string pinyin;
    std::uint32_t base_weight{};
    std::int64_t score{};
};

class Engine final {
public:
    void load_lexicon(const std::filesystem::path& path);
    void load_user_model(const std::filesystem::path& path);
    void save_user_model(const std::filesystem::path& path) const;
    void record_selection(const std::string& pinyin, const std::string& word);

    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 16U) const;

    [[nodiscard]] std::vector<PinyinSegmentation> decode(
        const std::string& input,
        const std::string& schema,
        const PinyinSettings& settings,
        std::size_t limit = 16U) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::vector<EngineCandidate> query(
        const std::string& input,
        const std::string& schema,
        const PinyinSettings& settings,
        std::size_t limit = 10U) const;

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const ShuangpinDecoder& shuangpin() const noexcept;

private:
    [[nodiscard]] std::vector<LexiconCandidate> query_exact(
        const std::string& pinyin,
        std::size_t limit) const;
    [[nodiscard]] std::vector<LexiconCandidate> query_prefix(
        const std::string& pinyin_prefix,
        std::size_t limit,
        std::size_t scan_limit) const;

    std::variant<std::monostate, DevLexicon, BinaryLexicon> lexicon_;
    PinyinSegmenter pinyin_;
    ShuangpinDecoder shuangpin_;
    UserModel user_model_;
};

}  // namespace piinput
