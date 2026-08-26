#include "pipe_endpoint.h"

#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const auto production = piinput::windows::make_host_endpoint_names(7U, L"");
    check(production.has_value(), "production endpoint names are valid");
    check(production->mutex == L"Local\\PiInput.Host.v1.7",
        "empty instance keeps the installed Host mutex name");
    check(production->pipe == L"\\\\.\\pipe\\PiInput.Host.v1.7",
        "empty instance keeps the installed Host pipe name");

    const auto isolated = piinput::windows::make_host_endpoint_names(7U, L"test_123-ABC");
    check(isolated.has_value(), "safe test instance is accepted");
    check(isolated->mutex == L"Local\\PiInput.Host.v1.7.test_123-ABC",
        "test instance isolates the mutex");
    check(isolated->pipe == L"\\\\.\\pipe\\PiInput.Host.v1.7.test_123-ABC",
        "test instance isolates the pipe");

    check(!piinput::windows::make_host_endpoint_names(7U, L"bad.name").has_value(),
        "dot cannot escape the instance-name segment");
    check(!piinput::windows::make_host_endpoint_names(7U, std::wstring(65U, L'a')).has_value(),
        "instance names are bounded");

    std::cout << "PiInput Host endpoint tests passed.\n";
    return 0;
}
