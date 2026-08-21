#include "piinput/session.h"

#include "piinput/candidate_layout.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace piinput {

ImeSession::ImeSession(Engine& engine, std::string schema, const std::size_t candidate_limit)
    : engine_(&engine),
      schema_(std::move(schema)),
      candidate_limit_(candidate_limit),
      settings_(default_settings()) {
    settings_.candidates.max_items = static_cast<std::uint32_t>((std::min)(
        candidate_limit_,
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    refresh();
}

ImeSession::ImeSession(Engine& engine, std::string schema, SettingsSnapshot settings)
    : engine_(&engine),
      schema_(std::move(schema)),
      candidate_limit_(settings.candidates.max_items),
      settings_(std::move(settings)) {
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
    snapshot_.segment_selection.clear();
    refresh();
}

void ImeSession::insert(const char character) {
    if (snapshot_.view_mode == CandidateViewMode::segment_selection) {
        snapshot_.segment_selection.clear();
    }
    snapshot_.input.insert(snapshot_.caret, 1U, character);
    ++snapshot_.caret;
    refresh();
}

bool ImeSession::backspace() {
    if (snapshot_.view_mode == CandidateViewMode::segment_selection && undo_segment()) {
        return true;
    }
    if (snapshot_.view_mode == CandidateViewMode::segment_selection) {
        (void)leave_segment_selection();
    }
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
    if (snapshot_.input.empty() &&
        snapshot_.view_mode == CandidateViewMode::normal) {
        return;
    }
    snapshot_.input.clear();
    snapshot_.caret = 0U;
    snapshot_.segment_selection.clear();
    refresh();
}

void ImeSession::restore(CandidateSnapshot snapshot) {
    snapshot_ = std::move(snapshot);
}

const CandidateSnapshot& ImeSession::snapshot() const noexcept {
    return snapshot_;
}

SegmentStageResult ImeSession::choose(const std::uint64_t candidate_id) {
    if (snapshot_.view_mode == CandidateViewMode::segment_selection) {
        return stage_candidate(candidate_id);
    }
    const std::uint64_t requested_generation = candidate_id >> 32U;
    if (requested_generation != snapshot_.generation) {
        return {};
    }
    const auto found = std::find_if(snapshot_.candidates.begin(), snapshot_.candidates.end(), [&](const SessionCandidate& item) {
        return item.id == candidate_id;
    });
    if (found == snapshot_.candidates.end()) {
        return {};
    }
    const EngineCandidate candidate = found->candidate;
    const auto parsed = engine_->parse_composition(
        snapshot_.input, schema_, settings_.pinyin);
    if (parsed.has_value() && !candidate.evidence.covers_all_input &&
        candidate.consumed_syllables != 0U &&
        candidate.consumed_syllables <= parsed->syllables.size()) {
        retained_normal_candidates_ = 0U;
        normal_candidates_cache_.clear();
        snapshot_.segment_selection.begin(*parsed);
        snapshot_.view_mode = CandidateViewMode::segment_selection;
        if (!snapshot_.segment_selection.stage(
                candidate.word, candidate.pinyin, candidate.consumed_syllables)) {
            snapshot_.segment_selection.clear();
            snapshot_.view_mode = CandidateViewMode::normal;
            refresh();
            return {};
        }
        refresh_segment();
        return {true, std::nullopt};
    }
    const std::string result = candidate.word;
    const std::string pinyin = candidate.pinyin;
    clear();
    return {true, result, pinyin, false};
}

bool ImeSession::enter_segment_selection(const std::size_t retained_normal_candidates) {
    if (snapshot_.input.empty()) return false;
    const auto parsed = engine_->parse_composition(snapshot_.input, schema_);
    if (!parsed.has_value() || parsed->syllables.empty()) return false;
    retained_normal_candidates_ = retained_normal_candidates;
    snapshot_.segment_selection.begin(*parsed);
    snapshot_.view_mode = CandidateViewMode::segment_selection;
    refresh_segment();
    // Whether there is anything to choose between, not whether the list is
    // non-empty. refresh_segment keeps the ordinary candidates the user was
    // already looking at and appends the per-syllable ones after them, so the
    // list is never empty and this always reported success -- even when the
    // syllable step contributed nothing. The caller then reset the view and
    // showed the same candidates from the top, which reads as the list looping
    // back to the start instead of ending.
    if (snapshot_.candidates.size() > snapshot_.segment_candidate_offset) return true;

    // Undo the whole attempt, view mode included: it was set before it was
    // known whether there would be anything to show, and leaving it behind made
    // the session report segment selection while publishing ordinary
    // candidates.
    snapshot_.view_mode = CandidateViewMode::normal;
    snapshot_.segment_selection.clear();
    retained_normal_candidates_ = 0U;
    ++snapshot_.generation;
    publish_cached_normal_candidates();
    return false;
}

bool ImeSession::leave_segment_selection() {
    if (snapshot_.view_mode != CandidateViewMode::segment_selection) return false;
    snapshot_.segment_selection.clear();
    retained_normal_candidates_ = 0U;
    ++snapshot_.generation;
    publish_cached_normal_candidates();
    return true;
}

SegmentStageResult ImeSession::stage_candidate(const std::uint64_t candidate_id) {
    if (snapshot_.view_mode != CandidateViewMode::segment_selection ||
        (candidate_id >> 32U) != snapshot_.generation) {
        return {};
    }
    const auto found = std::find_if(
        snapshot_.candidates.begin(), snapshot_.candidates.end(),
        [&](const SessionCandidate& item) { return item.id == candidate_id; });
    if (found == snapshot_.candidates.end()) return {};
    if (found->role == SessionCandidateRole::placeholder) return {};
    if (found->role == SessionCandidateRole::normal) {
        const std::string word = found->candidate.word;
        const std::string pinyin = found->candidate.pinyin;
        clear();
        return {true, word, pinyin, false};
    }
    if (!snapshot_.segment_selection.stage(
            found->candidate.word,
            found->candidate.pinyin,
            found->candidate.consumed_syllables)) {
        return {};
    }
    if (snapshot_.segment_selection.complete()) {
        const std::string result = snapshot_.segment_selection.finish();
        const std::string canonical = snapshot_.segment_selection.composition().canonical;
        const std::vector<SegmentSelectionEntry> learning_segments =
            snapshot_.segment_selection.history();
        clear();
        return {true, result, canonical, true, learning_segments};
    }
    refresh_segment();
    return {true, std::nullopt};
}

void ImeSession::record_committed_selection(
    const std::string& pinyin,
    const std::string& word) {
    engine_->record_selection(pinyin, word);
}

void ImeSession::record_composed_phrase(
    const std::string& pinyin,
    const std::string& word) {
    engine_->record_composed_phrase(pinyin, word);
}

void ImeSession::pin_candidate(const EngineCandidate& candidate) {
    engine_->pin_candidate(candidate.pinyin, candidate.word);
    refresh();
}

void ImeSession::unpin_candidate(const EngineCandidate& candidate) {
    engine_->unpin_candidate(candidate.pinyin, candidate.word);
    refresh();
}

void ImeSession::delete_candidate(const EngineCandidate& candidate) {
    engine_->remove_candidate_learning(candidate.pinyin, candidate.word);
    engine_->suppress_candidate(candidate.pinyin, candidate.word);
    refresh();
}

bool ImeSession::undo_segment() {
    if (snapshot_.view_mode != CandidateViewMode::segment_selection ||
        !snapshot_.segment_selection.undo()) {
        return false;
    }
    refresh_segment();
    return true;
}

void ImeSession::refresh() {
    ++snapshot_.generation;
    retained_normal_candidates_ = 0U;
    normal_candidates_cache_.clear();
    snapshot_.candidates.clear();
    snapshot_.view_mode = CandidateViewMode::normal;
    snapshot_.staged_text.clear();
    snapshot_.remaining_pinyin.clear();
    snapshot_.normal_browse_candidate_count = 0U;
    snapshot_.segment_candidate_offset = 0U;
    if (snapshot_.input.empty()) {
        return;
    }
    normal_candidates_cache_ = engine_->query(
        snapshot_.input, schema_, candidate_limit_, settings_);
    publish_cached_normal_candidates();
}

void ImeSession::publish_cached_normal_candidates() {
    snapshot_.candidates.clear();
    snapshot_.view_mode = CandidateViewMode::normal;
    snapshot_.staged_text.clear();
    snapshot_.remaining_pinyin.clear();
    snapshot_.normal_browse_candidate_count = 0U;
    snapshot_.segment_candidate_offset = 0U;
    snapshot_.candidates.reserve(normal_candidates_cache_.size());
    for (std::size_t index = 0U; index < normal_candidates_cache_.size(); ++index) {
        const std::uint64_t id = (snapshot_.generation << 32U) | static_cast<std::uint64_t>(index + 1U);
        snapshot_.candidates.push_back({
            id, normal_candidates_cache_[index], SessionCandidateRole::normal});
    }
    // Normal query results are now all backed by one real dictionary entry or
    // confirmed personal learning. Words may span several rows and the single
    // characters after them remain ordinary browseable candidates.
    snapshot_.normal_browse_candidate_count = normal_candidates_cache_.size();
}

void ImeSession::refresh_segment() {
    ++snapshot_.generation;
    snapshot_.candidates.clear();
    snapshot_.view_mode = CandidateViewMode::segment_selection;
    snapshot_.staged_text = snapshot_.segment_selection.staged_text();
    snapshot_.remaining_pinyin = snapshot_.segment_selection.remaining_pinyin();
    // Keep every ordinary word the user was already looking at. The trusted
    // prefix run alone can be a single entry even when a full row of real
    // dictionary words is on screen, and dropping them made '=' look like it
    // replaced the candidates instead of continuing past them.
    const std::size_t normal_count = (std::min)(
        retained_normal_candidates_, normal_candidates_cache_.size());
    auto candidates = engine_->query_segment(
        snapshot_.segment_selection.composition(),
        snapshot_.segment_selection.syllable_offset(),
        candidate_limit_);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&](const EngineCandidate& candidate) {
            return std::any_of(normal_candidates_cache_.begin(),
                normal_candidates_cache_.begin() + static_cast<std::ptrdiff_t>(normal_count),
                [&](const EngineCandidate& existing) {
                    return existing.word == candidate.word &&
                        existing.pinyin == candidate.pinyin;
                });
        }), candidates.end());

    const std::size_t configured =
        (std::max<std::size_t>)(1U, settings_.candidates.items_per_row);
    std::vector<std::string_view> presentation;
    presentation.reserve(configured);
    for (std::size_t index = 0U; index < normal_count && presentation.size() < configured;
         ++index) {
        presentation.emplace_back(normal_candidates_cache_[index].word);
    }
    for (const auto& candidate : candidates) {
        if (presentation.size() >= configured) break;
        presentation.emplace_back(candidate.word);
    }
    const std::size_t row_size = candidate_items_per_row(
        settings_.candidates.items_per_row,
        std::span<const std::string_view>(presentation));

    snapshot_.candidates.reserve(normal_count + row_size + candidates.size());
    for (std::size_t index = 0U; index < normal_count; ++index) {
        const std::uint64_t id =
            (snapshot_.generation << 32U) | static_cast<std::uint64_t>(index + 1U);
        snapshot_.candidates.push_back({
            id, normal_candidates_cache_[index], SessionCandidateRole::normal});
    }
    if (normal_count != 0U) {
        const std::size_t padding = (row_size - normal_count % row_size) % row_size;
        for (std::size_t index = 0U; index < padding; ++index) {
            const std::size_t ordinal = snapshot_.candidates.size();
            const std::uint64_t id = (snapshot_.generation << 32U) |
                static_cast<std::uint64_t>(ordinal + 1U);
            snapshot_.candidates.push_back({
                id, EngineCandidate{}, SessionCandidateRole::placeholder});
        }
    }
    snapshot_.segment_candidate_offset = snapshot_.candidates.size();
    for (const auto& candidate : candidates) {
        const std::size_t index = snapshot_.candidates.size();
        const std::uint64_t id =
            (snapshot_.generation << 32U) | static_cast<std::uint64_t>(index + 1U);
        snapshot_.candidates.push_back({id, candidate, SessionCandidateRole::segment});
    }
    snapshot_.normal_browse_candidate_count = normal_count;
}

}  // namespace piinput
