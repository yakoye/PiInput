#pragma once

namespace liteime {

class ShiftToggleState final {
public:
    void on_shift_down(bool modifier_already_down = false) noexcept;
    void on_other_key_down() noexcept;
    [[nodiscard]] bool on_shift_up() noexcept;
    void reset() noexcept;

private:
    bool pressed_{};
    bool used_as_modifier_{};
};

}  // namespace liteime
