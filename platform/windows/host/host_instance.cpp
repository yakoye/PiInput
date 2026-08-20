#include "host_instance.h"

#include "pipe_endpoint.h"

namespace piinput::windows {

HostInstanceLock::~HostInstanceLock() {
    release();
}

HostInstanceState HostInstanceLock::acquire() noexcept {
    release();
    const auto endpoint = current_host_endpoint_names();
    if (!endpoint.has_value()) return HostInstanceState::failure;
    mutex_ = CreateMutexW(nullptr, TRUE, endpoint->mutex.c_str());
    if (mutex_ == nullptr) return HostInstanceState::failure;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return HostInstanceState::already_running;
    }
    owns_mutex_ = true;
    return HostInstanceState::acquired;
}

void HostInstanceLock::release() noexcept {
    if (mutex_ == nullptr) return;
    if (owns_mutex_) (void)ReleaseMutex(mutex_);
    CloseHandle(mutex_);
    mutex_ = nullptr;
    owns_mutex_ = false;
}

}  // namespace piinput::windows
