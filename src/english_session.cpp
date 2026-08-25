#include "piinput/english_session.h"

#include <algorithm>
#include <limits>

namespace piinput {
namespace {

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        return a == b;
    });
}

}  // namespace

EnglishSession::EnglishSession(
    EnglishLexicon& lexicon,
    const std::size_t candidate_limit,
    const bool learning_enabled,
    std::vector<CustomShortcutSettings> shortcuts)
    : lexicon_(&lexicon),
      candidate_limit_(candidate_limit),
      learning_enabled_(learning_enabled),
      shortcuts_(std::move(shortcuts)) {}

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

void EnglishSession::set_candidate_limit(const std::size_t candidate_limit) {
    if (candidate_limit_ == candidate_limit) {
        return;
    }
    candidate_limit_ = candidate_limit;
    refresh();
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

std::optional<std::string> EnglishSession::candidate(const std::size_t index) const {
    if (index >= snapshot_.candidates.size()) {
        return std::nullopt;
    }
    return snapshot_.candidates[index].word;
}

std::optional<std::string> EnglishSession::action_target(const std::size_t index) const {
    if (index >= snapshot_.candidates.size() ||
        snapshot_.candidates[index].action_target.empty()) {
        return std::nullopt;
    }
    return snapshot_.candidates[index].action_target;
}

std::optional<std::string> EnglishSession::choose(const std::size_t index) {
    if (index >= snapshot_.candidates.size()) {
        return std::nullopt;
    }
    const std::string result = snapshot_.candidates[index].word;
    if (learning_enabled_ && snapshot_.candidates[index].action_target.empty()) {
        const bool recorded = lexicon_->record_selection(result);
        (void)recorded;
    }
    clear();
    return result;
}

void EnglishSession::restore(EnglishSessionSnapshot snapshot) {
    snapshot_ = std::move(snapshot);
}

void EnglishSession::refresh() {
    if (snapshot_.input.empty() || candidate_limit_ == 0U) {
        snapshot_.candidates.clear();
        return;
    }

    auto candidates = lexicon_->query(snapshot_.input, candidate_limit_);
    EnglishCandidate typed{
        0U,
        snapshot_.input,
        (std::numeric_limits<std::uint64_t>::max)(),
        0U,
        false,
        static_cast<std::uint32_t>(EnglishCandidateFlag::typed),
        {},
    };
    const auto exact = std::find_if(candidates.begin(), candidates.end(), [&](const auto& item) {
        return ascii_case_equal(item.word, snapshot_.input);
    });
    if (exact != candidates.end()) {
        typed.id = exact->id;
        typed.learning_count = exact->learning_count;
        typed.user_entry = exact->user_entry;
        typed.flags |= exact->flags;
        candidates.erase(exact);
    }
    candidates.insert(candidates.begin(), std::move(typed));
    struct MatchedShortcut final {
        std::size_t position{};
        std::size_t order{};
        EnglishCandidate candidate;
    };
    std::vector<MatchedShortcut> matched;
    for (std::size_t index = 0U; index < shortcuts_.size(); ++index) {
        const auto& shortcut = shortcuts_[index];
        if (shortcut.aliases.empty() || shortcut.name.empty() || shortcut.target.empty() ||
            !shortcut_alias_matches(shortcut.aliases, snapshot_.input)) {
            continue;
        }
        EnglishCandidate action;
        action.word = shortcut_candidate_label(shortcut);
        action.base_weight = (std::numeric_limits<std::uint64_t>::max)();
        action.action_target = shortcut_action_target(shortcut);
        matched.push_back({static_cast<std::size_t>(shortcut.position), index,
            std::move(action)});
    }
    std::stable_sort(matched.begin(), matched.end(), [](const auto& left, const auto& right) {
        if (left.position != right.position) return left.position < right.position;
        return left.order < right.order;
    });
    for (auto iterator = matched.rbegin(); iterator != matched.rend(); ++iterator) {
        auto& action = *iterator;
        if (action.position == 0U || action.position > candidate_limit_) continue;
        if (candidates.size() >= candidate_limit_) candidates.pop_back();
        const std::size_t at = (std::min)(action.position - 1U, candidates.size());
        candidates.insert(candidates.begin() + static_cast<std::ptrdiff_t>(at),
            std::move(action.candidate));
    }
    if (candidates.size() > candidate_limit_) {
        candidates.resize(candidate_limit_);
    }
    snapshot_.candidates = std::move(candidates);
}

}  // namespace piinput
