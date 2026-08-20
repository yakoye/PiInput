#pragma once

#include "composition_mirror.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace piinput::windows {

struct DeferredCompositionUpdate final {
    MirrorRequest request;
    std::string text;
    std::size_t caret{};
};

class DeferredUpdateQueue final {
public:
    [[nodiscard]] bool begin(const MirrorRequest& request) noexcept {
        if (inflight_.has_value()) return false;
        inflight_ = request;
        return true;
    }

    void defer(DeferredCompositionUpdate update) {
        deferred_ = std::move(update);
    }

    [[nodiscard]] std::optional<DeferredCompositionUpdate> complete(
        const MirrorRequest& request) {
        if (!inflight_.has_value() ||
            inflight_->client_id != request.client_id ||
            inflight_->session_id != request.session_id ||
            inflight_->sequence != request.sequence) {
            return std::nullopt;
        }
        inflight_.reset();
        return std::exchange(deferred_, std::nullopt);
    }

    void clear() noexcept {
        inflight_.reset();
        deferred_.reset();
    }

    [[nodiscard]] bool busy() const noexcept { return inflight_.has_value(); }

private:
    std::optional<MirrorRequest> inflight_;
    std::optional<DeferredCompositionUpdate> deferred_;
};

}  // namespace piinput::windows
