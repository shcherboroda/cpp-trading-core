#pragma once

#include <cstdint>

namespace trading {

using Price    = std::int64_t;   // e.g. price in ticks (1 = 0.01)
using Quantity = std::int64_t;
using OrderId  = std::uint64_t;

enum class Side {
    Buy,
    Sell
};

struct BestQuote {
    Price price{};
    Quantity qty{};
    bool valid{false};
};

struct MatchResult {
    Quantity requested{};   // запрошенный объём
    Quantity filled{};      // сколько реально исполнилось
    Quantity remaining{};   // остаток (requested - filled)
};

} // namespace trading
