#pragma once

#include <optional>
#include <string>

namespace piinput {

enum class PunctuationMode {
    chinese,
    english,
    programmer,
};

enum class PunctuationBracketStyle {
    sogou,
    wechat,
};

class PunctuationTransformer final {
public:
    [[nodiscard]] std::string transform(
        char key,
        PunctuationMode mode,
        bool shift,
        PunctuationBracketStyle bracket_style = PunctuationBracketStyle::sogou) const;
    void reset_quotes() noexcept;

private:
    mutable bool next_double_quote_open_{true};
    mutable bool next_single_quote_open_{true};
};

}  // namespace piinput
