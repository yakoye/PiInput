#include "shim_pipe_transport.h"
#include "stable_runtime.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace piinput::windows {

namespace {

[[nodiscard]] std::wstring pipe_name() noexcept {
    DWORD session_id = 0U;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) == FALSE) return {};
    return L"\\\\.\\pipe\\PiInput.Host.v1." + std::to_wstring(session_id);
}

[[nodiscard]] std::filesystem::path module_directory(const HINSTANCE module) {
    std::array<wchar_t, 32768U> buffer{};
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring_view(buffer.data(), length)).parent_path();
}

constexpr std::size_t initial_reply_bytes = 16U * 1024U;

struct ExchangeResult final {
    bool delivered{};
    std::optional<HostEnvelope> reply;
};

[[nodiscard]] ExchangeResult exchange(
    const std::wstring& name,
    const HostEnvelope& envelope,
    std::vector<std::byte>& response) noexcept {
    const HANDLE pipe = CreateFileW(
        name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING, 0U, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return {};

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) == FALSE) {
        CloseHandle(pipe);
        return {};
    }

    try {
        const auto encoded = encode_host_envelope(envelope);
        DWORD written = 0U;
        if (WriteFile(pipe, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr) == FALSE ||
            written != encoded.size()) {
            CloseHandle(pipe);
            return {};
        }
        constexpr std::size_t ceiling = host_header_bytes + host_max_payload_bytes;
        if (response.empty()) response.resize(initial_reply_bytes);
        std::size_t offset = 0U;
        for (;;) {
            DWORD read = 0U;
            const BOOL complete = ReadFile(
                pipe, response.data() + offset,
                static_cast<DWORD>(response.size() - offset), &read, nullptr);
            offset += read;
            if (complete != FALSE) break;
            if (GetLastError() != ERROR_MORE_DATA || response.size() >= ceiling) {
                CloseHandle(pipe);
                return {.delivered = true};
            }
            response.resize((std::min)(ceiling, response.size() * 2U));
        }
        CloseHandle(pipe);
        ProtocolError error = ProtocolError::none;
        return {
            .delivered = true,
            .reply = decode_host_envelope(
                std::span<const std::byte>(response.data(), offset), error),
        };
    } catch (...) {
        CloseHandle(pipe);
        return {};
    }
}

}  // namespace

ShimPipeTransport::ShimPipeTransport(const HINSTANCE module)
    : module_directory_(module_directory(module)) {}

std::optional<HostEnvelope> ShimPipeTransport::request(
    const HostEnvelope& envelope) const noexcept {
    const std::wstring name = pipe_name();
    if (name.empty()) return std::nullopt;

    auto result = exchange(name, envelope, reply_buffer_);
    if (result.reply.has_value()) {
        connection_policy_.record_success();
        return result.reply;
    }
    // commit_result is a one-way delivery. The Host intentionally stays silent
    // when the committed generation had nothing to learn. A successful write
    // must therefore complete the notification instead of entering the 750 ms
    // cold-start retry loop and holding every later key behind it.
    if (envelope.type == HostMessageType::commit_result && result.delivered) {
        connection_policy_.record_success();
        return std::nullopt;
    }

    const auto attempt = connection_policy_.plan_after_exchange_failure(GetTickCount64());
    if (!attempt.launch_host) return std::nullopt;
    if (!start_host()) return std::nullopt;

    const ULONGLONG deadline = GetTickCount64() + attempt.wait_budget_ms;
    do {
        result = exchange(name, envelope, reply_buffer_);
        if (result.reply.has_value()) {
            connection_policy_.record_success();
            return result.reply;
        }
        if (envelope.type == HostMessageType::commit_result && result.delivered) {
            connection_policy_.record_success();
            return std::nullopt;
        }
        if (GetTickCount64() >= deadline) return std::nullopt;
        (void)WaitNamedPipeW(name.c_str(), 25U);
        Sleep(5U);
    } while (GetTickCount64() < deadline);
    return std::nullopt;
}

void ShimPipeTransport::warm_up() const noexcept {
    // A Host already serving answers this immediately; the point is only to
    // find out whether one exists. Nothing is sent and nothing is waited for.
    const auto name = pipe_name();
    if (WaitNamedPipeW(name.c_str(), 1U) != FALSE) return;
    (void)start_host();
}

bool ShimPipeTransport::start_host() const noexcept {
    const auto host_path = resolve_host_path();
    std::error_code error;
    if (host_path.empty() || !std::filesystem::is_regular_file(host_path, error) || error) return false;
    std::wstring command = L"\"" + host_path.wstring() + L"\" --serve";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        host_path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, host_path.parent_path().c_str(),
        &startup, &process);
    if (created == FALSE) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::filesystem::path ShimPipeTransport::resolve_program_path(
    const std::wstring_view name) const noexcept {
    return program_beside_host(resolve_host_path(), name);
}

std::filesystem::path ShimPipeTransport::resolve_host_path() const noexcept {
    constexpr wchar_t key[] = L"Software\\PiInput\\Runtime";
    constexpr wchar_t value_name[] = L"CurrentHostPath";
    DWORD type = 0U;
    DWORD bytes = 0U;
    if (RegGetValueW(HKEY_CURRENT_USER, key, value_name, RRF_RT_REG_SZ,
            &type, nullptr, &bytes) == ERROR_SUCCESS && bytes >= sizeof(wchar_t)) {
        try {
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_CURRENT_USER, key, value_name, RRF_RT_REG_SZ,
                    &type, value.data(), &bytes) == ERROR_SUCCESS) {
                while (!value.empty() && value.back() == L'\0') value.pop_back();
                if (!value.empty()) {
                    const std::filesystem::path registered(value);
                    std::error_code error;
                    if (std::filesystem::is_regular_file(registered, error) && !error) {
                        return registered;
                    }
                }
            }
        } catch (...) {
            return {};
        }
    }
    if (const auto recovered = piinput::windows::installer::resolve_current_host(
            module_directory_.parent_path()); !recovered.empty()) {
        return recovered;
    }
    return module_directory_ / L"PiInputHost.exe";
}

}  // namespace piinput::windows
