#include "trading/flat_market_data_order_book.hpp"

#include <algorithm>

namespace trading {

void FlatMarketDataOrderBook::apply_snapshot(const std::vector<PriceLevel>& bids,
                                              const std::vector<PriceLevel>& asks)
{
    bids_.clear();
    asks_.clear();
    bids_.reserve(bids.size());
    asks_.reserve(asks.size());
    for (const auto& level : bids) {
        if (level.qty > 0) {
            bids_.push_back(level);
        }
    }
    for (const auto& level : asks) {
        if (level.qty > 0) {
            asks_.push_back(level);
        }
    }
}

void FlatMarketDataOrderBook::apply_side_delta(std::vector<PriceLevel>& side,
                                                const std::vector<PriceLevel>& updates,
                                                bool descending)
{
    for (const auto& update : updates) {
        const auto it = std::lower_bound(side.begin(), side.end(), update.price,
            [descending](const PriceLevel& level, Price price) {
                return descending ? level.price > price : level.price < price;
            });
        const bool found = it != side.end() && it->price == update.price;
        if (update.qty <= 0) {
            if (found) {
                side.erase(it);
            }
        } else if (found) {
            it->qty = update.qty;
        } else {
            side.insert(it, update);
        }
    }
}

void FlatMarketDataOrderBook::apply_delta(const std::vector<PriceLevel>& bids,
                                           const std::vector<PriceLevel>& asks)
{
    apply_side_delta(bids_, bids, true);
    apply_side_delta(asks_, asks, false);
}

bool FlatMarketDataOrderBook::best_bid(Price& price_out, Quantity& qty_out) const noexcept
{
    if (bids_.empty()) {
        return false;
    }
    price_out = bids_.front().price;
    qty_out = bids_.front().qty;
    return true;
}

bool FlatMarketDataOrderBook::best_ask(Price& price_out, Quantity& qty_out) const noexcept
{
    if (asks_.empty()) {
        return false;
    }
    price_out = asks_.front().price;
    qty_out = asks_.front().qty;
    return true;
}

} // namespace trading
