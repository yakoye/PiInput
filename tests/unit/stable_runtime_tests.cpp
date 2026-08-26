#include "stable_runtime.h"
#include "piinput/windows_compat.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_layout_keeps_shim_stable_and_host_versioned() {
    const auto layout = piinput::windows::installer::make_stable_runtime_layout(
        LR"(C:\Users\test\AppData\Local\PiInput)", L"0.4.0-build-1");
    check(layout.has_value(), "safe runtime layout is accepted");
    // Everything installs into one fixed directory. A per-version path is what
    // let a registration captured by a packaged application outlive the version
    // it named, leaving a dead link no upgrade could repair.
    check(layout->shim_dll.filename() == L"PiInputTSF.dll" &&
            layout->shim_dll.parent_path().filename() == L"bin",
        "TSF Shim has a stable non-versioned path");
    check(layout->version_root == layout->shim_directory,
        "the Host installs beside the Shim, not under a versioned directory");
    check(layout->version_root.wstring().find(L"0.4.0-build-1") == std::wstring::npos,
        "no installed path carries the version");
}

void test_layout_and_marker_reject_traversal() {
    check(!piinput::windows::installer::make_stable_runtime_layout(L"C:\\PiInput", L"..\\escape"),
        "runtime layout rejects traversal");
    const auto root = std::filesystem::temp_directory_path() / L"piinput-stable-runtime-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    check(!piinput::windows::installer::write_runtime_marker_atomic(
            root / L"current.json", {L"..", 1U}),
        "runtime marker rejects parent traversal");
}

void test_marker_round_trip_and_corruption_rejection() {
    const auto root = std::filesystem::temp_directory_path() / L"piinput-stable-runtime-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto path = root / L"current.json";
    const piinput::windows::installer::RuntimeMarker expected{L"0.4.0-build-2", 1U};
    check(piinput::windows::installer::write_runtime_marker_atomic(path, expected),
        "runtime marker is atomically written");
    check(piinput::windows::installer::read_runtime_marker(path) == expected,
        "runtime marker round trips");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "{bad json}";
    }
    check(!piinput::windows::installer::read_runtime_marker(path),
        "corrupt runtime marker is rejected");
    std::filesystem::remove_all(root, error);
}

void test_stable_shim_binary_comparison_is_exact_and_fail_closed() {
    const auto root = std::filesystem::temp_directory_path() / L"piinput-stable-shim-compare-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto source = root / L"source.dll";
    const auto equal = root / L"equal.dll";
    const auto different = root / L"different.dll";
    std::ofstream(source, std::ios::binary) << "PiInput stable shim revision 2";
    std::ofstream(equal, std::ios::binary) << "PiInput stable shim revision 2";
    std::ofstream(different, std::ios::binary) << "PiInput stable shim revision 1";

    check(piinput::windows::installer::files_are_identical(source, equal),
        "byte-identical stable shims are recognized");
    check(!piinput::windows::installer::files_are_identical(source, different),
        "different stable shim bytes require a refresh");
    check(!piinput::windows::installer::files_are_identical(source, root / L"missing.dll"),
        "missing stable shim comparison fails closed");
    std::filesystem::remove_all(root, error);
}

void test_versioned_registration_is_never_reused_as_the_stable_entry() {
    const auto root = std::filesystem::temp_directory_path() / L"piinput-stable-shim-path-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / L"Runtime" / L"Shim");
    std::filesystem::create_directories(root / L"Runtime" / L"versions" / L"0.4.8" / L"bin");
    const auto packaged = root / L"package" / L"PiInputTSF.dll";
    const auto stable = root / L"Runtime" / L"Shim" / L"PiInputTSF.dll";
    const auto previous =
        root / L"Runtime" / L"versions" / L"0.4.8" / L"bin" / L"PiInputTSF.dll";
    std::filesystem::create_directories(packaged.parent_path());
    std::ofstream(packaged, std::ios::binary) << "same shim bytes";
    std::ofstream(previous, std::ios::binary) << "same shim bytes";

    check(!piinput::windows::installer::can_reuse_registered_stable_shim(
              previous, stable, packaged),
        "a byte-identical versioned DLL must not remain the permanent COM entry");
    std::ofstream(stable, std::ios::binary) << "same shim bytes";
    check(piinput::windows::installer::can_reuse_registered_stable_shim(
              stable, stable, packaged),
        "the non-versioned stable Shim can be reused when its bytes match");
    std::filesystem::remove_all(root, error);
}

void test_locked_stable_shim_never_falls_back_to_a_versioned_registration() {
    const auto root = std::filesystem::temp_directory_path() /
        L"piinput-stable-shim-lock-fallback-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto stable = root / L"Runtime" / L"Shim" / L"PiInputTSF.dll";
    std::filesystem::create_directories(stable.parent_path());
    std::ofstream(stable, std::ios::binary) << "compatible immutable shim";

    check(piinput::windows::installer::stable_shim_registration_fallback(
              stable, ERROR_SHARING_VIOLATION) == stable,
        "a loaded stable shim remains the COM registration target during upgrade");
    check(piinput::windows::installer::stable_shim_registration_fallback(
              stable, ERROR_ACCESS_DENIED) == stable,
        "access denied cannot redirect COM registration into a version directory");
    check(piinput::windows::installer::stable_shim_registration_fallback(
              stable, ERROR_INVALID_PARAMETER).empty(),
        "unexpected replacement failures are not hidden as a successful upgrade");

    std::filesystem::remove(stable, error);
    check(piinput::windows::installer::stable_shim_registration_fallback(
              stable, ERROR_SHARING_VIOLATION).empty(),
        "a missing stable shim cannot be replaced by a versioned in-process DLL");
    std::filesystem::remove_all(root, error);
}

void test_stable_shim_refresh_keeps_the_entry_path_and_replaces_its_bytes() {
    const auto root = std::filesystem::temp_directory_path() /
        L"piinput-stable-shim-refresh-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto source = root / L"package" / L"PiInputTSF.dll";
    const auto stable = root / L"Runtime" / L"Shim" / L"PiInputTSF.dll";
    std::filesystem::create_directories(source.parent_path());
    std::filesystem::create_directories(stable.parent_path());
    std::ofstream(source, std::ios::binary) << "new stable shim";
    std::ofstream(stable, std::ios::binary) << "old stable shim";

    const auto refreshed = piinput::windows::installer::refresh_stable_shim(
        source, stable, L"test-build");
    check(refreshed.path == stable && refreshed.exact_bytes &&
            piinput::windows::installer::files_are_identical(source, stable),
        "a stable-shim refresh publishes exact new bytes at the permanent entry path");
    check(refreshed.path.parent_path().filename() == L"Shim",
        "refresh never redirects COM registration into a version directory");
    std::filesystem::remove_all(root, error);
}

void test_runtime_marker_resolves_host_without_a_login_start_command() {
    const auto root = std::filesystem::temp_directory_path() / L"piinput-runtime-locator-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto layout = piinput::windows::installer::make_stable_runtime_layout(
        root, L"0.4.2-build-1");
    check(layout.has_value(), "runtime locator fixture layout is valid");
    std::filesystem::create_directories(layout->shim_directory);
    const auto host = layout->shim_directory / L"PiInputHost.exe";
    std::ofstream(host, std::ios::binary) << "host";
    // The Host lives at a fixed path, so it is found from the layout alone. A
    // version marker could disagree with what is on disk; that disagreement is
    // what left a packaged application pointing at a version long since gone.
    check(piinput::windows::installer::resolve_current_host(layout->root) == host,
        "stable shim recovers the current Host path without a version marker");
    std::filesystem::remove_all(root, error);
}

void test_machine_shim_uses_a_protected_fixed_path() {
    const std::filesystem::path program_files = LR"(C:\Program Files)";
    const auto root = piinput::windows::installer::machine_runtime_root(program_files);
    const auto shim = piinput::windows::installer::machine_shim_path(program_files);
    check(root == program_files / L"PiInput" / L"Runtime",
        "machine runtime is rooted below Program Files");
    check(shim == root / L"Shim" / L"PiInputTSF.dll",
        "machine COM uses a fixed protected Shim path");
    check(piinput::windows::installer::is_safe_machine_runtime_root(root, program_files),
        "the exact protected machine runtime is accepted for cleanup");
    check(!piinput::windows::installer::is_safe_machine_runtime_root(
              program_files, program_files),
        "Program Files itself is never accepted as a recursive cleanup target");
    check(!piinput::windows::installer::is_safe_machine_runtime_root(
              LR"(C:\Users\test\AppData\Local\PiInput)", program_files),
        "a per-user writable directory is never accepted as machine runtime");
}

}  // namespace

int main() {
    test_layout_keeps_shim_stable_and_host_versioned();
    test_layout_and_marker_reject_traversal();
    test_marker_round_trip_and_corruption_rejection();
    test_stable_shim_binary_comparison_is_exact_and_fail_closed();
    test_versioned_registration_is_never_reused_as_the_stable_entry();
    test_locked_stable_shim_never_falls_back_to_a_versioned_registration();
    test_stable_shim_refresh_keeps_the_entry_path_and_replaces_its_bytes();
    test_runtime_marker_resolves_host_without_a_login_start_command();
    test_machine_shim_uses_a_protected_fixed_path();
    std::cout << "PiInput stable runtime tests passed.\n";
    return 0;
}
