#include "piinput/scel_parser.h"
#include "piinput/utf.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "piinput/windows_compat.h"

namespace {

void print_usage() {
    std::cout
        << "PiInput SCEL Converter v" << PIINPUT_VERSION << "\n\n"
        << "Usage:\n"
        << "  piinput-scel-converter --info <input.scel>\n"
        << "  piinput-scel-converter --input <input.scel> --output <output> "
           "[--format tsv|jsonl|txt] [--limit N]\n";
}

[[nodiscard]] std::string json_escape(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U) {
                static constexpr char hex[] = "0123456789abcdef";
                output += "\\u00";
                output.push_back(hex[(character >> 4U) & 0x0FU]);
                output.push_back(hex[character & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return output;
}

[[nodiscard]] std::string tsv_escape(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
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

    std::string input_value;
    std::string output_value;
    std::string format = "tsv";
    std::size_t limit = 0U;
    bool info_only = false;

    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "--info") {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error("--info requires a SCEL path");
            }
            info_only = true;
            input_value = arguments[++index];
        } else if (argument == "--input") {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error("--input requires a SCEL path");
            }
            input_value = arguments[++index];
        } else if (argument == "--output") {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error("--output requires a path");
            }
            output_value = arguments[++index];
        } else if (argument == "--format") {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error("--format requires tsv, jsonl, or txt");
            }
            format = arguments[++index];
        } else if (argument == "--limit") {
            if (index + 1U >= arguments.size()) {
                throw std::runtime_error("--limit requires a number");
            }
            limit = parse_size(arguments[++index]);
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    if (input_value.empty()) {
        throw std::runtime_error("Missing --input or --info");
    }

    piinput::ScelParser parser;
    const auto dictionary = parser.parse_file(piinput::path_from_utf8(input_value));

    if (info_only) {
        std::cout
            << "{\n"
            << "  \"title\": \"" << json_escape(dictionary.metadata.title) << "\",\n"
            << "  \"category\": \"" << json_escape(dictionary.metadata.category) << "\",\n"
            << "  \"description\": \"" << json_escape(dictionary.metadata.description) << "\",\n"
            << "  \"format_mask\": " << static_cast<unsigned int>(dictionary.metadata.format_mask) << ",\n"
            << "  \"pinyin_count\": " << dictionary.pinyin_table.size() << ",\n"
            << "  \"entry_count\": " << dictionary.entries.size() << "\n"
            << "}\n";
        return 0;
    }

    if (output_value.empty()) {
        throw std::runtime_error("Missing --output");
    }
    if (format != "tsv" && format != "jsonl" && format != "txt") {
        throw std::runtime_error("Unsupported format: " + format);
    }

    std::ofstream output(piinput::path_from_utf8(output_value), std::ios::binary);
    if (!output) {
        throw std::runtime_error("Cannot create output file: " + output_value);
    }

    const std::size_t count = limit == 0U
        ? dictionary.entries.size()
        : (std::min)(limit, dictionary.entries.size());

    if (format == "tsv") {
        output << "# PiInput SCEL conversion\n"
               << "# title=" << tsv_escape(dictionary.metadata.title) << "\n"
               << "# category=" << tsv_escape(dictionary.metadata.category) << "\n"
               << "# source_entries=" << dictionary.entries.size() << "\n"
               << "word\tpinyin\tweight\n";
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& entry = dictionary.entries[index];
            output << tsv_escape(entry.word) << '\t'
                   << tsv_escape(entry.pinyin) << '\t'
                   << entry.weight << '\n';
        }
    } else if (format == "jsonl") {
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& entry = dictionary.entries[index];
            output << "{\"word\":\"" << json_escape(entry.word)
                   << "\",\"pinyin\":\"" << json_escape(entry.pinyin)
                   << "\",\"weight\":" << entry.weight << "}\n";
        }
    } else {
        for (std::size_t index = 0U; index < count; ++index) {
            output << dictionary.entries[index].word << '\n';
        }
    }

    if (!output) {
        throw std::runtime_error("Failed while writing output file");
    }

    std::cout << "Converted " << count << " entries to " << output_value << "\n";
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
