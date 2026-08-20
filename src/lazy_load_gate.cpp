#include "piinput/lazy_load_gate.h"

namespace piinput {

bool should_initialize_chinese_engine(
    const bool english_mode,
    const bool shifted,
    const bool disallowed_modifier,
    const LazyLoadKeyKind key) noexcept {
    if (english_mode || shifted || disallowed_modifier) {
        return false;
    }
    return key == LazyLoadKeyKind::letter || key == LazyLoadKeyKind::symbol_trigger;
}

bool LazyLoadGate::try_begin() noexcept {
    if (state_ != State::unloaded) {
        return false;
    }
    state_ = State::loading;
    return true;
}

void LazyLoadGate::complete(const bool success) noexcept {
    if (state_ == State::loading) {
        state_ = success ? State::loaded : State::unloaded;
    }
}

void LazyLoadGate::reset() noexcept {
    state_ = State::unloaded;
}

bool LazyLoadGate::loaded() const noexcept {
    return state_ == State::loaded;
}

bool LazyLoadGate::loading() const noexcept {
    return state_ == State::loading;
}

}  // namespace piinput
