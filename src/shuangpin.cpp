#include "piinput/shuangpin.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace piinput {
namespace {

enum class SchemeKind {
    flypy,
    natural,
    microsoft,
    abc,
};

struct SchemeDefinition {
    const char* id;
    const char* name;
    SchemeKind kind;
    std::unordered_map<std::string, char> finals;
    std::unordered_map<std::string, char> initials;
};

[[nodiscard]] std::vector<SchemeDefinition> definitions() {
    const std::unordered_map<std::string, char> flypy_finals = {
        {"iu", 'q'}, {"ei", 'w'}, {"uan", 'r'}, {"van", 'r'},
        {"ue", 't'}, {"ve", 't'}, {"un", 'y'}, {"vn", 'y'},
        {"uo", 'o'}, {"ie", 'p'}, {"ong", 's'}, {"iong", 's'},
        {"ing", 'k'}, {"uai", 'k'}, {"ai", 'd'}, {"en", 'f'},
        {"eng", 'g'}, {"iang", 'l'}, {"uang", 'l'}, {"ang", 'h'},
        {"ian", 'm'}, {"an", 'j'}, {"ou", 'z'}, {"ia", 'x'},
        {"ua", 'x'}, {"iao", 'n'}, {"ao", 'c'}, {"ui", 'v'},
        {"in", 'b'},
    };
    const std::unordered_map<std::string, char> natural_finals = {
        {"iu", 'q'}, {"ia", 'w'}, {"ua", 'w'}, {"uan", 'r'}, {"van", 'r'},
        {"ue", 't'}, {"ve", 't'}, {"ing", 'y'}, {"uai", 'y'},
        {"uo", 'o'}, {"un", 'p'}, {"vn", 'p'}, {"ong", 's'}, {"iong", 's'},
        {"iang", 'd'}, {"uang", 'd'}, {"en", 'f'}, {"eng", 'g'},
        {"ang", 'h'}, {"ian", 'm'}, {"an", 'j'}, {"iao", 'c'},
        {"ao", 'k'}, {"ai", 'l'}, {"ei", 'z'}, {"ie", 'x'},
        {"ui", 'v'}, {"ou", 'b'}, {"in", 'n'},
    };
    const std::unordered_map<std::string, char> microsoft_finals = {
        {"iu", 'q'}, {"ia", 'w'}, {"ua", 'w'}, {"er", 'r'},
        {"uan", 'r'}, {"van", 'r'}, {"ue", 't'}, {"ve", 't'},
        {"v", 'y'}, {"uai", 'y'}, {"uo", 'o'}, {"un", 'p'}, {"vn", 'p'},
        {"ong", 's'}, {"iong", 's'}, {"iang", 'd'}, {"uang", 'd'},
        {"en", 'f'}, {"eng", 'g'}, {"ang", 'h'}, {"ian", 'm'},
        {"an", 'j'}, {"iao", 'c'}, {"ao", 'k'}, {"ai", 'l'},
        {"ei", 'z'}, {"ie", 'x'}, {"ui", 'v'}, {"ou", 'b'},
        {"in", 'n'}, {"ing", ';'},
    };
    const std::unordered_map<std::string, char> abc_finals = {
        {"ei", 'q'}, {"ian", 'w'}, {"er", 'r'}, {"iu", 'r'},
        {"iang", 't'}, {"uang", 't'}, {"ing", 'y'}, {"uo", 'o'},
        {"uan", 'p'}, {"van", 'p'}, {"ong", 's'}, {"iong", 's'},
        {"ia", 'd'}, {"ua", 'd'}, {"en", 'f'}, {"eng", 'g'},
        {"ang", 'h'}, {"an", 'j'}, {"iao", 'z'}, {"ao", 'k'},
        {"in", 'c'}, {"uai", 'c'}, {"ai", 'l'}, {"ie", 'x'},
        {"ou", 'b'}, {"un", 'n'}, {"vn", 'n'}, {"ue", 'm'},
        {"ve", 'm'}, {"ui", 'm'},
    };

    return {
        {"flypy", "小鹤双拼", SchemeKind::flypy, flypy_finals,
            {{"zh", 'v'}, {"ch", 'i'}, {"sh", 'u'}}},
        {"natural", "自然码双拼", SchemeKind::natural, natural_finals,
            {{"zh", 'v'}, {"ch", 'i'}, {"sh", 'u'}}},
        {"mspy", "微软双拼", SchemeKind::microsoft, microsoft_finals,
            {{"zh", 'v'}, {"ch", 'i'}, {"sh", 'u'}}},
        {"abc", "智能 ABC 双拼", SchemeKind::abc, abc_finals,
            {{"zh", 'a'}, {"ch", 'e'}, {"sh", 'v'}}},
    };
}

[[nodiscard]] std::pair<std::string, std::string> split_initial(const std::string& syllable) {
    static const std::vector<std::string> initials = {
        "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
        "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
    };
    for (const auto& initial : initials) {
        if (syllable.starts_with(initial)) {
            return {initial, syllable.substr(initial.size())};
        }
    }
    return {"", syllable};
}

[[nodiscard]] std::string normalize_final(std::string initial, std::string final) {
    if ((initial == "j" || initial == "q" || initial == "x" || initial == "y") &&
        !final.empty() && final.front() == 'u') {
        final.front() = 'v';
    }
    return final;
}

[[nodiscard]] std::vector<std::string> encode_syllable(
    const SchemeDefinition& definition,
    const std::string& syllable) {
    auto [initial, final] = split_initial(syllable);
    final = normalize_final(initial, final);

    std::vector<std::string> codes;
    char final_key = '\0';
    const auto final_it = definition.finals.find(final);
    if (final_it != definition.finals.end()) {
        final_key = final_it->second;
    }

    if (initial.empty()) {
        if (syllable.empty() || (syllable.front() != 'a' && syllable.front() != 'e' && syllable.front() != 'o')) {
            return {};
        }
        if (definition.kind == SchemeKind::microsoft || definition.kind == SchemeKind::abc) {
            std::string code;
            code.push_back('o');
            code.push_back(final_key == '\0' ? syllable.back() : final_key);
            codes.push_back(code);
            if (definition.kind == SchemeKind::microsoft && (syllable.front() == 'a' || syllable.front() == 'e')) {
                std::string alternate;
                alternate.push_back(syllable.front());
                alternate.push_back(final_key == '\0' ? syllable.back() : final_key);
                codes.push_back(alternate);
            }
            return codes;
        }

        std::string code;
        code.push_back(syllable.front());
        if (definition.kind == SchemeKind::flypy) {
            // Xiaohe zero-initial syllables use aa/ee/oo for one letter,
            // the original spelling for two letters, and the regular final
            // key for three letters (ang -> ah, eng -> eg).
            if (syllable.size() == 1U) {
                code.push_back(syllable.front());
            } else if (syllable.size() == 2U) {
                code.push_back(syllable.back());
            } else if (final_key != '\0') {
                code.push_back(final_key);
            } else {
                return {};
            }
        } else if (syllable.size() == 1U) {
            code.push_back(syllable.front());
        } else if (final_key != '\0') {
            code.push_back(final_key);
        } else if (syllable.size() == 2U) {
            code.push_back(syllable.back());
        } else {
            return {};
        }
        codes.push_back(code);
        return codes;
    }

    char initial_key = initial.front();
    const auto initial_it = definition.initials.find(initial);
    if (initial_it != definition.initials.end()) {
        initial_key = initial_it->second;
    }

    std::string code;
    code.push_back(initial_key);
    if (final_key != '\0') {
        code.push_back(final_key);
    } else if (final.size() == 1U) {
        code.push_back(final.front());
    } else {
        return {};
    }
    codes.push_back(code);
    return codes;
}

[[nodiscard]] std::string normalize_code(const std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (const unsigned char current : input) {
        if ((current >= 'A' && current <= 'Z') || (current >= 'a' && current <= 'z')) {
            output.push_back(static_cast<char>(std::tolower(current)));
        } else if (current == ';' || current == '\'') {
            output.push_back(static_cast<char>(current));
        } else if (current == ' ') {
            if (!output.empty() && output.back() != '\'') {
                output.push_back('\'');
            }
        } else {
            throw std::invalid_argument("Unsupported character in shuangpin input");
        }
    }
    while (!output.empty() && output.back() == '\'') {
        output.pop_back();
    }
    return output;
}

[[nodiscard]] std::vector<std::string> split_codes(const std::string& input) {
    std::vector<std::string> codes;
    if (input.find('\'') != std::string::npos) {
        std::size_t start = 0U;
        for (std::size_t index = 0U; index <= input.size(); ++index) {
            if (index == input.size() || input[index] == '\'') {
                if (index - start != 2U) {
                    return {};
                }
                codes.push_back(input.substr(start, 2U));
                start = index + 1U;
            }
        }
        return codes;
    }
    if ((input.size() % 2U) != 0U) {
        return {};
    }
    for (std::size_t index = 0U; index < input.size(); index += 2U) {
        codes.push_back(input.substr(index, 2U));
    }
    return codes;
}

}  // namespace

ShuangpinDecoder::ShuangpinDecoder() {
    for (const auto& definition : definitions()) {
        SchemeData data;
        data.info = {definition.id, definition.name};
        for (const auto& syllable : PinyinSegmenter::standard_syllables()) {
            for (const auto& code : encode_syllable(definition, syllable)) {
                data.code_to_syllables[code].push_back(syllable);
            }
        }
        for (auto& [code, syllables] : data.code_to_syllables) {
            (void)code;
            std::sort(syllables.begin(), syllables.end());
            syllables.erase(std::unique(syllables.begin(), syllables.end()), syllables.end());
        }
        scheme_infos_.push_back(data.info);
        schemes_.emplace(data.info.id, std::move(data));
    }
}

const std::vector<ShuangpinSchemeInfo>& ShuangpinDecoder::schemes() const noexcept {
    return scheme_infos_;
}

bool ShuangpinDecoder::has_scheme(const std::string_view scheme_id) const {
    return schemes_.contains(std::string(scheme_id));
}

std::vector<std::string> ShuangpinDecoder::syllables_for_code(
    const std::string_view scheme_id,
    const std::string_view code) const {
    const auto scheme_it = schemes_.find(std::string(scheme_id));
    if (scheme_it == schemes_.end()) {
        return {};
    }
    const std::string normalized = normalize_code(code);
    const auto found = scheme_it->second.code_to_syllables.find(normalized);
    return found == scheme_it->second.code_to_syllables.end() ? std::vector<std::string>{} : found->second;
}

std::vector<PinyinSegmentation> ShuangpinDecoder::decode(
    const std::string_view scheme_id,
    const std::string_view raw_input,
    const std::size_t limit) const {
    if (limit == 0U) {
        return {};
    }
    const auto scheme_it = schemes_.find(std::string(scheme_id));
    if (scheme_it == schemes_.end()) {
        throw std::invalid_argument("Unknown shuangpin scheme: " + std::string(scheme_id));
    }
    const std::string normalized = normalize_code(raw_input);
    const auto codes = split_codes(normalized);
    if (codes.empty()) {
        return {};
    }

    std::vector<PinyinSegmentation> results(1U);
    for (const auto& code : codes) {
        const auto found = scheme_it->second.code_to_syllables.find(code);
        if (found == scheme_it->second.code_to_syllables.end()) {
            return {};
        }
        std::vector<PinyinSegmentation> next;
        for (const auto& current : results) {
            for (const auto& syllable : found->second) {
                PinyinSegmentation candidate = current;
                candidate.syllables.push_back(syllable);
                candidate.score += static_cast<int>(syllable.size() * syllable.size() * 10U);
                candidate.canonical = PinyinSegmenter::join(candidate.syllables);
                next.push_back(std::move(candidate));
                if (next.size() >= limit * 4U) {
                    break;
                }
            }
            if (next.size() >= limit * 4U) {
                break;
            }
        }
        std::stable_sort(next.begin(), next.end(), [](const PinyinSegmentation& left, const PinyinSegmentation& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.canonical < right.canonical;
        });
        if (next.size() > limit) {
            next.resize(limit);
        }
        results = std::move(next);
    }
    return results;
}

}  // namespace piinput
