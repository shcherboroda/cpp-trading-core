#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace trading {

using Price    = std::int64_t;   // e.g. price in ticks (1 = 0.01)
using Quantity = std::int64_t;
using OrderId  = std::uint64_t;

inline constexpr std::int64_t PRICE_SCALE = 100;   // 1 = $0.01
inline constexpr std::int64_t QTY_SCALE   = 1000;  // 1 = 0.001 units

inline Price encode_price(double px) noexcept {
    return static_cast<Price>(std::llround(px * PRICE_SCALE));
}

inline Quantity encode_qty(double q) noexcept {
    return static_cast<Quantity>(std::llround(q * QTY_SCALE));
}

inline double decode_price(Price p) noexcept {
    return static_cast<double>(p) / PRICE_SCALE;
}

inline double decode_qty(Quantity q) noexcept {
    return static_cast<double>(q) / QTY_SCALE;
}

enum class Side {
    Buy,
    Sell
};

/**
 * @brief Simple Level-2 market data price level.
 *
 * This represents aggregated quantity at a given price on one side of the book.
 */
struct PriceLevel {
    Price    price{0};
    Quantity qty{0};
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

struct LevelInfo {
    bool     valid{false};
    Price    price{0};
    Quantity qty{0};
};

} // namespace trading
