#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace piinput::windows::installer {

struct MigrationConflict {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::filesystem::path conflict_copy;
};

struct MigrationPlan {
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copy_files;
    std::vector<MigrationConflict> conflicts;
    bool delete_source_after_verification{};
};

[[nodiscard]] MigrationPlan plan_migration(
    const std::filesystem::path& source_user_data,
    const std::filesystem::path& destination_user_data);

[[nodiscard]] bool is_safe_migration_source(
    const std::filesystem::path& source_root,
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& piinput_root);

[[nodiscard]] std::vector<std::filesystem::path> delayed_delete_order(
    const std::filesystem::path& root);

[[nodiscard]] std::optional<std::filesystem::path> discover_legacy_runtime(
    const std::filesystem::path& local_app_data,
    const std::filesystem::path& piinput_root);

void migrate_legacy_user_data(
    const std::filesystem::path& source_root,
    const std::filesystem::path& piinput_root);

void remove_or_schedule_legacy_runtime(const std::filesystem::path& source_root);

}  // namespace piinput::windows::installer
