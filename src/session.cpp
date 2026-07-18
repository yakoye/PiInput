#include "liteime/session.h"

#include <algorithm>
#include <stdexcept>

namespace liteime {

ImeSession::ImeSession(Engine& engine, std::string schema, const std::size_t candidate_limit)
    : engine_(&engine), schema_(std::move(schema)), candidate_limit_(candidate_limit) {
    refresh();
}

void ImeSession::set_schema(std::string schema) {
    if (schema_ == schema) {
        return;
    }
    schema_ = std::move(schema);
    refresh();
}

void ImeSession::set_input(std::string input) {
    if (snapshot_.input == input) {
        return;
    }
    snapshot_.input = std::move(input);
    snapshot_.caret = snapshot_.input.size();
    refresh();
}

void ImeSession::insert(const char character) {
    snapshot_.input.insert(snapshot_.caret, 1U, character);
    ++snapshot_.caret;
    refresh();
}

bool ImeSession::backspace() {
    if (snapshot_.caret == 0U) {
        return false;
    }
    snapshot_.input.erase(snapshot_.caret - 1U, 1U);
    --snapshot_.caret;
    refresh();
    return true;
}

bool ImeSession::delete_forward() {
    if (snapshot_.caret >= snapshot_.input.size()) {
        return false;
    }
    snapshot_.input.erase(snapshot_.caret, 1U);
    refresh();
    return true;
}

bool ImeSession::move_left() {
    if (snapshot_.caret == 0U) {
        return false;
    }
    --snapshot_.caret;
    return true;
}

bool ImeSession::move_right() {
    if (snapshot_.caret >= snapshot_.input.size()) {
        return false;
    }
    ++snapshot_.caret;
    return true;
}

void ImeSession::move_home() noexcept {
    snapshot_.caret = 0U;
}

void ImeSession::move_end() noexcept {
    snapshot_.caret = snapshot_.input.size();
}

void ImeSession::clear() {
    if (snapshot_.input.empty()) {
        return;
    }
    snapshot_.input.clear();
    snapshot_.caret = 0U;
    refresh();
}

const CandidateSnapshot& ImeSession::snapshot() const noexcept {
    return snapshot_;
}

std::optional<std::string> ImeSession::choose(const std::uint64_t candidate_id) {
    const std::uint64_t requested_generation = candidate_id >> 32U;
    if (requested_generation != snapshot_.generation) {
        return std::nullopt;
    }
    const auto found = std::find_if(snapshot_.candidates.begin(), snapshot_.candidates.end(), [&](const SessionCandidate& item) {
        return item.id == candidate_id;
    });
    if (found == snapshot_.candidates.end()) {
        return std::nullopt;
    }
    const std::string result = found->candidate.word;
    engine_->record_selection(found->candidate.pinyin, found->candidate.word);
    clear();
    return result;
}

void ImeSession::refresh() {
    ++snapshot_.generation;
    snapshot_.candidates.clear();
    if (snapshot_.input.empty()) {
        return;
    }
    const auto candidates = engine_->query(snapshot_.input, schema_, candidate_limit_);
    snapshot_.candidates.reserve(candidates.size());
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        const std::uint64_t id = (snapshot_.generation << 32U) | static_cast<std::uint64_t>(index + 1U);
        snapshot_.candidates.push_back({id, candidates[index]});
    }
}

}  // namespace liteime
