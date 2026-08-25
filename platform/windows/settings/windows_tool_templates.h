#pragma once

#include <span>

namespace piinput::windows {

struct WindowsToolTemplate final {
    const wchar_t* category;
    const wchar_t* name;
    // PiInput shortcut target. shell:program|arguments keeps executable and
    // arguments separate when the entry is launched through ShellExecute.
    const wchar_t* target;
};

[[nodiscard]] std::span<const WindowsToolTemplate> windows_tool_templates() noexcept;

}  // namespace piinput::windows
