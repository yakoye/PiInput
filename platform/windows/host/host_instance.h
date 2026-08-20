#pragma once

#include <windows.h>

namespace piinput::windows {

enum class HostInstanceState {
    acquired,
    already_running,
    failure,
};

class HostInstanceLock final {
public:
    HostInstanceLock() = default;
    ~HostInstanceLock();

    HostInstanceLock(const HostInstanceLock&) = delete;
    HostInstanceLock& operator=(const HostInstanceLock&) = delete;

    [[nodiscard]] HostInstanceState acquire() noexcept;
    void release() noexcept;

private:
    HANDLE mutex_{};
    bool owns_mutex_{};
};

}  // namespace piinput::windows
