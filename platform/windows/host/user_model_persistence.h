#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace piinput::windows {

class UserModelPersistence final {
public:
    using Save = std::function<bool(std::string&)>;

    explicit UserModelPersistence(
        Save save,
        std::chrono::milliseconds delay = std::chrono::milliseconds{1500});
    UserModelPersistence(const UserModelPersistence&) = delete;
    UserModelPersistence& operator=(const UserModelPersistence&) = delete;
    ~UserModelPersistence();

    void mark_dirty() noexcept;
    [[nodiscard]] bool flush_now() noexcept;
    [[nodiscard]] std::uint64_t saved_revision() const noexcept;
    [[nodiscard]] std::string last_error() const;

private:
    void run() noexcept;
    [[nodiscard]] bool save_revision(std::uint64_t revision) noexcept;

    Save save_;
    std::chrono::milliseconds delay_;
    std::mutex save_mutex_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::uint64_t dirty_revision_{};
    std::uint64_t saved_revision_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::string last_error_;
    bool stopping_{};
};

}  // namespace piinput::windows
