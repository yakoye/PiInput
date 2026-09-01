#pragma once

#include <inputscope.h>

namespace piinput::windows {

// Scopes where an input method must not convert: the field takes a secret, and
// every one of these is a place where showing candidates or committing
// converted text would be wrong. PiInput declines the keystrokes entirely and
// lets the application have them raw.
[[nodiscard]] constexpr bool input_scope_refuses_conversion(
    const InputScope scope) noexcept {
    switch (scope) {
    case IS_PASSWORD:
    case IS_NUMERIC_PASSWORD:
    case IS_NUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN_SET:
        return true;
    default:
        return false;
    }
}

// IS_PRIVATE used to be in the list above, and that was a misreading. It marks
// text an application does not want remembered -- not a field that refuses
// input. Chrome puts it on ordinary textfields, and the tab-rename box was one:
// every letter was handed straight to the application as Latin text while the
// indicator still read 中, with no way to type Chinese there at all.
//
// Declining to learn from such a field is the right response and is not
// implemented yet: the shim sees the scope and the Host does the learning, so
// carrying it across needs a protocol field. Tracked in docs/待办事项.md.
[[nodiscard]] constexpr bool input_scope_forbids_learning(
    const InputScope scope) noexcept {
    return scope == IS_PRIVATE;
}

}  // namespace piinput::windows
