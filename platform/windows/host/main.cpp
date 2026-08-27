#include "pipe_server.h"
#include "candidate_presenter.h"
#include "host_instance.h"
#include "host_runtime.h"
#include "session_manager.h"
#include "settings_file.h"
#include "shim_ui_control.h"
#include "user_model_persistence.h"

#include "piinput/host_protocol.h"
#include "piinput/utf.h"

#include <shellapi.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string payload_text(const piinput::HostEnvelope& envelope) {
    std::string result;
    result.reserve(envelope.payload.size());
    for (const std::byte value : envelope.payload) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return result;
}

std::filesystem::path current_executable_path() {
    std::array<wchar_t, 32768U> path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    return length == 0U || length >= path.size()
        ? std::filesystem::path{}
        : std::filesystem::path(std::wstring_view(path.data(), length));
}

void launch_settings(const std::filesystem::path& settings_path) {
    const auto executable = piinput::windows::settings_executable_for_host(
        current_executable_path());
    if (!std::filesystem::is_regular_file(executable)) return;
    const std::wstring arguments = L"--settings \"" + settings_path.wstring() + L"\"";
    (void)ShellExecuteW(
        nullptr, L"open", executable.c_str(), arguments.c_str(),
        executable.parent_path().c_str(), SW_SHOWNORMAL);
}

[[nodiscard]] std::wstring schema_label(const std::string& schema) {
    if (schema == "full") return L"全拼";
    if (schema == "natural") return L"自然码双拼";
    if (schema == "mspy") return L"微软双拼";
    if (schema == "abc") return L"智能 ABC 双拼";
    return L"小鹤双拼";
}

// Menu actions write settings rather than poking the engine: the host reloads
// settings.ini at the next composition boundary anyway, so the tray, the
// settings window and the text service all apply a change the same way.
void toggle_setting_in_file(
    const std::filesystem::path& settings_path,
    const bool schema_instead_of_language) {
    std::string error;
    auto settings = piinput::windows::load_all_settings(settings_path, error);
    if (schema_instead_of_language) {
        settings.general.schema = settings.general.schema == piinput::InputSchema::full
            ? piinput::InputSchema::flypy
            : piinput::InputSchema::full;
    } else {
        settings.general.default_language =
            settings.general.default_language == piinput::DefaultInputLanguage::english
                ? piinput::DefaultInputLanguage::chinese
                : piinput::DefaultInputLanguage::english;
    }
    (void)piinput::windows::save_all_settings_atomic(settings_path, settings, error);
}

void launch_symbol_tool(
    const std::string& configured,
    const std::filesystem::path& program_directory) {
    std::filesystem::path tool = configured.empty()
        ? program_directory / L"yesymbol.exe"
        : std::filesystem::path(piinput::utf8_to_wide(configured));
    if (std::filesystem::is_regular_file(tool)) {
        (void)ShellExecuteW(nullptr, L"open", tool.c_str(), nullptr,
            tool.parent_path().c_str(), SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(nullptr,
        L"找不到符号工具 yesymbol.exe。\n\n"
        L"它随 PiInput 一同安装，正常在程序目录下。也可以在设置窗口的"
        L"「标点符号」页指定其他路径。",
        L"PiInput 符号", MB_OK | MB_ICONINFORMATION);
}

void open_help(const std::filesystem::path& program_directory) {
    // The release package ships its guides beside the program directory.
    const auto guide = program_directory.parent_path() / L"安装与使用指南.md";
    const auto target = std::filesystem::is_regular_file(guide)
        ? guide
        : program_directory.parent_path();
    (void)ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}


}  // namespace

int main(const int argc, char** const argv) {
    const std::string_view command = argc >= 2 ? argv[1] : "--serve";
    if (command == "--build-id") {
        std::cout << PIINPUT_BUILD_ID << '\n';
        return 0;
    }
    if (command == "--version") {
        std::cout << PIINPUT_VERSION << '\n';
        return 0;
    }
    if (command == "--health" || command == "--drain") {
        const auto type = command == "--health"
            ? piinput::HostMessageType::health
            : piinput::HostMessageType::drain;
        const auto response = piinput::windows::request_host(type);
        if (!response.has_value()) return piinput::windows::host_exit_failure;
        const std::string response_text = payload_text(*response);
        std::cout << response_text << '\n';
        if (command == "--health" && argc >= 3) {
            const std::string expected = "build_id=" + std::string(argv[2]);
            const std::size_t position = response_text.find(expected);
            const bool exact_line = position != std::string::npos &&
                (position == 0U || response_text[position - 1U] == '\n') &&
                position + expected.size() == response_text.size();
            if (!exact_line) return piinput::windows::host_exit_failure;
        }
        return piinput::windows::host_exit_success;
    }
    if (command != "--serve") return piinput::windows::host_exit_failure;

    // Measured by the Host so a startup regression can be seen without timing
    // it from another process, where creating that process is most of the
    // reading whenever the machine is busy.
    const auto startup_began = std::chrono::steady_clock::now();

    // Candidate UI lives in this process. Match TSF screen coordinates exactly on
    // mixed-DPI desktops instead of letting Windows bitmap-scale the popup.
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Win the cross-process singleton before touching the large dictionary. Without
    // this early gate, several newly focused applications can each launch a Host and
    // all of them parse the same 50万-entry lexicon before the old late mutex runs.
    piinput::windows::HostInstanceLock instance_lock;
    const auto instance_state = instance_lock.acquire();
    if (instance_state == piinput::windows::HostInstanceState::failure) {
        return piinput::windows::host_exit_failure;
    }
    if (instance_state == piinput::windows::HostInstanceState::already_running) {
        std::cout << "already_running_build_id=starting-or-ready\n";
        return piinput::windows::host_exit_already_running;
    }

    const auto runtime_paths = piinput::windows::discover_host_runtime_paths(
        GetModuleHandleW(nullptr));
    piinput::windows::HostRuntime runtime;
    std::string runtime_error;
    if (!runtime.load(
            runtime_paths, runtime_error)) {
        std::cerr << "PiInputHost: " << runtime_error << '\n';
        return piinput::windows::host_exit_failure;
    }
    piinput::windows::SessionManager sessions(
        runtime.engine(), &runtime.english(), &runtime.symbols(),
        runtime.settings(), runtime.schema());
    piinput::windows::UserModelPersistence user_model_persistence(
        [&](std::string& error) { return runtime.save_user_model(error); });
    sessions.set_user_model_dirty_handler([&] { user_model_persistence.mark_dirty(); });
    piinput::windows::CandidatePresenter presenter;
    presenter.set_visual_settings({
        runtime.settings().candidates.font_size,
        runtime.settings().candidates.window_height,
        runtime.settings().candidates.show_composition,
    });
    presenter.set_toolbar_handler([&](
        const std::uint64_t client_id,
        const std::uint64_t session_id,
        const piinput::windows::CandidateToolbarAction action) {
        if (action == piinput::windows::CandidateToolbarAction::expand_candidates) {
            const auto reply = sessions.expand_candidates(client_id, session_id);
            if (reply.has_value() && !reply->snapshot.raw.empty()) {
                (void)presenter.stage(client_id, session_id, reply->snapshot);
            }
        } else if (action == piinput::windows::CandidateToolbarAction::symbols) {
            const auto reply = sessions.open_symbol_center(client_id, session_id);
            if (reply.has_value() && !reply->snapshot.raw.empty()) {
                (void)presenter.stage(client_id, session_id, reply->snapshot);
            }
        } else if (action == piinput::windows::CandidateToolbarAction::settings) {
            launch_settings(runtime_paths.user_data / L"settings.ini");
        }
    });
    // A mouse click on a candidate has to end up as text in the application,
    // and only the text service can put it there. The host resolves which
    // candidate was hit and hands the id back to the shim, which replays it
    // through the ordinary selection path.
    presenter.set_candidate_select_handler([&](
        const std::uint64_t client_id,
        const std::uint64_t session_id,
        const std::uint64_t candidate_id) {
        (void)piinput::windows::notify_shim_select_candidate(
            client_id, session_id, candidate_id);
    });
    presenter.set_candidate_context_handler([&](
        const std::uint64_t client_id,
        const std::uint64_t session_id,
        const std::uint64_t candidate_id,
        const piinput::windows::CandidateContextAction action) {
        if (action == piinput::windows::CandidateContextAction::dismiss) {
            const auto reply = sessions.cancel_composition(client_id, session_id);
            presenter.hide(client_id, session_id);
            if (reply.has_value() && reply->accepted) {
                (void)piinput::windows::notify_shim_cancel_composition(
                    client_id, session_id);
            }
            return;
        }
        piinput::CandidateManagementAction managed =
            piinput::CandidateManagementAction::delete_candidate;
        if (action == piinput::windows::CandidateContextAction::pin_first) {
            managed = piinput::CandidateManagementAction::pin_first;
        } else if (action == piinput::windows::CandidateContextAction::unpin) {
            managed = piinput::CandidateManagementAction::unpin;
        }
        const auto reply = sessions.manage_candidate(
            client_id, session_id, candidate_id, managed);
        if (!reply.has_value() || !reply->accepted) return;
        if (!reply->snapshot.raw.empty()) {
            (void)presenter.stage(client_id, session_id, reply->snapshot);
        }
    });
    if (!presenter.create(GetModuleHandleW(nullptr))) return piinput::windows::host_exit_failure;

    // No notification-area icon. Windows draws the 中/英 mark from the
    // GUID_LBI_INPUTMODE language bar item the text service registers, and the
    // product logo beside it from the language profile's IconFile. A tray icon
    // on top of those would be the third icon, which is what the earlier
    // attempt produced.

    piinput::windows::PipeServer server(
        PIINPUT_BUILD_ID, &sessions, &presenter,
        runtime.engine().lexicon_memory_mapped(),
        runtime.engine().lexicon_mapped_bytes());
    server.set_startup_duration(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startup_began).count()));
    server.set_composition_boundary_handler([&] {
        runtime.poll_settings_at_composition_boundary();
        sessions.update_settings(runtime.settings(), runtime.schema());
        presenter.set_visual_settings({
            runtime.settings().candidates.font_size,
            runtime.settings().candidates.window_height,
            runtime.settings().candidates.show_composition,
        });
    });
    return server.run();
}
