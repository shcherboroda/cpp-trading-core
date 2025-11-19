#pragma once

#include "types.hpp"

#include <map>
#include <list>
#include <unordered_map>

namespace trading {

class OrderBook {
public:
    OrderBook() = default;
    ~OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    OrderBook(OrderBook&&) noexcept = default;
    OrderBook& operator=(OrderBook&&) noexcept = default;

    OrderId add_limit_order(Side side, Price price, Quantity qty);
    bool cancel(OrderId id);
    MatchResult execute_market_order(Side side, Quantity qty);

    BestQuote best_bid() const;
    BestQuote best_ask() const;

    bool empty() const;

private:
    struct Order {
        OrderId id{};
        Side side{};
        Price price{};
        Quantity qty{};
    };

    using Level = std::list<Order>;
    using Levels = std::map<Price, Level>;

    struct OrderRef {
        Side side;
        Price price;
        Level::iterator it;
    };

    Levels bids_;
    Levels asks_;
    std::unordered_map<OrderId, OrderRef> index_;
    OrderId next_id_{1};
};

} // namespace trading
