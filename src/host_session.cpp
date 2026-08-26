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
    constexpr std::string_view grave_command = "``f";
    if (input.starts_with(grave_command)) {
        return SymbolRequest{std::string(input.substr(grave_command.size())),
            input.size() == grave_command.size()};
    }
    return std::nullopt;
}

// Entries whose candidate number the user configured, or that open a list
// rather than committing text. English words go after them so their numbers
// stay put: calc opens the calculator from position two because it was put
// there, and mixing in a word above it would silently renumber it.
[[nodiscard]] bool is_reserved_action(const CandidateKind kind) noexcept {
    return kind == CandidateKind::symbol_tool_action ||
           kind == CandidateKind::emoji_tool_action ||
           kind == CandidateKind::settings_action ||
           kind == CandidateKind::launch_action ||
           kind == CandidateKind::datetime_group;
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
      engine_(&engine),
      english_lexicon_(english_lexicon),
      symbol_index_(symbol_index),
      candidate_grid_(settings_.candidates, 0U) {
    mode_ = settings_.general.default_language == DefaultInputLanguage::english
        ? HostInputMode::english
        : HostInputMode::chinese;
    if (english_lexicon_ != nullptr && settings_.english.enabled) {
        english_ = std::make_unique<EnglishSession>(
            *english_lexicon_, settings_.candidates.max_items,
            settings_.english.user_learning, settings_.custom_shortcuts);
    }
}

HostReply HostSession::apply(const HostKeyEvent& event) {
    if (event.kind == HostKeyKind::open_symbol_center) {
        if (symbol_index_ == nullptr) return reply(false, HostAction::none);
        if (english_ != nullptr) english_->clear();
        mode_ = HostInputMode::chinese;
        chinese_.set_input("``f");
        advance_generation(true);
        return reply(true, HostAction::update);
    }
    if (event.kind == HostKeyKind::switch_to_english) {
        // A standalone Shift is also an explicit boundary for the Chinese
        // composition. Preserve exactly what the user typed instead of
        // selecting a Chinese candidate or cancelling the raw letters. This
        // makes "cmd" + Shift commit "cmd" and then enter English mode.
        const std::string raw = current_raw();
        chinese_.clear();
        if (english_ != nullptr) english_->clear();
        mode_ = HostInputMode::english;
        advance_generation(true);
        return raw.empty()
            ? reply(true, HostAction::cancel)
            : reply(true, HostAction::commit, raw);
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
            // Accepting the request is not the same as having something to show.
            // It could report success and leave the view in normal mode, and the
            // generation change that followed reset the grid -- folding the rows
            // and jumping back to the top with the same candidates underneath.
            // Held down, that read as the list looping round and round instead
            // of reaching an end.
            if (chinese_.enter_segment_selection(visible_normal_candidates) &&
                chinese_.snapshot().view_mode == CandidateViewMode::segment_selection) {
                advance_generation(true);
                select_first_segment_candidate();
                return reply(true, HostAction::update);
            }
            normal_return_index_ = 0U;
            // The bottom, and it stays there. The key is still consumed --
            // letting it through typed an equals sign into the middle of a
            // composition, which is never what paging meant.
            return reply(true, HostAction::none);
        }
        const auto before = candidate_grid_.active_row();
        candidate_grid_.move_row(1);
        const bool changed = before != candidate_grid_.active_row();
        if (changed) advance_generation(false);
        // Consumed either way. Reaching the last row is not a request to type
        // an equals sign into the middle of a composition.
        return reply(true, changed ? HostAction::update : HostAction::none);
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
        // At the top of an expanded list, so fold it back to the single row it
        // started as, and stop there with that row still on screen.
        if (current_candidate_count() != 0U) {
            const bool folded = candidate_grid_.expanded();
            if (folded) {
                candidate_grid_.collapse();
                advance_generation(false);
            }
            // Consumed either way. Paging up used to end by committing the
            // selected word and inserting a literal dash, which took the
            // candidates off the screen at the exact moment the user was still
            // looking through them.
            return reply(true, folded ? HostAction::update : HostAction::none);
        }
        return reply(false, HostAction::none);
    }
    if (event.kind == HostKeyKind::space) {
        const std::size_t index = selected_candidate_index();
        if (index < current_candidate_count()) {
            return choose(candidate_id_at(index));
        }
        if (mode_ == HostInputMode::chinese &&
            current_raw().starts_with('`')) {
            std::string literal = current_raw();
            if (literal == "`") {
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
        // The format list is a menu: there is no raw text worth committing
        // there, so Enter takes whatever is highlighted.
        //
        // Among ordinary candidates Enter still commits the letters as typed,
        // which is what it is for -- but only while the selection is untouched.
        // Once the user has moved it, they have picked something, and Enter
        // takes that instead of throwing the choice away.
        const std::size_t selected = selected_candidate_index();
        if ((!datetime_menu_.empty() || selected != 0U) &&
            selected < current_candidate_count()) {
            return choose(snapshot().candidates[selected].id);
        }
        if (current_raw().empty()) return reply(false, HostAction::pass_through);
        const std::string raw = current_raw();
        if (mode_ == HostInputMode::english && english_ != nullptr) english_->clear();
        else chinese_.clear();
        advance_generation(true);
        return reply(true, HostAction::commit, raw);
    }
    if (event.kind == HostKeyKind::escape && !datetime_menu_.empty()) {
        // Back to the words, not out of the composition entirely.
        close_datetime_menu();
        advance_generation(false);
        return reply(true, HostAction::update);
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
    if (!datetime_menu_.empty()) {
        result.raw = source.input;
        result.composition_text = source.input;
        result.caret = source.caret;
        result.view.mode = HostCandidateMode::normal;
        result.candidates.reserve(datetime_menu_.size());
        for (std::size_t index = 0; index < datetime_menu_.size(); ++index) {
            result.candidates.push_back({
                (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U),
                datetime_menu_[index],
                {},
                0,
            });
        }
        return result;
    }
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
    result.candidates.reserve(source.candidates.size() + english_plan_.words.size());
    for (std::size_t index = 0; index < source.candidates.size(); ++index) {
        const auto& candidate = source.candidates[index].candidate;
        result.candidates.push_back({
            0U,
            candidate.word,
            candidate.pinyin,
            candidate.score,
        });
    }
    // Inserted rather than appended: the whole point is which number the word
    // carries. english_insert_at_ already accounts for the shortcuts it must
    // not renumber.
    for (std::size_t offset = 0; offset < english_plan_.words.size(); ++offset) {
        const std::size_t at = (std::min)(
            english_insert_at_ + offset, result.candidates.size());
        result.candidates.insert(
            result.candidates.begin() + static_cast<std::ptrdiff_t>(at),
            HostCandidate{0U, english_plan_.words[offset], {}, 0});
    }
    // Numbered last so every entry carries the position it is actually shown
    // at, which is what selection and management resolve against.
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        result.candidates[index].id =
            (generation_ << 32U) | static_cast<std::uint64_t>(index + 1U);
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
        requested_generation != generation_ || ordinal == 0U) {
        return reply(false, HostAction::none);
    }
    std::size_t chinese_index = static_cast<std::size_t>(ordinal - 1U);
    // Pinning and deleting act on the dictionary, so an English word in the
    // row has nothing for them to act on. Refuse rather than silently pinning
    // whichever Chinese entry happens to share the position.
    if (!english_plan_.words.empty()) {
        if (chinese_index >= english_insert_at_ &&
            chinese_index < english_insert_at_ + english_plan_.words.size()) {
            return reply(false, HostAction::none);
        }
        if (chinese_index >= english_insert_at_) {
            chinese_index -= english_plan_.words.size();
        }
    }
    if (chinese_index >= source.candidates.size()) {
        return reply(false, HostAction::none);
    }
    const EngineCandidate candidate = source.candidates[chinese_index].candidate;
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
    // The row is renumbered after the deletion, so the selection follows the
    // position the user was looking at rather than the Chinese-side index.
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
    std::size_t index = static_cast<std::size_t>(ordinal - 1U);
    // The row the user sees has English words spliced into it, so a position
    // in that row is not a position in the Chinese candidate list. Resolve the
    // English entries first, then shift the rest back by however many of them
    // sit above.
    if (!english_plan_.words.empty()) {
        if (index >= english_insert_at_ &&
            index < english_insert_at_ + english_plan_.words.size()) {
            const std::string word = english_plan_.words[index - english_insert_at_];
            if (english_lexicon_ != nullptr && settings_.english.user_learning) {
                // Learned wherever it was typed. Which mode a word was picked
                // in says nothing about whether it will be wanted again, and a
                // word learned once is the only way the long tail is reached.
                (void)english_lexicon_->record_selection(word);
            }
            chinese_.clear();
            advance_generation(true);
            return reply(true, HostAction::commit, word);
        }
        if (index >= english_insert_at_) {
            index -= english_plan_.words.size();
        }
    }
    std::optional<std::string> chosen;
    if (!datetime_menu_.empty()) {
        if (index >= datetime_menu_.size()) return reply(false, HostAction::none);
        const std::string text = datetime_menu_[index];
        close_datetime_menu();
        chinese_.clear();
        advance_generation(true);
        return reply(true, HostAction::commit, text);
    }
    if (mode_ == HostInputMode::english && english_ != nullptr) {
        const auto action_target = english_->action_target(index);
        if (action_target.has_value()) {
            english_->clear();
            advance_generation(true);
            if (*action_target == "system:settings") {
                return reply(true, HostAction::launch_settings);
            }
            if (*action_target == "system:symbol_tool") {
                return reply(true, HostAction::launch_symbol_tool);
            }
            return reply(true, HostAction::launch_program, *action_target);
        }
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
        // The entry that stands for the date or time formats opens them instead
        // of committing. Its text is the shortest format, so the row reads
        // sensibly before it is opened.
        const auto& listed = chinese_.snapshot().candidates;
        if (index < listed.size()) {
            const CandidateKind kind = listed[index].candidate.evidence.kind;
            if (kind == CandidateKind::symbol_tool_action ||
                kind == CandidateKind::emoji_tool_action ||
                kind == CandidateKind::settings_action ||
                kind == CandidateKind::launch_action) {
                const std::string launch_target =
                    listed[index].candidate.evidence.action_target;
                if (kind == CandidateKind::launch_action && launch_target.empty()) {
                    return reply(false, HostAction::none);
                }
                chinese_.clear();
                advance_generation(true);
                if (kind == CandidateKind::launch_action) {
                    if (launch_target == "system:settings") {
                        return reply(true, HostAction::launch_settings);
                    }
                    if (launch_target == "system:symbol_tool") {
                        return reply(true, HostAction::launch_symbol_tool);
                    }
                    return reply(true, HostAction::launch_program, launch_target);
                }
                return reply(true,
                    kind == CandidateKind::settings_action
                        ? HostAction::launch_settings
                        : HostAction::launch_symbol_tool);
            }
        }
        if (index < listed.size() &&
            listed[index].candidate.evidence.kind == CandidateKind::datetime_group &&
            open_datetime_menu(listed[index].candidate.pinyin)) {
            advance_generation(false);
            candidate_grid_.select_index(0U);
            return reply(true, HostAction::update);
        }
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

void HostSession::rebuild_english_plan() {
    english_plan_ = {};
    english_insert_at_ = 0U;
    if (mode_ != HostInputMode::chinese || english_lexicon_ == nullptr ||
        !settings_.english.chinese_mode_completion) {
        return;
    }
    const auto& source = chinese_.snapshot();
    // Segment selection is a different surface with its own meaning for each
    // row; mixing dictionary words into it would only make it harder to read.
    if (source.view_mode != CandidateViewMode::normal) return;

    ChineseCandidateSummary summary;
    summary.has_candidates = !source.candidates.empty();
    if (summary.has_candidates) {
        const auto& best = source.candidates.front().candidate;
        summary.covers_all_input = best.evidence.covers_all_input;
        summary.top_score = best.score;
    }
    EnglishCompletionSettings completion;
    completion.enabled = true;
    completion.max_items = 3U;
    completion.double_pinyin = schema_ != "full";
    english_plan_ = plan_english_completion(
        source.input, summary, *english_lexicon_, completion);
    if (english_plan_.words.empty()) return;

    std::vector<bool> reserved;
    reserved.reserve(source.candidates.size());
    for (const auto& candidate : source.candidates) {
        reserved.push_back(is_reserved_action(candidate.candidate.evidence.kind));
    }
    english_insert_at_ = english_insert_index(english_plan_.start_position, reserved);
}

void HostSession::rebuild_candidate_grid(const bool collapse_view) {
    // Any new composition state replaces the format list, so it cannot outlive
    // the candidates it was opened from.
    if (collapse_view) close_datetime_menu();
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
    if (!datetime_menu_.empty()) {
        take_leading(datetime_menu_,
            [](const std::string& text) -> const std::string& { return text; });
        // A list, not a row. These are whole timestamps rather than words:
        // side by side they neither fit nor compare, and a column is how a
        // set of formats is read.
        candidate_grid_.set_items_per_row(1U);
        candidate_grid_.set_visible_rows(datetime_menu_.size());
        candidate_grid_.set_candidate_count(datetime_menu_.size());
        candidate_grid_.expand();
        return;
    } else if (mode_ == HostInputMode::english && english_ != nullptr) {
        take_leading(english_->snapshot().candidates,
            [](const auto& candidate) -> const std::string& { return candidate.word; });
    } else if (symbol_index_ != nullptr &&
               symbol_request_for_input(chinese_.snapshot().input)) {
        symbols = resolve_symbols(
            *symbol_index_, chinese_.snapshot().input, settings_.candidates.max_items);
        take_leading(symbols,
            [](const auto& candidate) -> const std::string& { return candidate.symbol; });
    } else {
        rebuild_english_plan();
        take_leading(chinese_.snapshot().candidates,
            [](const auto& candidate) -> const std::string& {
                return candidate.candidate.word;
            });
        // The mixed-in words are part of the row, so the count that drives
        // paging and selection has to include them.
        count += english_plan_.words.size();
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

bool HostSession::open_datetime_menu(const std::string& reading) {
    if (engine_ == nullptr) return false;
    auto formats = engine_->datetime_formats(reading);
    if (formats.empty()) return false;
    datetime_menu_ = std::move(formats);
    datetime_reading_ = reading;
    // The grid is rebuilt from this list by the generation change the caller
    // makes next, which is also what renumbers the candidate ids.
    return true;
}

void HostSession::close_datetime_menu() noexcept {
    datetime_menu_.clear();
    datetime_reading_.clear();
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
