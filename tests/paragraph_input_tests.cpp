#include "piinput/engine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1U;
    }
    return fields;
}

std::vector<std::string> split_utf8(const std::string& text) {
    std::vector<std::string> result;
    for (std::size_t index = 0U; index < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1U;
        if ((lead & 0xE0U) == 0xC0U) length = 2U;
        else if ((lead & 0xF0U) == 0xE0U) length = 3U;
        else if ((lead & 0xF8U) == 0xF0U) length = 4U;
        if (index + length > text.size()) return {};
        result.push_back(text.substr(index, length));
        index += length;
    }
    return result;
}

void verify_case(
    const piinput::Engine& engine,
    const std::string& target,
    const std::string& input,
    const std::string& schema,
    const std::string& canonical) {
    const auto parsed = engine.parse_composition(input, schema);
    check(parsed.has_value(), schema + " parses paragraph input");
    if (!parsed.has_value()) return;
    check(parsed->canonical == canonical, schema + " preserves paragraph syllable boundaries");
    const auto characters = split_utf8(target);
    check(characters.size() == parsed->syllables.size(),
        schema + " target character count matches syllable count");
    if (characters.size() != parsed->syllables.size()) return;
    for (std::size_t offset = 0U; offset < characters.size(); ++offset) {
        const auto candidates = engine.query_segment(*parsed, offset, 90U);
        const bool found = std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.word == characters[offset] && candidate.consumed_syllables == 1U;
        });
        check(found, schema + " can select character " + characters[offset] +
            " at syllable " + std::to_string(offset + 1U));
    }
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: piinput-paragraph-input-tests <lexicon> <cases.tsv>\n";
        return 2;
    }
    piinput::Engine engine;
    engine.load_lexicon(std::filesystem::path(argv[1]));
    std::ifstream input(argv[2], std::ios::binary);
    std::string line;
    std::size_t cases = 0U;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tabs(line);
        check(fields.size() == 4U, "paragraph row has four fields");
        if (fields.size() != 4U) continue;
        verify_case(engine, fields[0], fields[1], "full", fields[3]);
        verify_case(engine, fields[0], fields[2], "flypy", fields[3]);
        ++cases;
    }
    check(cases == 3U, "all three supplied paragraph cases are loaded");
    if (failures != 0) {
        std::cerr << failures << " paragraph input test(s) failed\n";
        return 1;
    }
    std::cout << "All paragraph input tests passed\n";
    return 0;
}
