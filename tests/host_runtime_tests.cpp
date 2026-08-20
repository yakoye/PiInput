#include "host_runtime.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_runtime_loads_packaged_dictionary_and_real_candidates() {
    piinput::windows::HostRuntime runtime;
    std::string error;
    const auto source = std::filesystem::path(PIINPUT_SOURCE_DIR);
    const auto user = source / L"build" / L"host-runtime-test-user-missing";
    std::error_code cleanup_error;
    std::filesystem::remove_all(user, cleanup_error);
    check(runtime.load({source / L"data", user}, error), "runtime loads packaged data");
    check(runtime.engine().entry_count() > 0U, "runtime dictionary is not empty");
    const auto candidates = runtime.engine().query("wo", "full", 8U);
    check(!candidates.empty() && candidates.front().word == "我",
        "runtime engine returns the expected common Chinese candidate");
    check(runtime.loaded_lexicon().filename() == L"base_lexicon.tsv",
        "runtime reports the selected packaged lexicon");
    check(runtime.prewarmed_prefix_count() == 26U,
        "runtime prewarms every first-letter path before publishing Host health");
}

void test_missing_dictionary_fails_before_pipe_accepts_clients() {
    piinput::windows::HostRuntime runtime;
    std::string error;
    const auto source = std::filesystem::path(PIINPUT_SOURCE_DIR);
    check(!runtime.load(
            {source / L"tests" / L"data" / L"missing-runtime", source / L"tests" / L"data" / L"missing-user"},
            error),
        "runtime refuses to serve with an empty dictionary");
    check(!error.empty(), "runtime exposes a diagnostic for missing dictionaries");
}

void test_settings_executable_is_resolved_beside_the_resident_host() {
    const std::filesystem::path host =
        L"C:\\Users\\tester\\AppData\\Local\\PiInput\\Dev\\versions\\0.5.0\\bin\\PiInputHost.exe";
    check(piinput::windows::settings_executable_for_host(host) ==
            host.parent_path() / L"PiInput-Settings.exe",
        "settings executable follows the side-by-side installed Host version");
}

void test_visual_settings_reload_without_reloading_the_dictionary() {
    const auto source = std::filesystem::path(PIINPUT_SOURCE_DIR);
    const auto user = source / L"build" / L"host-runtime-visual-reload-user";
    std::error_code cleanup_error;
    std::filesystem::remove_all(user, cleanup_error);
    std::filesystem::create_directories(user);
    {
        std::ofstream output(user / L"settings.ini", std::ios::binary | std::ios::trunc);
        output << "[candidates]\nfont_size=18\nwindow_height=48\n";
    }
    piinput::windows::HostRuntime runtime;
    std::string error;
    check(runtime.load({source / L"data", user}, error),
        "runtime loads for visual reload test");
    const auto loaded_path = runtime.loaded_lexicon();
    const auto entry_count = runtime.engine().entry_count();
    {
        std::ofstream output(user / L"settings.ini", std::ios::binary | std::ios::trunc);
        output << "[candidates]\nfont_size=24\nwindow_height=60\n";
    }
    runtime.poll_settings_at_composition_boundary();
    check(runtime.settings().candidates.font_size == 24U &&
            runtime.settings().candidates.window_height == 60U,
        "visual settings publish at the next composition boundary");
    check(runtime.loaded_lexicon() == loaded_path &&
            runtime.engine().entry_count() == entry_count,
        "visual reload does not reload or replace the resident dictionary");
    std::filesystem::remove_all(user, cleanup_error);
}

}  // namespace

int main() {
    test_runtime_loads_packaged_dictionary_and_real_candidates();
    test_missing_dictionary_fails_before_pipe_accepts_clients();
    test_settings_executable_is_resolved_beside_the_resident_host();
    test_visual_settings_reload_without_reloading_the_dictionary();
    std::cout << "PiInput Host runtime tests passed.\n";
    return 0;
}
