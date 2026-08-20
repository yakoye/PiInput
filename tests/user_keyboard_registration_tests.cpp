#include "user_keyboard_registration.h"

#include <iostream>
#include <string>

namespace {

std::wstring captured_identifier;
DWORD captured_flags = 0U;
BOOL callback_result = TRUE;

BOOL CALLBACK capture_install_request(const LPCWSTR identifier, const DWORD flags) {
    captured_identifier = identifier == nullptr ? L"" : identifier;
    captured_flags = flags;
    return callback_result;
}

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace piinput::windows::tsf;

    // Product renames must preserve the TSF identity already admitted by Windows.
    // The visible product name, paths, and binaries remain PiInput.
    constexpr wchar_t expected_identifier[] =
        L"0x0804:{13EB305F-2DA3-4CF7-8C45-16B016B801B5}"
        L"{4ED27B7C-678E-4240-827A-24DA597F8D4B};";

    bool ok = true;
    ok &= expect(profile_tip_identifier() == expected_identifier,
        "PiInput must use the exact Windows text-service profile identifier");

    captured_identifier.clear();
    captured_flags = 99U;
    callback_result = TRUE;
    ok &= expect(apply_user_keyboard_registration(capture_install_request, false) == S_OK,
        "enabling the current-user keyboard entry must succeed when Windows accepts it");
    ok &= expect(captured_identifier == expected_identifier,
        "enabling must pass the PiInput TIP identifier to Windows");
    ok &= expect(captured_flags == 0U,
        "enabling must not pass an uninstall flag");

    captured_identifier.clear();
    captured_flags = 99U;
    ok &= expect(apply_user_keyboard_registration(capture_install_request, true) == S_OK,
        "disabling the current-user keyboard entry must succeed when Windows accepts it");
    ok &= expect(captured_identifier == expected_identifier,
        "disabling must pass the PiInput TIP identifier to Windows");
    ok &= expect(captured_flags == 0x00000001U,
        "disabling must use the documented ILOT_UNINSTALL flag");

    callback_result = FALSE;
    ok &= expect(FAILED(apply_user_keyboard_registration(capture_install_request, false)),
        "a Windows user-list update failure must be returned to the installer");
    ok &= expect(apply_user_keyboard_registration(nullptr, false) == E_POINTER,
        "a missing Windows API entry point must fail closed");

    if (!ok) {
        return 1;
    }
    std::cout << "PiInput current-user keyboard registration tests passed.\n";
    return 0;
}
