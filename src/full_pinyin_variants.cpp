#include "piinput/full_pinyin_variants.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace piinput {
namespace {

[[nodiscard]] bool is_ascii_letter(const char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] bool uses_u_for_umlaut(const char initial) {
    return initial == 'j' || initial == 'q' || initial == 'x' || initial == 'y';
}

[[nodiscard]] bool uses_v_for_umlaut(const char initial) {
    return initial == 'l' || initial == 'n';
}

}  // namespace

std::vector<std::string> normalize_full_pinyin_variants(
    const std::string_view input,
    const PinyinSettings& settings,
    const std::size_t variant_limit) {
    if (variant_limit == 0U || input.empty()) {
        return {};
    }

    std::string normalized;
    normalized.reserve(input.size());
    // True only for an ASCII v/V. UTF-8 ü and u: are standard spelling inputs
    // and therefore do not depend on uv_compatibility.
    std::vector<bool> ascii_v;
    ascii_v.reserve(input.size());

    for (std::size_t index = 0U; index < input.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(input[index]);
        if (current < 0x80U) {
            const char character = static_cast<char>(current);
            if (is_ascii_letter(character)) {
                const char lower = static_cast<char>(std::tolower(current));
                normalized.push_back(lower);
                ascii_v.push_back(lower == 'v');
                continue;
            }
            if (character == '\'' || character == ' ') {
                if (!normalized.empty() && normalized.back() != '\'') {
                    normalized.push_back('\'');
                    ascii_v.push_back(false);
                }
                continue;
            }
            if (character == ':' && settings.accept_u_colon && normalized.size() >= 2U &&
                normalized.back() == 'u' && uses_v_for_umlaut(normalized[normalized.size() - 2U])) {
                normalized.back() = 'v';
                ascii_v.back() = false;
                continue;
            }
            return {};
        }

        if (index + 1U < input.size() && current == 0xC3U &&
            (static_cast<unsigned char>(input[index + 1U]) == 0xBCU ||
             static_cast<unsigned char>(input[index + 1U]) == 0x9CU)) {
            normalized.push_back('v');
            ascii_v.push_back(false);
            ++index;
            continue;
        }
        return {};
    }

    while (!normalized.empty() && normalized.back() == '\'') {
        normalized.pop_back();
        ascii_v.pop_back();
    }
    if (normalized.empty()) {
        return {};
    }

    std::string canonical = normalized;
    std::vector<std::size_t> boundary_points;
    for (std::size_t index = 0U; index < canonical.size(); ++index) {
        if (canonical[index] != 'v') {
            continue;
        }
        if (index == 0U) {
            return {};
        }
        const char initial = canonical[index - 1U];
        if (uses_u_for_umlaut(initial)) {
            if (ascii_v[index] && !settings.uv_compatibility) {
                return {};
            }
            canonical[index] = 'u';
            continue;
        }
        if (!uses_v_for_umlaut(initial)) {
            return {};
        }
        // This project's syllable table writes lüe/nüe as lue/nue, while the
        // standalone syllables stay lv/nv. Dictionaries need not agree -- the
        // imported one stores 忽略 as hu'lve -- and Engine::query_exact_unlocked
        // covers that by retrying the other spelling when a lookup comes back
        // empty. What is handled here is a different ambiguity: in continuous
        // input ASCII lve/nve can also be lv + e, so both readings are kept.
        if (index + 1U < canonical.size() && canonical[index + 1U] == 'e') {
            if (ascii_v[index]) {
                boundary_points.push_back(index);
            } else {
                canonical[index] = 'u';
            }
        }
    }

    std::vector<std::string> variants{std::move(canonical)};
    for (const std::size_t point : boundary_points) {
        std::vector<std::string> expanded;
        expanded.reserve((std::min)(variant_limit, variants.size() * 2U));
        for (const auto& variant : variants) {
            if (expanded.size() == variant_limit) {
                break;
            }
            std::string extended = variant;
            extended[point] = 'u';
            expanded.push_back(std::move(extended));
            if (expanded.size() < variant_limit) {
                std::string separated = variant;
                separated[point] = 'v';
                expanded.push_back(std::move(separated));
            }
        }
        variants = std::move(expanded);
    }
    return variants;
}

}  // namespace piinput
