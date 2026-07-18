#pragma once

#include "liteime/windows_compat.h"

#include <guiddef.h>

// {84E21A77-3A42-4D7B-93B8-BCDF818FC414}
inline constexpr CLSID CLSID_LiteImeTextService =
    {0x84e21a77, 0x3a42, 0x4d7b, {0x93, 0xb8, 0xbc, 0xdf, 0x81, 0x8f, 0xc4, 0x14}};

// {A99F4C36-EA4E-4457-AE7A-861804AC7439}
inline constexpr GUID GUID_LiteImeProfile =
    {0xa99f4c36, 0xea4e, 0x4457, {0xae, 0x7a, 0x86, 0x18, 0x04, 0xac, 0x74, 0x39}};

inline constexpr LANGID kLiteImeLanguageId = 0x0804;  // zh-CN
