#pragma once

#include <map>
#include <vector>

#include "trading/types.hpp"

namespace trading {

/**
 * @brief Lightweight order book for market data (Level-2 snapshots + deltas).
 *
 * This class is intentionally simpler than the matching engine OrderBook:
 * - it stores only aggregated quantities per price level;
 * - it does NOT track individual orders or order IDs;
 * - it is designed to be fed by exchange snapshots and deltas (price/size).
 *
 * Use this for:
 *  - building a local copy of the exchange order book
 *    (e.g. Bybit REST snapshot + WebSocket deltas),
 *  - providing fast best bid/ask and depth for strategies.
 *
 * Do NOT use this for:
 *  - simulating your own orders,
 *  - tracking fills / cancellations by order ID.
 */
class MarketDataOrderBook {
public:
    using BidBook = std::map<Price, Quantity, std::greater<Price>>;
    using AskBook = std::map<Price, Quantity, std::less<Price>>;

    MarketDataOrderBook() = default;

    /// Remove all levels from both sides.
    void clear() noexcept;

    /**
     * @brief Build the book from a full snapshot.
     *
     * Existing levels are discarded. Snapshot is assumed to be non-crossing
     * (best bid < best ask) and already aggregated by price.
     */
    void apply_snapshot(const std::vector<PriceLevel>& bids,
                        const std::vector<PriceLevel>& asks);

    /**
     * @brief Apply incremental updates to the current book state.
     *
     * For each update:
     *  - qty == 0: remove the level at this price (if present),
     *  - qty > 0 : set/overwrite the level to this aggregated size.
     */
    void apply_delta(const std::vector<PriceLevel>& bids,
                     const std::vector<PriceLevel>& asks);

    /// Returns false if there is no bid side.
    bool best_bid(Price& price_out, Quantity& qty_out) const noexcept;

    /// Returns false if there is no ask side.
    bool best_ask(Price& price_out, Quantity& qty_out) const noexcept;

    const BidBook& bids() const noexcept { return bids_; }
    const AskBook& asks() const noexcept { return asks_; }

    bool empty() const noexcept { return bids_.empty() && asks_.empty(); }

private:
    BidBook bids_;
    AskBook asks_;
};

} // namespace trading
