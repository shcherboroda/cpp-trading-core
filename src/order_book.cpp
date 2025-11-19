#include "trading/order_book.hpp"

namespace trading {

bool OrderBook::empty() const {
    return bids_.empty() && asks_.empty();
}

BestQuote OrderBook::best_bid() const {
    BestQuote q;
    if (bids_.empty())
        return q;

    // max price = последний элемент в map
    auto it = std::prev(bids_.end());   // либо auto it = std::next(bids_.rbegin()).base();
    q.price = it->first;

    Quantity total = 0;
    for (const auto& ord : it->second)
        total += ord.qty;

    q.qty   = total;
    q.valid = true;
    return q;
}

BestQuote OrderBook::best_ask() const {
    BestQuote q;
    if (asks_.empty())
        return q;

    // min price = первый элемент
    auto it = asks_.begin();
    q.price = it->first;

    Quantity total = 0;
    for (const auto& ord : it->second)
        total += ord.qty;

    q.qty   = total;
    q.valid = true;
    return q;
}

OrderId OrderBook::add_limit_order(Side side, Price price, Quantity qty) {
    OrderId id = next_id_++;

    auto& book = (side == Side::Buy) ? bids_ : asks_;
    auto level_it = book.find(price);
    if (level_it == book.end()) {
        level_it = book.emplace(price, Level{}).first;
    }

    Level& level = level_it->second;
    level.push_back(Order{ id, side, price, qty });
    auto it = std::prev(level.end());

    index_[id] = OrderRef{ side, price, it };
    return id;
}

bool OrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end())
        return false;

    const auto& ref = it->second;
    auto& book = (ref.side == Side::Buy) ? bids_ : asks_;
    auto level_it = book.find(ref.price);
    if (level_it == book.end()) {
        // Inconsistent state: order index says there is a level, but map doesn't have it.
        // For now, just return false, but this should be unreachable in normal flows.
        return false;
    }

    Level& level = level_it->second;
    level.erase(ref.it);
    if (level.empty())
        book.erase(level_it);

    index_.erase(it);
    return true;
}

MatchResult OrderBook::execute_market_order(Side side, Quantity qty) {
    auto& book = (side == Side::Buy) ? asks_ : bids_;
    MatchResult result;
    result.requested = qty;
    if (book.empty())
    {
        result.filled    = 0;
        result.remaining = qty;
        return result;
    }
    while (qty > 0 && !book.empty()) {
        auto level_it = (side == Side::Buy) ? book.begin() : std::prev(book.end());
        Level& level = level_it->second;

        while (qty > 0 && !level.empty()) {
            Order& ord = level.front();
            Quantity trade_qty = std::min(qty, ord.qty);

            ord.qty -= trade_qty;
            qty -= trade_qty;
            result.filled += trade_qty;

            if (ord.qty == 0) {
                index_.erase(ord.id);
                level.pop_front();
            }
        }

        if (level.empty()) {
            book.erase(level_it);
        }
    }
    result.remaining = result.requested - result.filled;
    
    return result;
}

// execute_market_order, best_bid, best_ask, empty — по аналогии

} // namespace trading
