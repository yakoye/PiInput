#include "user_model_persistence.h"

#include <utility>

namespace piinput::windows {

UserModelPersistence::UserModelPersistence(
    Save save,
    const std::chrono::milliseconds delay)
    : save_(std::move(save)),
      delay_(delay),
      worker_([this] { run(); }) {}

UserModelPersistence::~UserModelPersistence() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void UserModelPersistence::mark_dirty() noexcept {
    {
        std::lock_guard lock(mutex_);
        ++dirty_revision_;
        deadline_ = std::chrono::steady_clock::now() + delay_;
    }
    changed_.notify_all();
}

bool UserModelPersistence::flush_now() noexcept {
    std::uint64_t revision = 0U;
    {
        std::lock_guard lock(mutex_);
        revision = dirty_revision_;
        if (revision == saved_revision_) return true;
    }
    return save_revision(revision);
}

std::uint64_t UserModelPersistence::saved_revision() const noexcept {
    std::lock_guard lock(mutex_);
    return saved_revision_;
}

std::string UserModelPersistence::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

bool UserModelPersistence::save_revision(const std::uint64_t revision) noexcept {
    // Manual shutdown/repair flushes and the debounce worker may race. The model
    // writer replaces one fixed destination, so only one save may run at a time.
    std::lock_guard save_lock(save_mutex_);
    std::string error;
    const bool succeeded = save_ && save_(error);
    std::lock_guard lock(mutex_);
    if (succeeded) {
        if (revision > saved_revision_) saved_revision_ = revision;
        last_error_.clear();
    } else {
        last_error_ = std::move(error);
        deadline_ = std::chrono::steady_clock::now() + delay_;
    }
    return succeeded;
}

void UserModelPersistence::run() noexcept {
    std::unique_lock lock(mutex_);
    for (;;) {
        changed_.wait(lock, [this] {
            return stopping_ || dirty_revision_ != saved_revision_;
        });
        if (stopping_) {
            const std::uint64_t revision = dirty_revision_;
            const bool needs_save = revision != saved_revision_;
            lock.unlock();
            if (needs_save) (void)save_revision(revision);
            return;
        }
        const auto expected_deadline = deadline_;
        if (changed_.wait_until(lock, expected_deadline, [this, expected_deadline] {
                return stopping_ || deadline_ != expected_deadline;
            })) {
            continue;
        }
        const std::uint64_t revision = dirty_revision_;
        lock.unlock();
        (void)save_revision(revision);
        lock.lock();
    }
}

}  // namespace piinput::windows
