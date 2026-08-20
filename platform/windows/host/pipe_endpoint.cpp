#include "pipe_endpoint.h"

#include <windows.h>

#include <array>
#include <cwctype>

namespace piinput::windows {
namespace {

[[nodiscard]] bool valid_instance(const std::wstring_view instance) noexcept {
    if (instance.size() > 64U) return false;
    for (const wchar_t character : instance) {
        if (!(std::iswalnum(character) != 0 || character == L'_' || character == L'-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::wstring environment_instance() noexcept {
    std::array<wchar_t, 66U> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"PIINPUT_HOST_INSTANCE", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0U || length >= value.size()) return {};
    return std::wstring(value.data(), length);
}

}  // namespace

std::optional<HostEndpointNames> make_host_endpoint_names(
    const std::uint32_t session_id,
    const std::wstring_view instance) noexcept {
    if (!valid_instance(instance)) return std::nullopt;
    try {
        const std::wstring suffix = instance.empty() ? L"" : L"." + std::wstring(instance);
        const std::wstring session = std::to_wstring(session_id);
        return HostEndpointNames{
            L"Local\\PiInput.Host.v1." + session + suffix,
            L"\\\\.\\pipe\\PiInput.Host.v1." + session + suffix,
        };
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<HostEndpointNames> current_host_endpoint_names() noexcept {
    DWORD session_id = 0U;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) == FALSE) return std::nullopt;
    return make_host_endpoint_names(session_id, environment_instance());
}

}  // namespace piinput::windows
