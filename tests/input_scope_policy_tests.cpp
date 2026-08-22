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
    using piinput::windows::sensitive_input_scope;

    check(sensitive_input_scope(IS_PASSWORD), "password fields are sensitive");
    check(sensitive_input_scope(IS_PRIVATE), "private fields are sensitive");
    check(sensitive_input_scope(IS_NUMERIC_PASSWORD), "numeric passwords are sensitive");
    check(sensitive_input_scope(IS_NUMERIC_PIN), "numeric PINs are sensitive");
    check(sensitive_input_scope(IS_ALPHANUMERIC_PIN), "alphanumeric PINs are sensitive");
    check(sensitive_input_scope(IS_ALPHANUMERIC_PIN_SET), "PIN setup fields are sensitive");

    check(!sensitive_input_scope(IS_DEFAULT), "ordinary text remains enabled");
    check(!sensitive_input_scope(IS_TEXT), "text input remains enabled");
    check(!sensitive_input_scope(IS_CHAT), "chat input remains enabled");
    check(!sensitive_input_scope(IS_LOGINNAME), "login names are not secret text");
    check(!sensitive_input_scope(IS_EMAIL_SMTPEMAILADDRESS), "email input remains enabled");

    if (failures != 0) return 1;
    std::cout << "PiInput sensitive input-scope policy tests passed.\n";
    return 0;
}
