#include "piinput/english_session.h"

namespace piinput {

EnglishSession::EnglishSession(
    EnglishLexicon& lexicon,
    const std::size_t candidate_limit,
    const bool learning_enabled)
    : lexicon_(&lexicon),
      candidate_limit_(candidate_limit),
      learning_enabled_(learning_enabled) {}

bool EnglishSession::should_start(
    const bool english_mode,
    const EnglishSettings& settings) noexcept {
    return english_mode && settings.enabled;
}

bool EnglishSession::insert(const char character) {
    const bool ascii_letter =
        (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z');
    if (!ascii_letter) {
        return false;
    }
    snapshot_.input.insert(snapshot_.caret, 1U, character);
    ++snapshot_.caret;
    refresh();
    return true;
}

bool EnglishSession::backspace() {
    if (snapshot_.caret == 0U) {
        return false;
    }
    snapshot_.input.erase(snapshot_.caret - 1U, 1U);
    --snapshot_.caret;
    refresh();
    return true;
}

bool EnglishSession::delete_forward() {
    if (snapshot_.caret >= snapshot_.input.size()) {
        return false;
    }
    snapshot_.input.erase(snapshot_.caret, 1U);
    refresh();
    return true;
}

bool EnglishSession::move_left() {
    if (snapshot_.caret == 0U) {
        return false;
    }
    --snapshot_.caret;
    return true;
}

bool EnglishSession::move_right() {
    if (snapshot_.caret >= snapshot_.input.size()) {
        return false;
    }
    ++snapshot_.caret;
    return true;
}

bool EnglishSession::move_home() noexcept {
    const bool changed = snapshot_.caret != 0U;
    snapshot_.caret = 0U;
    return changed;
}

bool EnglishSession::move_end() noexcept {
    const bool changed = snapshot_.caret != snapshot_.input.size();
    snapshot_.caret = snapshot_.input.size();
    return changed;
}

void EnglishSession::clear() {
    snapshot_.input.clear();
    snapshot_.caret = 0U;
    snapshot_.candidates.clear();
}

const EnglishSessionSnapshot& EnglishSession::snapshot() const noexcept {
    return snapshot_;
}

const std::string& EnglishSession::raw_input() const noexcept {
    return snapshot_.input;
}

std::optional<std::string> EnglishSession::choose(const std::size_t index) {
    if (index >= snapshot_.candidates.size()) {
        return std::nullopt;
    }
    const std::string result = snapshot_.candidates[index].word;
    if (learning_enabled_) {
        const bool recorded = lexicon_->record_selection(result);
        (void)recorded;
    }
    clear();
    return result;
}

void EnglishSession::refresh() {
    snapshot_.candidates = snapshot_.input.empty()
        ? std::vector<EnglishCandidate>{}
        : lexicon_->query(snapshot_.input, candidate_limit_);
}

}  // namespace piinput
