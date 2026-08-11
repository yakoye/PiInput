#include "migration.h"

#include "piinput/settings.h"
#include "piinput/windows_compat.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

namespace piinput::windows::installer {
namespace {

[[nodiscard]] std::filesystem::path normalized(const std::filesystem::path& path) {
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    if (error) {
        result = std::filesystem::absolute(path, error).lexically_normal();
    }
    return result;
}

[[nodiscard]] bool is_descendant(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) {
    const auto normalized_child = normalized(child);
    const auto normalized_parent = normalized(parent);
    auto child_part = normalized_child.begin();
    for (auto parent_part = normalized_parent.begin(); parent_part != normalized_parent.end();
         ++parent_part, ++child_part) {
        if (child_part == normalized_child.end() || *child_part != *parent_part) {
            return false;
        }
    }
    return normalized_child != normalized_parent;
}

[[nodiscard]] bool files_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::file_size(left, error) != std::filesystem::file_size(right, error) || error) {
        return false;
    }
    std::ifstream left_input(left, std::ios::binary);
    std::ifstream right_input(right, std::ios::binary);
    return left_input && right_input &&
        std::equal(std::istreambuf_iterator<char>(left_input), {},
            std::istreambuf_iterator<char>(right_input), {});
}

[[nodiscard]] std::filesystem::path conflict_name(const std::filesystem::path& destination) {
    auto candidate = destination;
    candidate += L".legacy-import";
    for (unsigned index = 1U; std::filesystem::exists(candidate); ++index) {
        candidate = destination;
        candidate += L".legacy-import-" + std::to_wstring(index);
    }
    return candidate;
}

void copy_tree(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!std::filesystem::is_directory(source)) {
        return;
    }
    for (const auto& item : std::filesystem::recursive_directory_iterator(source)) {
        const auto relative = std::filesystem::relative(item.path(), source);
        const auto target = destination / relative;
        if (item.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (item.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(item.path(), target,
                std::filesystem::copy_options::overwrite_existing);
        }
    }
}

void validate_staging(const std::filesystem::path& staging) {
    for (const auto& item : std::filesystem::recursive_directory_iterator(staging)) {
        if (!item.is_regular_file()) {
            continue;
        }
        std::ifstream input(item.path(), std::ios::binary);
        if (!input) {
            throw std::runtime_error("Migrated user file is unreadable");
        }
    }
    const auto settings = staging / L"settings.ini";
    if (std::filesystem::is_regular_file(settings)) {
        std::ifstream input(settings, std::ios::binary);
        const std::string text{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (text.find('\0') != std::string::npos ||
            parse_settings_text(text, default_settings()).document_fatal) {
            throw std::runtime_error("Migrated settings.ini is damaged");
        }
    }
}

[[nodiscard]] std::filesystem::path unique_sibling(
    const std::filesystem::path& base,
    const wchar_t* suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto result = base;
    result += std::wstring(suffix) + L"." + std::to_wstring(nonce);
    return result;
}

}  // namespace

MigrationPlan plan_migration(
    const std::filesystem::path& source_user_data,
    const std::filesystem::path& destination_user_data) {
    MigrationPlan plan;
    if (!std::filesystem::is_directory(source_user_data)) {
        return plan;
    }
    for (const auto& item : std::filesystem::recursive_directory_iterator(source_user_data)) {
        if (!item.is_regular_file()) {
            continue;
        }
        const auto relative = std::filesystem::relative(item.path(), source_user_data);
        const auto destination = destination_user_data / relative;
        if (!std::filesystem::exists(destination)) {
            plan.copy_files.emplace_back(item.path(), destination);
        } else if (!files_equal(item.path(), destination)) {
            const auto conflict = conflict_name(destination);
            plan.copy_files.emplace_back(item.path(), conflict);
            plan.conflicts.push_back({item.path(), destination, conflict});
        }
    }
    plan.delete_source_after_verification = true;
    return plan;
}

bool is_safe_migration_source(
    const std::filesystem::path& source_root,
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& piinput_root) {
    if (!source_root.is_absolute()) {
        return false;
    }
    const auto source = normalized(source_root);
    const auto local = normalized(local_app_data);
    const auto destination = normalized(piinput_root);
    return source != local && source != destination && is_descendant(source, local);
}

std::vector<std::filesystem::path> delayed_delete_order(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    if (std::filesystem::is_directory(root, error)) {
        for (const auto& item : std::filesystem::recursive_directory_iterator(root, error)) {
            if (error) {
                break;
            }
            paths.push_back(item.path());
        }
    }
    paths.push_back(root);
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.native().size() > right.native().size();
    });
    return paths;
}

std::optional<std::filesystem::path> discover_legacy_runtime(
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& piinput_root) {
    std::vector<std::filesystem::path> matches;
    std::error_code error;
    for (const auto& child : std::filesystem::directory_iterator(local_app_data, error)) {
        if (error || !child.is_directory() || normalized(child.path()) == normalized(piinput_root)) {
            error.clear();
            continue;
        }
        const auto user_data = child.path() / L"UserData";
        const auto developer = child.path() / L"Dev";
        if (!std::filesystem::is_regular_file(user_data / L"settings.ini") ||
            !std::filesystem::is_directory(developer)) {
            continue;
        }
        bool has_tsf_dll = false;
        for (const auto& item : std::filesystem::recursive_directory_iterator(developer, error)) {
            if (error) {
                break;
            }
            if (!item.is_regular_file() || item.path().extension() != L".dll") {
                continue;
            }
            std::wstring name = item.path().filename().wstring();
            std::transform(name.begin(), name.end(), name.begin(), [](const wchar_t character) {
                return static_cast<wchar_t>(std::towlower(character));
            });
            if (name.find(L"tsf") != std::wstring::npos) {
                has_tsf_dll = true;
                break;
            }
        }
        error.clear();
        if (has_tsf_dll) {
            matches.push_back(child.path());
        }
    }
    return matches.size() == 1U
        ? std::optional<std::filesystem::path>(matches.front())
        : std::nullopt;
}

void migrate_legacy_user_data(
    const std::filesystem::path& source_root,
    const std::filesystem::path& piinput_root) {
    const auto source = source_root / L"UserData";
    if (!std::filesystem::is_directory(source)) {
        return;
    }
    const auto destination = piinput_root / L"UserData";
    const auto staging = unique_sibling(destination, L".migrating");
    const auto backup = unique_sibling(destination, L".backup");
    try {
        std::filesystem::create_directories(staging);
        copy_tree(destination, staging);
        const auto plan = plan_migration(source, destination);
        for (const auto& [from, to] : plan.copy_files) {
            const auto relative = std::filesystem::relative(to, destination);
            const auto staged_target = staging / relative;
            std::filesystem::create_directories(staged_target.parent_path());
            std::filesystem::copy_file(from, staged_target,
                std::filesystem::copy_options::overwrite_existing);
        }
        validate_staging(staging);
        const bool had_destination = std::filesystem::exists(destination);
        if (had_destination) {
            std::filesystem::rename(destination, backup);
        }
        try {
            std::filesystem::rename(staging, destination);
            validate_staging(destination);
            std::error_code ignored;
            std::filesystem::remove_all(backup, ignored);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(destination, ignored);
            if (had_destination && std::filesystem::exists(backup)) {
                std::filesystem::rename(backup, destination);
            }
            throw;
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
}

void remove_or_schedule_legacy_runtime(const std::filesystem::path& source_root) {
    std::error_code error;
    std::filesystem::remove_all(source_root, error);
    if (!error && !std::filesystem::exists(source_root)) {
        return;
    }
#ifdef _WIN32
    for (const auto& path : delayed_delete_order(source_root)) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        if (MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) == FALSE) {
            throw std::runtime_error("Cannot schedule the legacy runtime for deletion");
        }
    }
#else
    throw std::runtime_error("Cannot remove the legacy runtime");
#endif
}

}  // namespace piinput::windows::installer
