#include "piinput/segment_selection.h"

#include <utility>

namespace piinput {
namespace {

[[nodiscard]] std::string join_remaining(
    const ParsedComposition& composition,
    const std::size_t offset,
    const bool trailing_consumed) {
    std::string result;
    for (std::size_t index = offset; index < composition.syllables.size(); ++index) {
        if (!result.empty()) result.push_back('\'');
        result.append(composition.syllables[index]);
    }
    if (!composition.trailing_prefix.empty() && !trailing_consumed) {
        if (!result.empty()) result.push_back('\'');
        result.append(composition.trailing_prefix);
    }
    return result;
}

}  // namespace

void SegmentSelection::begin(ParsedComposition composition) {
    composition_ = std::move(composition);
    history_.clear();
    staged_text_.clear();
    syllable_offset_ = 0U;
    trailing_consumed_ = false;
    active_ = !composition_.syllables.empty() || !composition_.trailing_prefix.empty();
}

void SegmentSelection::clear() noexcept {
    composition_ = {};
    history_.clear();
    staged_text_.clear();
    syllable_offset_ = 0U;
    trailing_consumed_ = false;
    active_ = false;
}

bool SegmentSelection::stage(
    std::string word,
    std::string pinyin,
    const std::size_t consumed_syllables) {
    if (!active_ || word.empty() || pinyin.empty() || consumed_syllables == 0U) {
        return false;
    }
    // Past the last parsed syllable only the unfinished one is left. Completing
    // it always yields exactly one syllable, and it can only be taken once.
    const bool trailing = syllable_offset_ == composition_.syllables.size();
    if (trailing) {
        if (composition_.trailing_prefix.empty() || trailing_consumed_ ||
            consumed_syllables != 1U) {
            return false;
        }
        trailing_consumed_ = true;
    } else if (consumed_syllables > composition_.syllables.size() - syllable_offset_) {
        return false;
    } else {
        syllable_offset_ += consumed_syllables;
    }
    staged_text_.append(word);
    history_.push_back(
        {std::move(word), std::move(pinyin), consumed_syllables, trailing});
    return true;
}

bool SegmentSelection::undo() {
    if (!active_ || history_.empty()) return false;
    const SegmentSelectionEntry& last = history_.back();
    if (last.consumed_trailing_prefix) {
        trailing_consumed_ = false;
    } else {
        syllable_offset_ -= last.consumed_syllables;
    }
    staged_text_.resize(staged_text_.size() - last.word.size());
    history_.pop_back();
    return true;
}

bool SegmentSelection::active() const noexcept { return active_; }

bool SegmentSelection::complete() const noexcept {
    return active_ && syllable_offset_ == composition_.syllables.size() &&
        (composition_.trailing_prefix.empty() || trailing_consumed_);
}

std::size_t SegmentSelection::syllable_offset() const noexcept { return syllable_offset_; }

const ParsedComposition& SegmentSelection::composition() const noexcept { return composition_; }

const std::string& SegmentSelection::staged_text() const noexcept { return staged_text_; }

const std::vector<SegmentSelectionEntry>& SegmentSelection::history() const noexcept {
    return history_;
}

std::string SegmentSelection::remaining_pinyin() const {
    return join_remaining(composition_, syllable_offset_, trailing_consumed_);
}

std::string SegmentSelection::finish() const {
    return complete() ? staged_text_ : std::string{};
}

}  // namespace piinput
