#pragma once

#include <optional>
#include <string>

namespace liteime {

enum class PunctuationMode {
    chinese,
    english,
    programmer,
};

class PunctuationTransformer final {
public:
    [[nodiscard]] std::string transform(char key, PunctuationMode mode, bool shift) const;
    void reset_quotes() noexcept;

private:
    mutable bool next_double_quote_open_{true};
    mutable bool next_single_quote_open_{true};
};

}  // namespace liteime
