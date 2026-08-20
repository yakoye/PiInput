#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace piinput::windows {

[[nodiscard]] std::optional<std::vector<std::byte>> current_user_sid() noexcept;

class PipeSecurity final {
public:
    PipeSecurity() = default;
    PipeSecurity(const PipeSecurity&) = delete;
    PipeSecurity& operator=(const PipeSecurity&) = delete;
    PipeSecurity(PipeSecurity&& other) noexcept;
    PipeSecurity& operator=(PipeSecurity&& other) noexcept;
    ~PipeSecurity();

    [[nodiscard]] static std::optional<PipeSecurity> create() noexcept;
    [[nodiscard]] const SECURITY_ATTRIBUTES& attributes() const noexcept;

private:
    PSECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
};

}  // namespace piinput::windows
