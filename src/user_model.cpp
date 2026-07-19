#include "piinput/user_model.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace piinput {
namespace {

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(line.data() + start, line.size() - start);
            break;
        }
        fields.emplace_back(line.data() + start, tab - start);
        start = tab + 1U;
    }
    return fields;
}

[[nodiscard]] std::uint64_t now_seconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

template <typename T>
[[nodiscard]] T parse_integer(const std::string_view value, const std::size_t line_number) {
    T output{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::runtime_error("Invalid user model number at line " + std::to_string(line_number));
    }
    return output;
}

}  // namespace

std::string UserModel::make_key(const std::string& pinyin, const std::string& word) {
    return pinyin + '\n' + word;
}

void UserModel::load(const std::filesystem::path& path) {
    entries_.clear();
    if (!std::filesystem::exists(path)) {
        return;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open user model: " + path.string());
    }
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto fields = split_tabs(line);
        if (fields.size() < 4U) {
            throw std::runtime_error("Invalid user model line " + std::to_string(line_number));
        }
        if (fields[0] == "pinyin" && fields[1] == "word") {
            continue;
        }
        entries_[make_key(std::string(fields[0]), std::string(fields[1]))] = Entry{
            parse_integer<std::uint32_t>(fields[2], line_number),
            parse_integer<std::uint64_t>(fields[3], line_number),
        };
    }
}

void UserModel::save(const std::filesystem::path& path) const {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create user model: " + temporary);
    }
    output << "# PiInput user selection model\n"
           << "pinyin\tword\tcount\tlast_used\n";

    std::vector<std::pair<std::string, Entry>> ordered(entries_.begin(), entries_.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (const auto& [key, entry] : ordered) {
        const std::size_t separator = key.find('\n');
        if (separator == std::string::npos) {
            continue;
        }
        output << key.substr(0U, separator) << '\t'
               << key.substr(separator + 1U) << '\t'
               << entry.count << '\t' << entry.last_used << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Failed while writing user model");
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace user model: " + error.message());
    }
}

void UserModel::record_selection(const std::string& pinyin, const std::string& word) {
    auto& entry = entries_[make_key(pinyin, word)];
    if (entry.count < std::numeric_limits<std::uint32_t>::max()) {
        ++entry.count;
    }
    entry.last_used = now_seconds();
}

void UserModel::remove(const std::string& pinyin, const std::string& word) {
    entries_.erase(make_key(pinyin, word));
}

int UserModel::score_adjustment(const std::string& pinyin, const std::string& word) const {
    const auto found = entries_.find(make_key(pinyin, word));
    if (found == entries_.end()) {
        return 0;
    }
    const std::uint64_t now = now_seconds();
    const std::uint64_t age = now > found->second.last_used ? now - found->second.last_used : 0U;
    int recency = 0;
    if (age < 3600U) {
        recency = 120000;
    } else if (age < 86400U) {
        recency = 80000;
    } else if (age < 7U * 86400U) {
        recency = 40000;
    }
    const std::uint32_t capped_count = (std::min)(found->second.count, 1000U);
    return static_cast<int>(capped_count) * 50000 + recency;
}

std::size_t UserModel::entry_count() const noexcept {
    return entries_.size();
}

}  // namespace piinput
