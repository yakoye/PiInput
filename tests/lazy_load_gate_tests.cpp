#include "piinput/lazy_load_gate.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_activation_and_focus_do_not_request_loading() {
    piinput::LazyLoadGate gate;
    check(!gate.loaded(), "starts unloaded");
    check(!gate.loading(), "starts outside a load operation");
}

void test_only_relevant_chinese_input_requests_the_large_engine() {
    using piinput::LazyLoadKeyKind;
    check(piinput::should_initialize_chinese_engine(
              false, false, false, LazyLoadKeyKind::letter),
        "a Chinese-mode letter initializes the engine");
    check(piinput::should_initialize_chinese_engine(
              false, false, false, LazyLoadKeyKind::symbol_trigger),
        "an explicit symbol-center trigger may initialize packaged symbols");
    check(!piinput::should_initialize_chinese_engine(
              true, false, false, LazyLoadKeyKind::letter),
        "English direct input never expands the Chinese dictionary");
    check(!piinput::should_initialize_chinese_engine(
              false, true, false, LazyLoadKeyKind::letter),
        "shifted letters remain direct input");
    check(!piinput::should_initialize_chinese_engine(
              false, false, true, LazyLoadKeyKind::letter),
        "shortcut modifiers never initialize the engine");
    check(!piinput::should_initialize_chinese_engine(
              false, false, false, LazyLoadKeyKind::other),
        "ordinary punctuation stays on the lightweight path");
}

void test_reentrant_initialization_is_rejected() {
    piinput::LazyLoadGate gate;
    check(gate.try_begin(), "first input begins lazy initialization");
    check(gate.loading(), "gate reports an in-progress initialization");
    check(!gate.try_begin(), "reentrant input cannot recursively initialize the engine");

    gate.complete(true);
    check(gate.loaded(), "successful initialization remains loaded");
    check(!gate.try_begin(), "loaded engine is never initialized twice");
}

void test_failed_initialization_can_retry_on_a_later_key() {
    piinput::LazyLoadGate gate;
    check(gate.try_begin(), "first attempt begins");
    gate.complete(false);
    check(!gate.loaded() && !gate.loading(), "failed attempt returns to unloaded state");
    check(gate.try_begin(), "a later input key may retry after failure");
}

void test_reset_returns_to_unloaded_without_leaking_loading_state() {
    piinput::LazyLoadGate gate;
    check(gate.try_begin(), "initialization begins before reset");
    gate.reset();
    check(!gate.loaded() && !gate.loading(), "deactivation resets the lazy load state");
    check(gate.try_begin(), "reactivation may initialize once again");
}

}  // namespace

int main() {
    test_activation_and_focus_do_not_request_loading();
    test_only_relevant_chinese_input_requests_the_large_engine();
    test_reentrant_initialization_is_rejected();
    test_failed_initialization_can_retry_on_a_later_key();
    test_reset_returns_to_unloaded_without_leaking_loading_state();
    std::cout << "All LazyLoadGate tests passed\n";
    return 0;
}
