#pragma once

#include "shim_connection_policy.h"

#include "piinput/host_protocol.h"
#include "piinput/windows_compat.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace piinput::windows {

class ShimPipeTransport final {
public:
    explicit ShimPipeTransport(HINSTANCE module);

    [[nodiscard]] std::optional<HostEnvelope> request(
        const HostEnvelope& envelope) const noexcept;

    // Starts the Host if it is not already up, without waiting for it.
    //
    // Called when this input method becomes active, which is seconds before the
    // first keystroke. Left to the ordinary request path the Host is only
    // launched by that first key, which then has 750 ms to be answered -- not
    // enough right after a sign-in, when the dictionary is still being read off
    // a cold disk. Those keys fall through as Latin letters, which reads as the
    // input method not working at all for the first few seconds.
    void warm_up() const noexcept;

private:
    [[nodiscard]] bool start_host() const noexcept;
    [[nodiscard]] std::filesystem::path resolve_host_path() const noexcept;

    std::filesystem::path module_directory_;
    mutable ShimConnectionPolicy connection_policy_;
    // Allocating and zero-filling the 1 MiB protocol ceiling for every reply
    // cost more than the keystroke it carried. One owned buffer is reused and
    // only grows when a reply genuinely needs more room.
    mutable std::vector<std::byte> reply_buffer_;
};

}  // namespace piinput::windows
