#include "piinput/dictionary_builder.h"
#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] piinput::DictionarySourceFormat parse_format(const std::string& value) {
    if (value == "tsv") return piinput::DictionarySourceFormat::piinput_tsv;
    if (value == "rime") return piinput::DictionarySourceFormat::rime_yaml;
    if (value == "pinyin-data") return piinput::DictionarySourceFormat::pinyin_data;
    if (value == "phrase-pinyin-data") return piinput::DictionarySourceFormat::phrase_pinyin_data;
    throw std::runtime_error("Unknown source format: " + value);
}

int run(const std::vector<std::string>& arguments) {
    std::filesystem::path output;
    std::vector<piinput::LexiconCandidate> combined;
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        if (arguments[index] == "--output" && index + 1U < arguments.size()) {
            output = piinput::path_from_utf8(arguments[++index]);
        } else if (arguments[index] == "--source" && index + 3U < arguments.size()) {
            const auto format = parse_format(arguments[++index]);
            const auto path = piinput::path_from_utf8(arguments[++index]);
            const auto weight = static_cast<std::uint32_t>(std::stoul(arguments[++index]));
            auto entries = piinput::read_dictionary_source(path, format, weight);
            combined.insert(combined.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
        } else if (arguments[index] == "--help" || arguments[index] == "-h") {
            std::cout << "piinput-dictionary-builder --output <file.tsv> "
                         "--source <tsv|rime|pinyin-data|phrase-pinyin-data> <path> <default-weight> [...]\n";
            return 0;
        } else {
            throw std::runtime_error("Invalid or incomplete argument: " + arguments[index]);
        }
    }
    if (output.empty() || combined.empty()) {
        throw std::runtime_error("At least one --source and --output are required");
    }
    piinput::write_dictionary_tsv(output, std::move(combined));
    std::cout << "Dictionary TSV written successfully.\n";
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        std::vector<std::string> arguments;
        for (int index = 0; index < argc; ++index) arguments.push_back(piinput::wide_to_utf8(argv[index]));
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(const int argc, char* argv[]) {
    try { return run(std::vector<std::string>(argv, argv + argc)); }
    catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
#endif
