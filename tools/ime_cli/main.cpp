#include "piinput/engine.h"
#include "piinput/symbols.h"
#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "PiInput Core CLI v" << PIINPUT_VERSION << "\n\n"
        << "Dictionary query:\n"
        << "  piinput-cli --lexicon <dictionary.tsv|dictionary.lex> --query <input>\n"
        << "               [--schema full|flypy|natural|mspy|abc] [--top N] [--show-decode]\n\n"
        << "Decode only:\n"
        << "  piinput-cli --decode <input> [--schema ...] [--top N]\n\n"
        << "Symbol search:\n"
        << "  piinput-cli --symbols <symbols.tsv> --symbol-query <name|pinyin|english> [--top N]\n\n"
        << "Other:\n"
        << "  piinput-cli --list-schemas\n";
}

[[nodiscard]] std::size_t parse_size(const std::string& value) {
    std::size_t processed = 0U;
    const unsigned long long number = std::stoull(value, &processed, 10);
    if (processed != value.size()) {
        throw std::runtime_error("Invalid numeric value: " + value);
    }
    return static_cast<std::size_t>(number);
}

int run(const std::vector<std::string>& arguments) {
    if (arguments.size() <= 1U || arguments[1] == "--help" || arguments[1] == "-h") {
        print_usage();
        return 0;
    }

    std::string lexicon_path;
    std::string query;
    std::string decode_input;
    std::string schema = "full";
    std::string symbols_path;
    std::string symbol_query;
    std::size_t top = 10U;
    bool show_decode = false;
    bool list_schemas = false;

    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        auto require_value = [&]() -> const std::string& {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error(argument + " requires a value");
            }
            return arguments[++index];
        };
        if (argument == "--lexicon") {
            lexicon_path = require_value();
        } else if (argument == "--query") {
            query = require_value();
        } else if (argument == "--decode") {
            decode_input = require_value();
        } else if (argument == "--schema") {
            schema = require_value();
        } else if (argument == "--top") {
            top = parse_size(require_value());
        } else if (argument == "--show-decode") {
            show_decode = true;
        } else if (argument == "--list-schemas") {
            list_schemas = true;
        } else if (argument == "--symbols") {
            symbols_path = require_value();
        } else if (argument == "--symbol-query") {
            symbol_query = require_value();
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    piinput::Engine engine;
    if (list_schemas) {
        std::cout << "full\t全拼\n";
        for (const auto& item : engine.shuangpin().schemes()) {
            std::cout << item.id << '\t' << item.name << '\n';
        }
        return 0;
    }

    if (!symbols_path.empty() || !symbol_query.empty()) {
        if (symbols_path.empty() || symbol_query.empty()) {
            throw std::runtime_error("Both --symbols and --symbol-query are required");
        }
        piinput::SymbolIndex symbols;
        symbols.load_tsv(piinput::path_from_utf8(symbols_path));
        const auto results = symbols.search(symbol_query, top);
        std::cout << "Loaded symbols: " << symbols.entry_count() << "\n";
        for (std::size_t index = 0U; index < results.size(); ++index) {
            std::cout << index + 1U << ". " << results[index].symbol << '\t'
                      << results[index].name << '\t' << results[index].category << '\n';
        }
        return results.empty() ? 2 : 0;
    }

    const std::string effective_input = !decode_input.empty() ? decode_input : query;
    if (effective_input.empty()) {
        throw std::runtime_error("Missing --query or --decode");
    }

    const auto decoded = engine.decode(effective_input, schema, top);
    if (!decode_input.empty() || show_decode) {
        std::cout << "Decoded pinyin:\n";
        for (std::size_t index = 0U; index < decoded.size(); ++index) {
            std::cout << "  " << index + 1U << ". " << decoded[index].canonical << '\n';
        }
        if (!decode_input.empty()) {
            return decoded.empty() ? 2 : 0;
        }
    }

    if (lexicon_path.empty()) {
        throw std::runtime_error("--lexicon is required for dictionary query");
    }
    engine.load_lexicon(piinput::path_from_utf8(lexicon_path));
    const auto candidates = engine.query(query, schema, top);
    std::cout << "Loaded entries: " << engine.entry_count() << "\n";
    if (candidates.empty()) {
        std::cout << "No candidate for: " << query << "\n";
        return 2;
    }
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        std::cout << index + 1U << ". " << candidates[index].word << '\t'
                  << candidates[index].pinyin << '\t' << candidates[index].base_weight << '\n';
    }
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(piinput::wide_to_utf8(argv[index]));
        }
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(const int argc, char* argv[]) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#endif
