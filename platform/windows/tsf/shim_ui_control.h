#pragma once

#include <windows.h>

#include <cstdint>

namespace piinput::windows {

inline constexpr wchar_t stable_shim_callback_window_class[] =
    L"PiInput.StableShim.Callback.v1";
inline constexpr UINT host_reply_window_message = WM_APP + 0x351U;
inline constexpr UINT host_cancel_composition_message = WM_APP + 0x352U;
inline constexpr UINT host_replay_update_message = WM_APP + 0x353U;
// Mouse click on a candidate. The host knows which candidate was hit but not
// how to commit it -- only the text service can put text into the application
// -- so it hands the id back and the shim replays it as an ordinary selection.
inline constexpr UINT host_select_candidate_message = WM_APP + 0x354U;
// The protocol client id includes the process creation time so a recycled PID
// cannot inherit an old Host session. It therefore cannot be matched against
// GetWindowThreadProcessId. Ask each callback window to identify itself using
// that same 64-bit value before posting a Host UI action.
inline constexpr UINT host_query_client_identity_message = WM_APP + 0x355U;

inline bool post_to_shim(
    const std::uint64_t client_id,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) noexcept {
    HWND window = nullptr;
    bool posted = false;
    while ((window = FindWindowExW(
                HWND_MESSAGE, window, stable_shim_callback_window_class, nullptr)) != nullptr) {
        DWORD_PTR matches = 0U;
        const auto low = static_cast<WPARAM>(client_id & 0xFFFFFFFFULL);
        const auto high = static_cast<LPARAM>(client_id >> 32U);
        const bool identified = SendMessageTimeoutW(
            window, host_query_client_identity_message, low, high,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 50U, &matches) != 0U && matches == 1U;
        if (!identified) {
            // Compatibility with a callback window from a pre-identity Shim.
            DWORD process_id = 0U;
            (void)GetWindowThreadProcessId(window, &process_id);
            if (static_cast<std::uint64_t>(process_id) != client_id) continue;
        }
        if (PostMessageW(window, message, wparam, lparam) != FALSE) {
            posted = true;
        }
    }
    return posted;
}

inline bool notify_shim_cancel_composition(
    const std::uint64_t client_id,
    const std::uint64_t session_id) noexcept {
    return post_to_shim(client_id, host_cancel_composition_message, 0U,
        static_cast<LPARAM>(session_id));
}

inline bool notify_shim_select_candidate(
    const std::uint64_t client_id,
    const std::uint64_t session_id,
    const std::uint64_t candidate_id) noexcept {
    (void)session_id;
    return post_to_shim(client_id, host_select_candidate_message, 0U,
        static_cast<LPARAM>(candidate_id));
}

}  // namespace piinput::windows
