#pragma once

namespace piinput {

enum class LazyLoadKeyKind {
    letter,
    symbol_trigger,
    other,
};

[[nodiscard]] bool should_initialize_chinese_engine(
    bool english_mode,
    bool shifted,
    bool disallowed_modifier,
    LazyLoadKeyKind key) noexcept;

class LazyLoadGate final {
public:
    [[nodiscard]] bool try_begin() noexcept;
    void complete(bool success) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool loading() const noexcept;

private:
    enum class State {
        unloaded,
        loading,
        loaded,
    };

    State state_{State::unloaded};
};

}  // namespace piinput
