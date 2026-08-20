#include "user_model_persistence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

void check(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Predicate>
bool wait_until(Predicate predicate, const std::chrono::milliseconds timeout = 1s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

void test_debounce_coalesces_revisions() {
    std::atomic<int> saves{};
    piinput::windows::UserModelPersistence persistence(
        [&](std::string&) {
            ++saves;
            return true;
        },
        40ms);

    persistence.mark_dirty();
    std::this_thread::sleep_for(10ms);
    persistence.mark_dirty();
    std::this_thread::sleep_for(10ms);
    persistence.mark_dirty();

    check(wait_until([&] { return persistence.saved_revision() == 3U; }),
        "debounced persistence eventually saves the newest revision");
    check(saves.load() == 1, "rapid learning events are coalesced into one save");
}

void test_flush_now_and_failed_retry() {
    std::atomic<int> attempts{};
    piinput::windows::UserModelPersistence persistence(
        [&](std::string& error) {
            const int attempt = ++attempts;
            if (attempt == 1) {
                error = "simulated write failure";
                return false;
            }
            return true;
        },
        10s);

    persistence.mark_dirty();
    check(!persistence.flush_now(), "a failed immediate save is reported");
    check(persistence.saved_revision() == 0U,
        "a failed save leaves the dirty revision pending");
    check(persistence.last_error() == "simulated write failure",
        "a failed save keeps a diagnostic message");
    check(persistence.flush_now(), "a pending failed save can be retried");
    check(persistence.saved_revision() == 1U,
        "successful retry advances the saved revision");
    check(persistence.last_error().empty(), "successful retry clears the diagnostic");
}

void test_destructor_flushes_pending_state() {
    std::atomic<int> saves{};
    {
        piinput::windows::UserModelPersistence persistence(
            [&](std::string&) {
                ++saves;
                return true;
            },
            10s);
        persistence.mark_dirty();
    }
    check(saves.load() == 1, "shutdown flushes the last pending user-model revision");
}

void test_failed_background_save_uses_bounded_retry_delay() {
    std::atomic<int> attempts{};
    {
        piinput::windows::UserModelPersistence persistence(
            [&](std::string& error) {
                ++attempts;
                error = "still unavailable";
                return false;
            },
            40ms);
        persistence.mark_dirty();
        std::this_thread::sleep_for(115ms);
        check(attempts.load() >= 1, "a failed background save is attempted");
        check(attempts.load() <= 3,
            "persistent failures use the debounce delay instead of a busy retry loop");
    }
}

void test_worker_and_manual_flush_never_write_concurrently() {
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable release;
    bool first_entered = false;
    bool allow_first_to_finish = false;
    std::atomic<int> active{};
    std::atomic<int> maximum_active{};
    std::atomic<int> calls{};

    piinput::windows::UserModelPersistence persistence(
        [&](std::string&) {
            const int current = ++active;
            int observed = maximum_active.load();
            while (observed < current &&
                   !maximum_active.compare_exchange_weak(observed, current)) {}
            const int call = ++calls;
            if (call == 1) {
                std::unique_lock lock(mutex);
                first_entered = true;
                entered.notify_all();
                release.wait(lock, [&] { return allow_first_to_finish; });
            }
            --active;
            return true;
        },
        5ms);

    persistence.mark_dirty();
    {
        std::unique_lock lock(mutex);
        check(entered.wait_for(lock, 1s, [&] { return first_entered; }),
            "background persistence entered the save callback");
    }
    persistence.mark_dirty();
    std::atomic<bool> flush_finished{};
    std::thread flush([&] {
        (void)persistence.flush_now();
        flush_finished = true;
    });
    std::this_thread::sleep_for(30ms);
    const int observed_maximum_active = maximum_active.load();
    {
        std::lock_guard lock(mutex);
        allow_first_to_finish = true;
    }
    release.notify_all();
    flush.join();
    check(observed_maximum_active == 1,
        "background and immediate persistence never write the model concurrently");
    check(flush_finished.load(), "manual flush completes after the active writer finishes");
    check(persistence.saved_revision() == 2U,
        "a dirty revision created during a save is not lost");
}

}  // namespace

int main() {
    try {
        test_debounce_coalesces_revisions();
        test_flush_now_and_failed_retry();
        test_destructor_flushes_pending_state();
        test_failed_background_save_uses_bounded_retry_delay();
        test_worker_and_manual_flush_never_write_concurrently();
        std::cout << "PiInput user model persistence tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
