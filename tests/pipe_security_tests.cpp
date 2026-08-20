#include "pipe_security.h"

#include <windows.h>
#include <sddl.h>

#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << " (win32=" << GetLastError() << ")\n";
        std::exit(1);
    }
}

void test_pipe_acl_grants_this_user_local_system_and_sandboxed_apps() {
    auto security = piinput::windows::PipeSecurity::create();
    check(security.has_value(), "pipe security descriptor is created");

    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    check(GetSecurityDescriptorDacl(
        security->attributes().lpSecurityDescriptor, &present, &dacl, &defaulted) != FALSE,
        "pipe DACL can be inspected");
    check(present != FALSE && dacl != nullptr, "pipe has an explicit DACL");
    // Three, not two: an AppContainer process carries this user's SID but its
    // token is refused unless the DACL also names an application-package SID.
    // Without the third entry the Store-sandboxed applications cannot reach the
    // Host at all and the input method silently does nothing in them.
    check(dacl->AceCount == 3U, "pipe DACL contains exactly three allow entries");

    const auto current_user = piinput::windows::current_user_sid();
    check(current_user.has_value(), "current user SID is available");
    BYTE system_sid_buffer[SECURITY_MAX_SID_SIZE]{};
    DWORD system_sid_size = sizeof(system_sid_buffer);
    check(CreateWellKnownSid(
        WinLocalSystemSid, nullptr, system_sid_buffer, &system_sid_size) != FALSE,
        "LocalSystem SID is available");
    PSID app_packages = nullptr;
    check(ConvertStringSidToSidW(L"S-1-15-2-1", &app_packages) != FALSE &&
        app_packages != nullptr, "ALL APPLICATION PACKAGES SID is available");

    bool found_user = false;
    bool found_system = false;
    bool found_app_packages = false;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw_ace = nullptr;
        check(GetAce(dacl, index, &raw_ace) != FALSE, "pipe ACE can be inspected");
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        check(header->AceType == ACCESS_ALLOWED_ACE_TYPE, "pipe ACL uses allow ACEs only");
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        PSID sid = const_cast<DWORD*>(&ace->SidStart);
        found_user = found_user ||
            EqualSid(sid, const_cast<std::byte*>(current_user->data())) != FALSE;
        found_system = found_system || EqualSid(sid, system_sid_buffer) != FALSE;
        found_app_packages = found_app_packages || EqualSid(sid, app_packages) != FALSE;
    }
    LocalFree(app_packages);
    check(found_user, "pipe grants the current user access");
    check(found_system, "pipe grants LocalSystem access");
    check(found_app_packages,
        "pipe grants ALL APPLICATION PACKAGES so sandboxed applications can connect");
}

}  // namespace

int main() {
    test_pipe_acl_grants_this_user_local_system_and_sandboxed_apps();
    std::cout << "PiInput pipe security tests passed.\n";
    return 0;
}
