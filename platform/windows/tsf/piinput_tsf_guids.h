#pragma once

#include "piinput/windows_compat.h"

#include <guiddef.h>

// {D73AABA7-BE3E-4E53-8DE2-652D352743F3}
inline constexpr CLSID CLSID_PiInputTextService =
    {0xd73aaba7, 0xbe3e, 0x4e53, {0x8d, 0xe2, 0x65, 0x2d, 0x35, 0x27, 0x43, 0xf3}};

// {13D6EB0B-023B-4AA8-ADE1-2A360820EC49}
inline constexpr GUID GUID_PiInputProfile =
    {0x13d6eb0b, 0x023b, 0x4aa8, {0xad, 0xe1, 0x2a, 0x36, 0x08, 0x20, 0xec, 0x49}};

inline constexpr LANGID kPiInputLanguageId = 0x0804;  // zh-CN
