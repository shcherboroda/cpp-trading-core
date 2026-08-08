#pragma once

#include <vector>

#include "trading/types.hpp"

namespace trading {

// L2 book optimized for sorted snapshots; deltas use binary search in flat arrays.
class FlatMarketDataOrderBook {
public:
    void apply_snapshot(const std::vector<PriceLevel>& bids, const std::vector<PriceLevel>& asks);
    void apply_delta(const std::vector<PriceLevel>& bids, const std::vector<PriceLevel>& asks);

    bool best_bid(Price& price_out, Quantity& qty_out) const noexcept;
    bool best_ask(Price& price_out, Quantity& qty_out) const noexcept;

private:
    static void apply_side_delta(std::vector<PriceLevel>& side,
                                 const std::vector<PriceLevel>& updates,
                                 bool descending);

    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;
};

} // namespace trading
