#include "piinput/dictionary_builder.h"
#include "piinput/utf.h"
#include "piinput/windows_compat.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] piinput::DictionarySourceFormat parse_format(const std::string& value) {
    if (value == "tsv") return piinput::DictionarySourceFormat::piinput_tsv;
    if (value == "rime") return piinput::DictionarySourceFormat::rime_yaml;
    if (value == "pinyin-data") return piinput::DictionarySourceFormat::pinyin_data;
    if (value == "phrase-pinyin-data") return piinput::DictionarySourceFormat::phrase_pinyin_data;
    if (value == "thuocl") return piinput::DictionarySourceFormat::thuocl;
    throw std::runtime_error("Unknown source format: " + value);
}

int run(const std::vector<std::string>& arguments) {
    struct PendingThuoclSource {
        std::filesystem::path path;
        std::uint32_t base_weight{};
    };
    std::filesystem::path output;
    std::filesystem::path report_path;
    std::filesystem::path rime_report_path;
    std::vector<piinput::LexiconCandidate> combined;
    std::vector<PendingThuoclSource> pending_thuocl;
    std::optional<piinput::RimeDictionaryImportReport> rime_report;
    for (std::size_t index = 1U; index < arguments.size(); ++index) {
        if (arguments[index] == "--output" && index + 1U < arguments.size()) {
            output = piinput::path_from_utf8(arguments[++index]);
        } else if (arguments[index] == "--report" && index + 1U < arguments.size()) {
            report_path = piinput::path_from_utf8(arguments[++index]);
        } else if (arguments[index] == "--rime-report" && index + 1U < arguments.size()) {
            rime_report_path = piinput::path_from_utf8(arguments[++index]);
        } else if (arguments[index] == "--rime-dictionary" && index + 2U < arguments.size()) {
            const auto path = piinput::path_from_utf8(arguments[++index]);
            const auto weight = static_cast<std::uint32_t>(std::stoul(arguments[++index]));
            piinput::RimeDictionaryImportReport current_report;
            auto entries = piinput::read_rime_dictionary(path, weight, current_report);
            combined.insert(combined.end(),
                std::make_move_iterator(entries.begin()),
                std::make_move_iterator(entries.end()));
            rime_report = std::move(current_report);
        } else if (arguments[index] == "--source" && index + 3U < arguments.size()) {
            const auto format = parse_format(arguments[++index]);
            const auto path = piinput::path_from_utf8(arguments[++index]);
            const auto weight = static_cast<std::uint32_t>(std::stoul(arguments[++index]));
            if (format == piinput::DictionarySourceFormat::thuocl) {
                pending_thuocl.push_back({path, weight});
            } else {
                auto entries = piinput::read_dictionary_source(path, format, weight);
                combined.insert(combined.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
            }
        } else if (arguments[index] == "--help" || arguments[index] == "-h") {
            std::cout << "piinput-dictionary-builder --output <file.tsv> "
                         "[--report <file.tsv>] "
                         "[--rime-report <file.tsv>] "
                         "[--rime-dictionary <master.dict.yaml> <default-weight>] "
                         "--source <tsv|rime|pinyin-data|phrase-pinyin-data|thuocl> "
                         "<path> <default-weight> [...]\n";
            return 0;
        } else {
            throw std::runtime_error("Invalid or incomplete argument: " + arguments[index]);
        }
    }
    if (output.empty() || combined.empty()) {
        throw std::runtime_error("At least one --source and --output are required");
    }
    piinput::DictionaryBuildReport total_report;
    for (const auto& source : pending_thuocl) {
        const auto terms = piinput::read_thuocl_terms(source.path);
        piinput::DictionaryBuildReport source_report;
        auto entries = piinput::resolve_dictionary_terms(
            terms, combined, source.base_weight, source_report);
        total_report.exact_phrase_count += source_report.exact_phrase_count;
        total_report.character_derived_count += source_report.character_derived_count;
        total_report.unresolved_count += source_report.unresolved_count;
        combined.insert(
            combined.end(),
            std::make_move_iterator(entries.begin()),
            std::make_move_iterator(entries.end()));
    }
    piinput::write_dictionary_tsv(output, std::move(combined));
    if (!report_path.empty()) {
        std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
        if (!report) {
            throw std::runtime_error("Cannot create dictionary build report");
        }
        report << "metric\tcount\n"
               << "exact_phrase\t" << total_report.exact_phrase_count << '\n'
               << "character_derived\t" << total_report.character_derived_count << '\n'
               << "unresolved\t" << total_report.unresolved_count << '\n';
        if (!report) {
            throw std::runtime_error("Failed while writing dictionary build report");
        }
    }
    if (!rime_report_path.empty()) {
        if (!rime_report.has_value()) {
            throw std::runtime_error("--rime-report requires --rime-dictionary");
        }
        std::ofstream report(rime_report_path, std::ios::binary | std::ios::trunc);
        if (!report) {
            throw std::runtime_error("Cannot create Rime dictionary report");
        }
        report << "file\traw_entries\taccepted_entries\tduplicate_entries\n";
        for (const auto& source : rime_report->sources) {
            report << source.path.generic_string() << '\t'
                   << source.raw_entries << '\t'
                   << source.accepted_entries << '\t'
                   << source.duplicate_entries << '\n';
        }
        if (!report) {
            throw std::runtime_error("Failed while writing Rime dictionary report");
        }
    }
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
