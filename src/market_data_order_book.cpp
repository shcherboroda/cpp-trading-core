#include "trading/market_data_order_book.hpp"

#ifndef MARKET_DATA_BOOK_HINTED_SNAPSHOT
#define MARKET_DATA_BOOK_HINTED_SNAPSHOT 0
#endif

#ifndef MARKET_DATA_BOOK_REUSE_SORTED_SNAPSHOT
#define MARKET_DATA_BOOK_REUSE_SORTED_SNAPSHOT 0
#endif

namespace {

template <typename Book>
bool is_sorted_unique_snapshot(const std::vector<trading::PriceLevel>& levels)
{
    const auto compare = typename Book::key_compare{};
    bool have_previous = false;
    trading::Price previous{};
    for (const auto& level : levels) {
        if (level.qty <= 0) {
            continue;
        }
        if (have_previous && !compare(previous, level.price)) {
            return false;
        }
        previous = level.price;
        have_previous = true;
    }
    return true;
}

template <typename Book>
void reconcile_sorted_snapshot(Book& book, const std::vector<trading::PriceLevel>& levels)
{
    const auto compare = typename Book::key_compare{};
    auto current = book.begin();
    for (const auto& level : levels) {
        if (level.qty <= 0) {
            continue;
        }
        while (current != book.end() && compare(current->first, level.price)) {
            current = book.erase(current);
        }
        if (current != book.end() && current->first == level.price) {
            current->second = level.qty;
            ++current;
        } else {
            current = book.insert(current, {level.price, level.qty});
            ++current;
        }
    }
    book.erase(current, book.end());
}

} // namespace

namespace trading {

void MarketDataOrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
}

void MarketDataOrderBook::apply_snapshot(const std::vector<PriceLevel>& bids,
                                         const std::vector<PriceLevel>& asks) {
#if MARKET_DATA_BOOK_REUSE_SORTED_SNAPSHOT
    if (is_sorted_unique_snapshot<BidBook>(bids) && is_sorted_unique_snapshot<AskBook>(asks)) {
        reconcile_sorted_snapshot(bids_, bids);
        reconcile_sorted_snapshot(asks_, asks);
        return;
    }
#endif
    clear();

    // Insert bids (highest price first due to std::greater in BidBook).
#if MARKET_DATA_BOOK_HINTED_SNAPSHOT
    auto bid_hint = bids_.end();
#endif
    for (const auto& lvl : bids) {
        if (lvl.qty <= 0) {
            continue;
        }
#if MARKET_DATA_BOOK_HINTED_SNAPSHOT
        bid_hint = bids_.insert_or_assign(bid_hint, lvl.price, lvl.qty);
#else
        bids_[lvl.price] = lvl.qty;
#endif
    }

    // Insert asks (lowest price first due to std::less in AskBook).
#if MARKET_DATA_BOOK_HINTED_SNAPSHOT
    auto ask_hint = asks_.end();
#endif
    for (const auto& lvl : asks) {
        if (lvl.qty <= 0) {
            continue;
        }
#if MARKET_DATA_BOOK_HINTED_SNAPSHOT
        ask_hint = asks_.insert_or_assign(ask_hint, lvl.price, lvl.qty);
#else
        asks_[lvl.price] = lvl.qty;
#endif
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
