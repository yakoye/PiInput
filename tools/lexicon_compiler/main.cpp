#include "liteime/binary_lexicon.h"
#include "liteime/utf.h"
#include "liteime/windows_compat.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int run(const std::vector<std::string>& arguments) {
    if (arguments.size() == 2U && (arguments[1] == "--help" || arguments[1] == "-h")) {
        std::cout << "LiteIME Lexicon Compiler v" << LITEIME_VERSION << "\n\n"
                  << "Usage: liteime-lexicon-compiler --input dictionary.tsv --output dictionary.lex\n";
        return 0;
    }
    std::string input;
    std::string output;
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        if (arguments[index] == "--input" && index + 1U < arguments.size()) {
            input = arguments[++index];
        } else if (arguments[index] == "--output" && index + 1U < arguments.size()) {
            output = arguments[++index];
        } else {
            throw std::runtime_error("Unknown or incomplete argument: " + arguments[index]);
        }
    }
    if (input.empty() || output.empty()) {
        throw std::runtime_error("Both --input and --output are required");
    }
    liteime::compile_tsv_to_binary(liteime::path_from_utf8(input), liteime::path_from_utf8(output));
    liteime::BinaryLexicon lexicon;
    lexicon.load(liteime::path_from_utf8(output));
    std::cout << "Compiled " << lexicon.entry_count() << " entries to " << output << "\n";
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        std::vector<std::string> arguments;
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(liteime::wide_to_utf8(argv[index]));
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
