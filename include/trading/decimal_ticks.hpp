#pragma once

#include <cstdint>
#include <string_view>

namespace trading {

inline bool parse_decimal_ticks(std::string_view text, std::int64_t scale, std::int64_t& out) noexcept {
    if (text.empty() || scale <= 0) return false;
    bool negative = false; std::size_t i = 0;
    if (text[i] == '-') { negative = true; ++i; }
    if (i == text.size()) return false;
    std::int64_t whole = 0, fraction = 0, fraction_scale = 1;
    bool saw_digit = false;
    for (; i < text.size() && text[i] != '.'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        saw_digit = true;
        whole = whole * 10 + (text[i] - '0');
    }
    if (i < text.size()) ++i;
    for (; i < text.size() && fraction_scale < scale; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        saw_digit = true;
        fraction = fraction * 10 + (text[i] - '0'); fraction_scale *= 10;
    }
    bool round_up = false;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        saw_digit = true;
        if (!round_up && text[i] >= '5') round_up = true;
    }
    if (!saw_digit) return false;
    while (fraction_scale < scale) { fraction *= 10; fraction_scale *= 10; }
    out = whole * scale + fraction;
    if (round_up) ++out;
    if (negative) out = -out;
    return true;
}

} // namespace trading
