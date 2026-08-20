#include "pipe_security.h"

#include <sddl.h>

#include <string>
#include <utility>

namespace piinput::windows {

std::optional<std::vector<std::byte>> current_user_sid() noexcept {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return std::nullopt;
    }
    DWORD required = 0U;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0U, &required);
    if (required == 0U) {
        CloseHandle(token);
        return std::nullopt;
    }
    std::vector<std::byte> token_buffer(required);
    if (GetTokenInformation(token, TokenUser, token_buffer.data(), required, &required) == FALSE) {
        CloseHandle(token);
        return std::nullopt;
    }
    CloseHandle(token);
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());
    const DWORD sid_size = GetLengthSid(token_user->User.Sid);
    if (sid_size == 0U) return std::nullopt;
    std::vector<std::byte> sid(sid_size);
    if (CopySid(sid_size, sid.data(), token_user->User.Sid) == FALSE) return std::nullopt;
    return sid;
}

PipeSecurity::PipeSecurity(PipeSecurity&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, nullptr)),
      attributes_(other.attributes_) {
    attributes_.lpSecurityDescriptor = descriptor_;
    other.attributes_ = {};
}

PipeSecurity& PipeSecurity::operator=(PipeSecurity&& other) noexcept {
    if (this == &other) return *this;
    if (descriptor_ != nullptr) LocalFree(descriptor_);
    descriptor_ = std::exchange(other.descriptor_, nullptr);
    attributes_ = other.attributes_;
    attributes_.lpSecurityDescriptor = descriptor_;
    other.attributes_ = {};
    return *this;
}

PipeSecurity::~PipeSecurity() {
    if (descriptor_ != nullptr) LocalFree(descriptor_);
}

std::optional<PipeSecurity> PipeSecurity::create() noexcept {
    const auto sid = current_user_sid();
    if (!sid.has_value()) return std::nullopt;
    LPWSTR sid_text = nullptr;
    if (ConvertSidToStringSidW(const_cast<std::byte*>(sid->data()), &sid_text) == FALSE) {
        return std::nullopt;
    }
    // AC is ALL APPLICATION PACKAGES. An AppContainer process carries the user
    // SID like any other, but its token is refused unless the DACL also names an
    // application-package SID, so the user ACE alone is not enough. Store-
    // sandboxed applications -- the ChatGPT/Codex desktop app is one -- could
    // not open this pipe without it, and the input method fell back to letting
    // the raw letters through. Every input method has to serve sandboxed
    // applications; this widens the pipe from "this user" to "this user, and
    // this user's sandboxed applications".
    const std::wstring sddl =
        L"D:P(A;;GA;;;SY)(A;;GA;;;AC)(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);

    PipeSecurity result;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &result.descriptor_, nullptr) == FALSE) {
        return std::nullopt;
    }
    result.attributes_.nLength = sizeof(SECURITY_ATTRIBUTES);
    result.attributes_.lpSecurityDescriptor = result.descriptor_;
    result.attributes_.bInheritHandle = FALSE;
    return result;
}

const SECURITY_ATTRIBUTES& PipeSecurity::attributes() const noexcept {
    return attributes_;
}

}  // namespace piinput::windows
