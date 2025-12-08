#include "trading/market_data_order_book.hpp"

namespace trading {

void MarketDataOrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
}

void MarketDataOrderBook::apply_snapshot(const std::vector<PriceLevel>& bids,
                                         const std::vector<PriceLevel>& asks) {
    clear();

    // Insert bids (highest price first due to std::greater in BidBook).
    for (const auto& lvl : bids) {
        if (lvl.qty <= 0) {
            continue;
        }
        bids_[lvl.price] = lvl.qty;
    }

    // Insert asks (lowest price first due to std::less in AskBook).
    for (const auto& lvl : asks) {
        if (lvl.qty <= 0) {
            continue;
        }
        asks_[lvl.price] = lvl.qty;
    }
}

void MarketDataOrderBook::apply_delta(const std::vector<PriceLevel>& bids,
                                      const std::vector<PriceLevel>& asks) {
    // Update bids
    for (const auto& upd : bids) {
        if (upd.qty <= 0) {
            // Remove level if present.
            auto it = bids_.find(upd.price);
            if (it != bids_.end()) {
                bids_.erase(it);
            }
        } else {
            // Insert or overwrite aggregated size.
            bids_[upd.price] = upd.qty;
        }
    }

    // Update asks
    for (const auto& upd : asks) {
        if (upd.qty <= 0) {
            auto it = asks_.find(upd.price);
            if (it != asks_.end()) {
                asks_.erase(it);
            }
        } else {
            asks_[upd.price] = upd.qty;
        }
    }
}

bool MarketDataOrderBook::best_bid(Price& price_out, Quantity& qty_out) const noexcept {
    if (bids_.empty()) {
        return false;
    }
    const auto& [price, qty] = *bids_.begin();
    price_out = price;
    qty_out   = qty;
    return true;
}

bool MarketDataOrderBook::best_ask(Price& price_out, Quantity& qty_out) const noexcept {
    if (asks_.empty()) {
        return false;
    }
    const auto& [price, qty] = *asks_.begin();
    price_out = price;
    qty_out   = qty;
    return true;
}

} // namespace trading
