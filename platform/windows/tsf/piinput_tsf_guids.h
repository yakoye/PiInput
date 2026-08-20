#pragma once

#include "piinput/windows_compat.h"

#include <guiddef.h>

// Permanent stable Shim identity: {13EB305F-2DA3-4CF7-8C45-16B016B801B5}
inline constexpr CLSID CLSID_PiInputTextService =
    {0x13eb305f, 0x2da3, 0x4cf7, {0x8c, 0x45, 0x16, 0xb0, 0x16, 0xb8, 0x01, 0xb5}};

// Permanent stable profile identity: {4ED27B7C-678E-4240-827A-24DA597F8D4B}
inline constexpr GUID GUID_PiInputProfile =
    {0x4ed27b7c, 0x678e, 0x4240, {0x82, 0x7a, 0x24, 0xda, 0x59, 0x7f, 0x8d, 0x4b}};

inline constexpr LANGID kPiInputLanguageId = 0x0804;  // zh-CN
