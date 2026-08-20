#pragma once

#include "piinput/windows_compat.h"

#include <string>

namespace piinput::windows::tsf {

using InstallLayoutOrTipFunction = BOOL(CALLBACK*)(LPCWSTR identifier, DWORD flags);

[[nodiscard]] std::wstring profile_tip_identifier();
[[nodiscard]] HRESULT apply_user_keyboard_registration(
    InstallLayoutOrTipFunction function,
    bool uninstall);
[[nodiscard]] HRESULT enable_user_keyboard();
[[nodiscard]] HRESULT disable_user_keyboard();

}  // namespace piinput::windows::tsf
