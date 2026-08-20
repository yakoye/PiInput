#include "piinput/host_session.h"

#include "piinput/candidate_layout.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace piinput {

namespace {

struct SymbolRequest final {
    std::string query;
    bool browse{};
};

[[nodiscard]] std::optional<SymbolRequest> symbol_request_for_input(
    const std::string_view input) {
    constexpr std::string_view semicolon_command = ";;f";
    constexpr std::string_view grave_command = "``f";
    if (input.starts_with(semicolon_command)) {
        return SymbolRequest{std::string(input.substr(semicolon_command.size())),
            input.size() == semicolon_command.size()};
    }
    if (input.starts_with(grave_command)) {
        return SymbolRequest{std::string(input.substr(grave_command.size())),
            input.size() == grave_command.size()};
    }
    if (input.starts_with(';') && !input.starts_with(";;")) {
        return SymbolRequest{std::string(input.substr(1U)), false};
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<SymbolCandidate> resolve_symbols(
    const SymbolIndex& symbols,
    const std::string_view input,
    const std::size_t limit) {
    const auto request = symbol_request_for_input(input);
    if (!request) return {};
    return request->browse ? symbols.browse(limit) : symbols.search(request->query, limit);
}

[[nodiscard]] bool contains_at_least_two_non_ascii_codepoints(
    const std::string_view text) noexcept {
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first < 0x80U) return false;
        std::size_t length = 0U;
        if ((first & 0xE0U) == 0xC0U) length = 2U;
        else if ((first & 0xF0U) == 0xE0U) length = 3U;
        else if ((first & 0xF8U) == 0xF0U) length = 4U;
        else return false;
        if (index + length > text.size()) return false;
        for (std::size_t offset = 1U; offset < length; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0U) != 0x80U) {
                return false;
            }
        }
        ++count;
        index += length;
    }
    return count >= 2U;
}

}  // namespace

HostSession::HostSession(
    Engine& engine,
    EnglishLexicon* const english_lexicon,
    SettingsSnapshot settings,
    std::string schema)
    : HostSession(engine, english_lexicon, nullptr, std::move(settings), std::move(schema)) {}

HostSession::HostSession(
    Engine& engine,
    EnglishLexicon* const english_lexicon,
    SymbolIndex* const symbol_index,
    SettingsSnapshot settings,
    std::string schema)
    : settings_(std::move(settings)),
      schema_(std::move(schema)),
      chinese_(engine, schema_, settings_),
      english_lexicon_(english_lexicon),
      symbol_index_(symbol_index),
      candidate_grid_(settings_.candidates, 0U) {
    mode_ = settings_.general.default_language == DefaultInputLanguage::english
        ? HostInputMode::english
        : HostInputMode::chinese;
    if (english_lexicon_ != nullptr && settings_.english.enabled) {
        english_ = std::make_unique<EnglishSession>(
            *english_lexicon_, settings_.candidates.max_items, settings_.english.user_learning);
    }
}

HostReply HostSession::apply(const HostKeyEvent& event) {
    if (event.kind == HostKeyKind::open_symbol_center) {
        if (symbol_index_ == nullptr) return reply(false, HostAction::none);
        if (english_ != nullptr) english_->clear();
        mode_ = HostInputMode::chinese;
        chinese_.set_input(";;f");
        advance_generation(true);
        return reply(true, HostAction::update);
    }
    if (event.kind == HostKeyKind::switch_to_english) {
        chinese_.clear();
        if (english_ != nullptr) english_->clear();
        mode_ = HostInputMode::english;
        advance_generation(true);
        return reply(true, HostAction::cancel);
    }
    if (event.kind == HostKeyKind::switch_to_chinese) {
        if (english_ != nullptr) english_->clear();
        chinese_.clear();
        mode_ = HostInputMode::chinese;
        advance_generation(true);
        return reply(true, HostAction::cancel);
    }
    if (mode_ == HostInputMode::english && english_ == nullptr &&
        event.kind == HostKeyKind::text) {
        return reply(true, HostAction::commit, std::string(1U, event.character));
    }
    if (event.kind == HostKeyKind::select_candidate) {
        return choose(event.candidate_id);
    }
    if (event.kind == HostKeyKind::select_digit) {
        if (event.character < '1' || event.character > '9') {
            return reply(false, HostAction::none);
        }
        const std::size_t index = candidate_grid_.active_row() *
            candidate_grid_.items_per_row() +
            static_cast<std::size_t>(event.character - '1');
        return index < current_candidate_count()
            ? choose(candidate_id_at(index))
            : reply(false, HostAction::none);
    }
    if (event.kind == HostKeyKind::punctuation ||
        event.kind == HostKeyKind::literal_punctuation) {
        const std::string symbol = punctuation_.transform(
            event.character,
            event.kind == HostKeyKind::literal_punctuation || mode_ == HostInputMode::english
                ? PunctuationMode::english
                : settings_.punctuation,
            event.shifted,
            settings_.punctuation_bracket_style);
        if (current_raw().empty()) {
            return reply(true, HostAction::commit, symbol);
        }
        const std::size_t index = selected_candidate_index();
        if (index < current_candidate_count()) {
            auto chosen = choose(candidate_id_at(index));
            // Punctuation is an explicit composition boundary. If the visible
            // lexical candidate covers only a prefix, resolve the remaining
            // syllables with the first real word/character of each segment.
            // Those intermediate choices never appear as a fabricated
            // sentence candidate; they only finish the user's pending text so
            // the symbol can be committed in the same edit.
            std::size_t remaining_guard = 64U;
            while (chosen.accepted && chosen.action == HostAction::update &&
                   mode_ == HostInputMode::chinese && remaining_guard-- != 0U) {
                const std::size_t next = selected_candidate_index();
                if (next >= current_candidate_count()) break;
                chosen = choose(candidate_id_at(next));
            }
            if (chosen.accepted && chosen.action == HostAction::commit) chosen.text += symbol;
            return chosen;
        }
        const std::string raw = current_raw();
        if (mode_ == HostInputMode::english && english_ != nullptr) english_->clear();
        else chinese_.clear();
        advance_generation(true);
        return reply(true, HostAction::commit, raw + symbol);
    }
    if (event.kind == HostKeyKind::previous_candidate ||
        event.kind == HostKeyKind::next_candidate) {
        if (current_candidate_count() == 0U) return reply(false, HostAction::none);
        const std::size_t before = candidate_grid_.selected_index();
        candidate_grid_.move_column(
            event.kind == HostKeyKind::next_candidate ? 1 : -1);
        const bool changed = before != candidate_grid_.selected_index();
        if (changed) advance_generation(false);
        return reply(changed, changed ? HostAction::update : HostAction::none);
    }
    if (event.kind == HostKeyKind::expand_next_row) {
        if (current_candidate_count() == 0U) return reply(false, HostAction::none);
        if (mode_ == HostInputMode::chinese &&
            chinese_.snapshot().view_mode == CandidateViewMode::normal) {
            if (candidate_grid_.can_move_row(
                    1, chinese_.snapshot().normal_browse_candidate_count)) {
                candidate_grid_.move_row(1);
                advance_generation(false);
                return reply(true, HostAction::update);
            }
            normal_return_index_ = candidate_grid_.active_row() *
                candidate_grid_.items_per_row();
            // Everything through the row the user is on stays on screen; the
            // per-syllable choices are appended underneath it.
            const std::size_t visible_normal_candidates =
                (candidate_grid_.active_row() + 1U) * candidate_grid_.items_per_row();
            if (chinese_.enter_segment_selection(visible_normal_candidates)) {
                advance_generation(true);
                select_first_segment_candidate();
                return reply(true, HostAction::update);
            }
            normal_return_index_ = 0U;
            return reply(false, HostAction::none);
        }
        const auto before = candidate_grid_.active_row();
        candidate_grid_.move_row(1);
        const bool changed = before != candidate_grid_.active_row();
        if (changed) advance_generation(false);
        return reply(changed, changed ? HostAction::update : HostAction::none);
    }
    if (event.kind == HostKeyKind::previous_row) {
        if (current_candidate_count() != 0U) {
            const auto before = candidate_grid_.active_row();
            const auto rows = candidate_grid_.visible_rows();
            candidate_grid_.move_row(-1);
            const bool changed = before != candidate_grid_.active_row() || rows != candidate_grid_.visible_rows();
            if (changed) {
                advance_generation(false);
                return reply(true, HostAction::update);
            }
            if (mode_ == HostInputMode::chinese &&
                chinese_.snapshot().view_mode == CandidateViewMode::segment_selection &&
                chinese_.leave_segment_selection()) {
                const std::size_t return_index = normal_return_index_;
                advance_generation(true);
                candidate_grid_.select_index(return_index);
                normal_return_index_ = 0U;
                return reply(true, HostAction::update);
            }
        }
        // Nothing to page. A dash then means the user wants the character
        // itself, so it commits the current candidate and inserts the dash
        // exactly like every other punctuation key. Arrow-up carries no
        // character and stays a pure navigation key.
        if (event.character == '-') {
            HostKeyEvent as_punctuation = event;
            as_punctuation.kind = HostKeyKind::punctuation;
            return apply(as_punctuation);
        }
        return reply(false, HostAction::none);
    }
    if (event.kind == HostKeyKind::space) {
        const std::size_t index = selected_candidate_index();
        if (index < current_candidate_count()) {
            return choose(candidate_id_at(index));
        }
        if (mode_ == HostInputMode::chinese &&
            (current_raw() == ";" || current_raw().starts_with(";;") ||
             current_raw().starts_with('`'))) {
            std::string literal = current_raw();
            if (literal == "`" || literal == ";") {
                literal = punctuation_.transform(literal.front(), settings_.punctuation, false,
                    settings_.punctuation_bracket_style);
            }
            chinese_.clear();
            advance_generation(true);
            return reply(true, HostAction::commit, std::move(literal));
        }
        return reply(false, HostAction::none);
    }
    if (event.kind == HostKeyKind::enter) {
        if (current_raw().empty()) return reply(false, HostAction::pass_through);
        const std::string raw = current_raw();
        if (mode_ == HostInputMode::english && english_ != nullptr) english_->clear();
        else chinese_.clear();
        advance_generation(true);
        return reply(true, HostAction::commit, raw);
    }
    if (event.kind == HostKeyKind::escape) {
        const bool composing = !current_raw().empty();
        if (mode_ == HostInputMode::english && english_ != nullptr) english_->clear();
        else chinese_.clear();
        if (composing) advance_generation(true);
        return reply(composing, composing ? HostAction::cancel : HostAction::pass_through);
    }

    const bool changed = edit(event);
    if (!changed) return reply(false, HostAction::pass_through);
    advance_generation(true);
    return reply(true, snapshot().raw.empty() ? HostAction::cancel : HostAction::update);
}

HostSnapshot HostSession::snapshot() const {
    HostSnapshot result;
    result.generation = generation_;
    result.mode = mode_;
    result.view = {
        .expanded = candidate_grid_.visible_rows() > 1U,
        .items_per_row = candidate_grid_.items_per_row(),
        .visible_rows = candidate_grid_.visible_rows(),
        .active_row = candidate_grid_.active_row(),
        .first_visible_row = candidate_grid_.first_visible_row(),
        .active_column = candidate_grid_.active_column(),
    };

    if (mode_ == HostInputMode::english && english_ != nullptr) {
        const auto& source = english_->snapshot();
        result.raw = source.input;
        result.composition_text = source.input;
        result.caret = source.caret;
        result.candidates.reserve(source.candidates.size());
        for (std::size_t index = 0; index < source.candidates.size(); ++index) {
            result.candidates.push_back({
                (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U),
                source.candidates[index].word,
                {},
                static_cast<std::int64_t>((std::min)(
                    source.candidates[index].base_weight,
                    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))),
            });
        }
        return result;
    }

    const auto& source = chinese_.snapshot();
    result.raw = source.input;
    result.composition_text = source.view_mode == CandidateViewMode::segment_selection
        ? source.staged_text + source.remaining_pinyin
        : source.input;
    result.caret = source.view_mode == CandidateViewMode::segment_selection
        ? result.composition_text.size()
        : source.caret;
    result.view.mode = source.view_mode == CandidateViewMode::segment_selection
        ? HostCandidateMode::segment_selection
        : HostCandidateMode::normal;
    if (symbol_index_ != nullptr && symbol_request_for_input(source.input)) {
        const auto symbols = resolve_symbols(
            *symbol_index_, source.input, settings_.candidates.max_items);
        result.candidates.reserve(symbols.size());
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            result.candidates.push_back({
                (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U),
                symbols[index].symbol,
                {},
                symbols[index].score,
            });
        }
        return result;
    }
    result.candidates.reserve(source.candidates.size());
    for (std::size_t index = 0; index < source.candidates.size(); ++index) {
        const auto& candidate = source.candidates[index].candidate;
        result.candidates.push_back({
            (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U),
            candidate.word,
            candidate.pinyin,
            candidate.score,
        });
    }
    return result;
}

HostReply HostSession::manage_candidate(
    const std::uint64_t candidate_id,
    const CandidateManagementAction action) {
    const std::uint64_t requested_generation = candidate_id >> 32U;
    const std::uint64_t ordinal = candidate_id & 0xffffffffULL;
    const auto& source = chinese_.snapshot();
    if (mode_ != HostInputMode::chinese ||
        source.view_mode != CandidateViewMode::normal ||
        requested_generation != generation_ || ordinal == 0U ||
        ordinal > source.candidates.size()) {
        return reply(false, HostAction::none);
    }
    const EngineCandidate candidate =
        source.candidates[static_cast<std::size_t>(ordinal - 1U)].candidate;
    if (action == CandidateManagementAction::pin_first) {
        chinese_.pin_candidate(candidate);
        advance_generation(true);
        return reply(true, HostAction::update);
    }
    if (action == CandidateManagementAction::unpin) {
        chinese_.unpin_candidate(candidate);
        advance_generation(true);
        return reply(true, HostAction::update);
    }
    const std::size_t deleted_index = static_cast<std::size_t>(ordinal - 1U);
    chinese_.delete_candidate(candidate);
    advance_generation(false);
    if (current_candidate_count() != 0U) {
        candidate_grid_.select_index((std::min)(
            deleted_index, current_candidate_count() - 1U));
    }
    return reply(true, HostAction::update);
}

bool HostSession::composing() const noexcept {
    return !current_raw().empty();
}

HostResumeState HostSession::resume_state() const {
    const auto current = snapshot();
    return {current.generation, current.raw, current.caret, current.mode};
}

void HostSession::restore(const HostResumeState& state) {
    mode_ = state.mode;
    if (mode_ == HostInputMode::english && english_ != nullptr) {
        english_->clear();
        for (const char character : state.raw) (void)english_->insert(character);
        (void)english_->move_home();
        for (std::size_t index = 0; index < (std::min)(state.caret, state.raw.size()); ++index) {
            (void)english_->move_right();
        }
    } else if (mode_ == HostInputMode::english) {
        // Direct English mode has no composition state: every printable key is
        // committed immediately. Preserve the mode across a Host restart even
        // when English candidates are disabled.
        chinese_.clear();
    } else {
        chinese_.set_input(state.raw);
        chinese_.move_home();
        for (std::size_t index = 0; index < (std::min)(state.caret, state.raw.size()); ++index) {
            (void)chinese_.move_right();
        }
    }
    generation_ = (std::max)(generation_, state.generation) + 1U;
    rebuild_candidate_grid(true);
}

void HostSession::start_after_generation(
    const std::uint64_t previous_generation) noexcept {
    generation_ = (std::max)(generation_, previous_generation) + 1U;
    rebuild_candidate_grid(true);
}

bool HostSession::edit(const HostKeyEvent& event) {
    if (mode_ == HostInputMode::english) {
        if (english_ == nullptr) return false;
        switch (event.kind) {
        case HostKeyKind::text: return english_->insert(event.character);
        case HostKeyKind::backspace: return english_->backspace();
        case HostKeyKind::delete_forward: return english_->delete_forward();
        case HostKeyKind::move_left: return english_->move_left();
        case HostKeyKind::move_right: return english_->move_right();
        case HostKeyKind::move_home: return english_->move_home();
        case HostKeyKind::move_end: return english_->move_end();
        default: return false;
        }
    }
    switch (event.kind) {
    case HostKeyKind::text:
        chinese_.insert(event.character);
        return true;
    case HostKeyKind::backspace: return chinese_.backspace();
    case HostKeyKind::delete_forward: return chinese_.delete_forward();
    case HostKeyKind::move_left: return chinese_.move_left();
    case HostKeyKind::move_right: return chinese_.move_right();
    case HostKeyKind::move_home:
        if (chinese_.snapshot().caret == 0U) return false;
        chinese_.move_home();
        return true;
    case HostKeyKind::move_end:
        if (chinese_.snapshot().caret == chinese_.snapshot().input.size()) return false;
        chinese_.move_end();
        return true;
    default: return false;
    }
}

HostReply HostSession::choose(const std::uint64_t candidate_id) {
    const std::uint64_t requested_generation = candidate_id >> 32U;
    const std::uint64_t ordinal = candidate_id & 0xffffffffULL;
    if (requested_generation != generation_ || ordinal == 0U ||
        ordinal > current_candidate_count()) {
        return reply(false, HostAction::none);
    }
    const std::size_t index = static_cast<std::size_t>(ordinal - 1U);
    std::optional<std::string> chosen;
    if (mode_ == HostInputMode::english && english_ != nullptr) {
        chosen = english_->choose(index);
    } else if (symbol_index_ != nullptr &&
               symbol_request_for_input(chinese_.snapshot().input)) {
        const auto symbols = resolve_symbols(
            *symbol_index_, chinese_.snapshot().input, settings_.candidates.max_items);
        if (index < symbols.size()) {
            chosen = symbols[index].symbol;
            chinese_.clear();
        }
    } else {
        const auto staged = chinese_.snapshot().view_mode ==
                CandidateViewMode::segment_selection
            ? chinese_.stage_candidate(chinese_.snapshot().candidates[index].id)
            : chinese_.choose(chinese_.snapshot().candidates[index].id);
        if (!staged.accepted) return reply(false, HostAction::none);
        if (!staged.commit_text.has_value()) {
            // A real prefix word or character moved into the composition.
            // Continue on the remaining syllables instead of committing early.
            advance_generation(true);
            select_first_segment_candidate();
            return reply(true, HostAction::update);
        }
        advance_generation(true);
        if (settings_.pinyin.user_learning &&
            (!staged.user_created ||
                contains_at_least_two_non_ascii_codepoints(*staged.commit_text))) {
            pending_learning_[generation_] = PendingLearning{
                staged.selection_pinyin, *staged.commit_text, staged.user_created,
                staged.learning_segments};
            while (pending_learning_.size() > 8U) {
                pending_learning_.erase(pending_learning_.begin());
            }
        }
        return reply(true, HostAction::commit, *staged.commit_text);
    }
    if (!chosen.has_value()) return reply(false, HostAction::none);
    advance_generation(true);
    return reply(true, HostAction::commit, std::move(*chosen));
}

bool HostSession::confirm_commit(
    const std::uint64_t generation,
    const bool succeeded) {
    const auto found = pending_learning_.find(generation);
    if (found == pending_learning_.end()) return false;
    PendingLearning learning = std::move(found->second);
    pending_learning_.erase(found);
    if (!succeeded) return true;
    if (learning.user_created) {
        chinese_.record_composed_phrase(learning.canonical_pinyin, learning.word);
    } else {
        chinese_.record_committed_selection(learning.canonical_pinyin, learning.word);
    }
    if (learning.user_created && !learning.segments.empty()) {
        std::unordered_set<std::string> recorded;
        recorded.insert(learning.word + "\n" + learning.canonical_pinyin);
        for (const auto& segment : learning.segments) {
            if (!contains_at_least_two_non_ascii_codepoints(segment.word)) continue;
            const std::string key = segment.word + "\n" + segment.pinyin;
            if (recorded.insert(key).second) {
                chinese_.record_committed_selection(segment.pinyin, segment.word);
            }
        }
        for (std::size_t index = 1U; index < learning.segments.size(); ++index) {
            const auto& left = learning.segments[index - 1U];
            const auto& right = learning.segments[index];
            const std::string word = left.word + right.word;
            const std::string pinyin = left.pinyin + "'" + right.pinyin;
            const std::string key = word + "\n" + pinyin;
            if (recorded.insert(key).second) {
                chinese_.record_composed_phrase(pinyin, word);
            }
        }
    }
    return true;
}

HostReply HostSession::reply(
    const bool accepted,
    const HostAction action,
    std::string text) const {
    return {accepted, action, std::move(text), snapshot()};
}

void HostSession::advance_generation(const bool collapse_view) {
    ++generation_;
    rebuild_candidate_grid(collapse_view);
    if (collapse_view && mode_ == HostInputMode::chinese &&
        chinese_.snapshot().view_mode == CandidateViewMode::normal) {
        normal_return_index_ = 0U;
    }
}

void HostSession::rebuild_candidate_grid(const bool collapse_view) {
    // Only the leading configured columns can change the layout, and the
    // candidate count is the single other value the grid needs. Building the
    // full presentation list here used to copy up to 90 words per key.
    const std::size_t configured =
        (std::max<std::size_t>)(1U, settings_.candidates.items_per_row);
    std::vector<std::string_view> leading;
    leading.reserve(configured);
    std::vector<SymbolCandidate> symbols;
    std::size_t count = 0U;
    const auto take_leading = [&](const auto& items, auto&& text_of) {
        count = items.size();
        const std::size_t limit = (std::min)(configured, count);
        for (std::size_t index = 0U; index < limit; ++index) {
            leading.emplace_back(text_of(items[index]));
        }
    };
    if (mode_ == HostInputMode::english && english_ != nullptr) {
        take_leading(english_->snapshot().candidates,
            [](const auto& candidate) -> const std::string& { return candidate.word; });
    } else if (symbol_index_ != nullptr &&
               symbol_request_for_input(chinese_.snapshot().input)) {
        symbols = resolve_symbols(
            *symbol_index_, chinese_.snapshot().input, settings_.candidates.max_items);
        take_leading(symbols,
            [](const auto& candidate) -> const std::string& { return candidate.symbol; });
    } else {
        take_leading(chinese_.snapshot().candidates,
            [](const auto& candidate) -> const std::string& {
                return candidate.candidate.word;
            });
    }
    candidate_grid_.set_items_per_row(static_cast<std::uint32_t>(candidate_items_per_row(
        settings_.candidates.items_per_row, std::span<const std::string_view>(leading))));
    if (collapse_view) candidate_grid_.reset(count);
    else candidate_grid_.set_candidate_count(count);
}

std::size_t HostSession::current_candidate_count() const noexcept {
    // rebuild_candidate_grid() is the single writer of this count and runs on
    // every generation change, so the grid is always authoritative. Recomputing
    // it here would re-run the symbol search on the keystroke hot path.
    return candidate_grid_.candidate_count();
}

void HostSession::select_first_segment_candidate() {
    if (mode_ != HostInputMode::chinese) return;
    const auto& segmented = chinese_.snapshot();
    if (segmented.view_mode != CandidateViewMode::segment_selection) return;
    const std::size_t first_segment = segmented.segment_candidate_offset;
    if (first_segment < segmented.candidates.size()) {
        candidate_grid_.select_index(first_segment);
    }
}

const std::string& HostSession::current_raw() const noexcept {
    if (mode_ == HostInputMode::english && english_ != nullptr) {
        return english_->snapshot().input;
    }
    return chinese_.snapshot().input;
}

std::size_t HostSession::selected_candidate_index() const noexcept {
    return candidate_grid_.active_row() * candidate_grid_.items_per_row() +
        candidate_grid_.active_column();
}

std::uint64_t HostSession::candidate_id_at(const std::size_t index) const noexcept {
    return (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U);
}

}  // namespace piinput
