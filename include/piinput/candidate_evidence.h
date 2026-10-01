#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace piinput {

enum class CandidateKind : std::uint8_t {
    user_phrase,
    exact_lexicon,
    decoded_sentence,
    prefix_lexicon,
    single_character,
    incomplete_completion,
    simplified_pinyin,
    // A symbol reached by typing its name: pai for π, qiuhe for ∑.
    symbol,
    // The entry that opens the date or time formats. Choosing it replaces the
    // candidate list with those formats instead of committing anything.
    datetime_group,
    // Candidate-row commands are kept distinct from text candidates so the
    // Host never tries to learn or commit their visible labels.
    symbol_tool_action,
    emoji_tool_action,
    settings_action,
    launch_action,
    // A stored phrase from the [phrases] table. It commits text like an ordinary
    // candidate, but it is kept as its own kind so it never reaches the user
    // model: "dzjl -> 北京市海淀区…" is not a pinyin/word fact, and learning it
    // would both pollute the reverse lookup and grow a duplicate candidate of
    // unexplained origin the next time the alias is typed.
    phrase,
};

struct CandidateEvidence {
    CandidateKind kind{CandidateKind::decoded_sentence};
    std::size_t consumed_syllables{};
    std::size_t word_count{};
    std::size_t single_character_tokens{};
    bool covers_all_input{};
    // Non-empty only for launch_action. It is carried to the TSF after the
    // composition has been cleared successfully.
    std::string action_target;
    // Non-empty only for `phrase`, where what the row shows and what the row
    // commits are deliberately different: a stored address is thirty characters
    // wide and would squeeze every other candidate on its row to nothing, so the
    // row shows a shortened form while this carries the text in full.
    //
    // A field of its own rather than another meaning for action_target. Those two
    // are the payloads of opposite actions -- one is handed to the shell, the
    // other typed into the document -- and a single field holding either would
    // make launching a stored address one mistake away.
    std::string commit_text;
};

}  // namespace piinput
