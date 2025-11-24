#pragma once

#include <cstdint>
#include <vector>

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

struct Trade {
    OrderId maker_id;   // id лимитного (пассивного) ордера
    Side    taker_side; // сторона агрессивного ордера: Buy или Sell
    Price   price;
    Quantity qty;
};

struct MatchResult {
    Quantity requested{0};
    Quantity filled{0};
    Quantity remaining{0};
    std::vector<Trade> trades; // новый компонент
};

} // namespace trading
