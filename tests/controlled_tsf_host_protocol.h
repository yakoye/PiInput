#pragma once

#include <windows.h>

namespace piinput::tests::controlled_tsf {

inline constexpr wchar_t window_class_name[] = L"PiInputControlledTsfHost";
inline constexpr wchar_t window_title[] = L"PiInput Controlled TSF Test Host";

inline constexpr int edit_a_id = 1001;
inline constexpr int edit_b_id = 1002;
inline constexpr int password_id = 1003;
inline constexpr int pin_id = 1004;

inline constexpr UINT recreate_edit_b_message = WM_APP + 1U;
inline constexpr UINT clear_all_message = WM_APP + 2U;

}  // namespace piinput::tests::controlled_tsf
