#include "input_scope_policy.h"

#include <iostream>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

}  // namespace

int main() {
    using piinput::windows::input_scope_forbids_learning;
    using piinput::windows::input_scope_refuses_conversion;

    // Secret entry. Converting here would show candidates for the secret and
    // commit converted text into the field, so the keystrokes are declined and
    // the application gets them raw.
    check(input_scope_refuses_conversion(IS_PASSWORD), "password fields refuse conversion");
    check(input_scope_refuses_conversion(IS_NUMERIC_PASSWORD),
        "numeric passwords refuse conversion");
    check(input_scope_refuses_conversion(IS_NUMERIC_PIN), "numeric PINs refuse conversion");
    check(input_scope_refuses_conversion(IS_ALPHANUMERIC_PIN),
        "alphanumeric PINs refuse conversion");
    check(input_scope_refuses_conversion(IS_ALPHANUMERIC_PIN_SET),
        "PIN setup fields refuse conversion");

    // IS_PRIVATE is not one of them, and reading it as one is what made
    // Chrome's tab-rename box impossible to type Chinese into: every letter was
    // handed to the application as Latin text while the indicator read 中. It
    // marks text that must not be remembered, not a field that refuses input.
    check(!input_scope_refuses_conversion(IS_PRIVATE),
        "private fields still accept input");
    check(input_scope_forbids_learning(IS_PRIVATE),
        "private fields are still recognised as not to be remembered");
    check(!input_scope_forbids_learning(IS_PASSWORD),
        "the two questions are asked separately");

    check(!input_scope_refuses_conversion(IS_DEFAULT), "ordinary text remains enabled");
    check(!input_scope_refuses_conversion(IS_TEXT), "text input remains enabled");
    check(!input_scope_refuses_conversion(IS_CHAT), "chat input remains enabled");
    check(!input_scope_refuses_conversion(IS_LOGINNAME), "login names are not secret text");
    check(!input_scope_refuses_conversion(IS_EMAIL_SMTPEMAILADDRESS),
        "email input remains enabled");

    if (failures != 0) return 1;
    std::cout << "PiInput input-scope policy tests passed.\n";
    return 0;
}
