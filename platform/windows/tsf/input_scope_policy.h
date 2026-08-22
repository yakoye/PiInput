#pragma once

#include <inputscope.h>

namespace piinput::windows {

[[nodiscard]] constexpr bool sensitive_input_scope(const InputScope scope) noexcept {
    switch (scope) {
    case IS_PASSWORD:
    case IS_PRIVATE:
    case IS_NUMERIC_PASSWORD:
    case IS_NUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN_SET:
        return true;
    default:
        return false;
    }
}

}  // namespace piinput::windows
