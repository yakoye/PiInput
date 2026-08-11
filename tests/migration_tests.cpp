#include "migration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

}  // namespace

int main() {
    using namespace piinput::windows::installer;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("piinput-migration-" + std::to_string(nonce));
    const auto source = root / "legacy" / "UserData";
    const auto destination = root / "PiInput" / "UserData";
    write_file(source / "settings.ini", "[general]\nschema=flypy\n");
    write_file(destination / "settings.ini", "[general]\nschema=full\n");
    write_file(source / "user_model.tsv", "word\t1\n");
    write_file(source / "lexicons" / "same.lex", "same");
    write_file(destination / "lexicons" / "same.lex", "same");
    write_file(root / "legacy" / "Dev" / "versions" / "old" / "bin" / "input-tsf.dll",
        "locked runtime fixture");

    const auto plan = plan_migration(source, destination);
    check(plan.copy_files.size() == 2U,
        "new and conflicting user files are both copied");
    check(plan.conflicts.size() == 1U,
        "a different destination becomes a conflict copy");
    check(plan.delete_source_after_verification,
        "source removal is gated by verification");
    check(plan.conflicts.front().conflict_copy.filename().wstring().find(L"legacy-import") !=
            std::wstring::npos,
        "conflict copy has a recognizable stable suffix");

    check(is_safe_migration_source(root / "legacy", root, root / "PiInput"),
        "an absolute sibling beneath LocalAppData is accepted");
    check(!is_safe_migration_source(root, root, root / "PiInput"),
        "the LocalAppData root itself is rejected");
    check(!is_safe_migration_source(root / "PiInput", root, root / "PiInput"),
        "the PiInput destination cannot migrate onto itself");
    check(!is_safe_migration_source(std::filesystem::path("relative"), root, root / "PiInput"),
        "relative migration sources are rejected");

    const auto discovered = discover_legacy_runtime(root, root / "PiInput");
    check(discovered == std::optional<std::filesystem::path>(root / "legacy"),
        "installer discovers one structurally valid legacy runtime without a brand name");

    const auto deletion = delayed_delete_order(root / "legacy");
    check(!deletion.empty() && deletion.back() == root / "legacy",
        "delayed deletion removes children before the legacy root");
    for (std::size_t index = 1U; index < deletion.size(); ++index) {
        check(deletion[index - 1U].native().size() >= deletion[index].native().size(),
            "delayed deletion order is deepest first");
    }

    migrate_legacy_user_data(root / "legacy", root / "PiInput");
    check(std::filesystem::is_regular_file(destination / "user_model.tsv"),
        "migration copies new user learning data");
    check(std::filesystem::is_regular_file(destination / "settings.ini.legacy-import"),
        "migration preserves a conflicting legacy settings copy");
    check(std::filesystem::is_directory(source),
        "verified migration does not delete the source before installation completes");

    const auto damaged_root = root / "damaged";
    write_file(damaged_root / "legacy" / "UserData" / "settings.ini",
        std::string("[general]\n", 10U) + std::string(1U, '\0'));
    bool damaged_failed = false;
    try {
        migrate_legacy_user_data(damaged_root / "legacy", damaged_root / "PiInput");
    } catch (...) {
        damaged_failed = true;
    }
    check(damaged_failed, "damaged settings abort migration");
    check(std::filesystem::is_directory(damaged_root / "legacy" / "UserData"),
        "failed migration preserves the legacy source");
    check(!std::filesystem::exists(damaged_root / "PiInput" / "UserData"),
        "failed migration does not publish partial user data");

    std::filesystem::remove_all(root);
    if (failures != 0) {
        std::cerr << failures << " migration test(s) failed\n";
        return 1;
    }
    std::cout << "PiInput migration tests passed\n";
    return 0;
}
