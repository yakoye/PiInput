#include "host_instance.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::wstring instance = L"host_lock_test_" + std::to_wstring(GetCurrentProcessId());
    check(SetEnvironmentVariableW(L"PIINPUT_HOST_INSTANCE", instance.c_str()) != FALSE,
        "isolated endpoint environment is available");

    piinput::windows::HostInstanceLock first;
    piinput::windows::HostInstanceLock second;
    check(first.acquire() == piinput::windows::HostInstanceState::acquired,
        "first process wins the Host singleton before initialization");
    check(second.acquire() == piinput::windows::HostInstanceState::already_running,
        "parallel cold start cannot load a second dictionary copy");
    first.release();
    check(second.acquire() == piinput::windows::HostInstanceState::acquired,
        "singleton can be reacquired after the Host exits");
    second.release();
    (void)SetEnvironmentVariableW(L"PIINPUT_HOST_INSTANCE", nullptr);
    std::cout << "PiInput Host instance tests passed.\n";
    return 0;
}
