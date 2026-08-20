#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace piinput::windows {

struct HostEndpointNames final {
    std::wstring mutex;
    std::wstring pipe;
};

[[nodiscard]] std::optional<HostEndpointNames> make_host_endpoint_names(
    std::uint32_t session_id,
    std::wstring_view instance) noexcept;

[[nodiscard]] std::optional<HostEndpointNames> current_host_endpoint_names() noexcept;

}  // namespace piinput::windows
