#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "trading/types.hpp"

namespace trading {

/**
 * In-memory limit order book for a single instrument.
 *
 * Design
 *  - Two price books:
 *      * bids_ : Price -> Level, ordered by std::greater (best bid at begin()).
 *      * asks_ : Price -> Level, ordered by std::less   (best ask at begin()).
 *  - Each Level stores indices into a flat vector<Order> orders_.
 *  - id_to_index_ maps external OrderId to the index in orders_.
 *  - free_indices_ stores reusable indices in orders_.
 *
 * All methods are NOT thread-safe; external synchronisation is required
 * if the book is shared between threads.
 */
class OrderBook
{
public:
    OrderBook();

    /// True if there are no active bids and asks.
    bool empty() const noexcept;

    /// Remove all orders and reset internal state.
    void clear() noexcept;

    /// Aggregate best bid (highest price) with total quantity at that level.
    LevelInfo best_bid() const noexcept;

    /// Aggregate best ask (lowest price) with total quantity at that level.
    LevelInfo best_ask() const noexcept;

    /// Create a new limit order, id is generated inside the book.
    /// If qty <= 0, returns 0 and does nothing.
    ///
    /// If the order is fully matched immediately (acts as taker),
    /// returns 0 and the order is not inserted into the book.
    OrderId add_limit_order(Side side, Price price, Quantity qty);

    /// Create a new limit order with a pre-defined id (for replay/benchmarks).
    /// If qty <= 0, simply returns id and does nothing.
    ///
    /// If the order is fully matched immediately, returns id and
    /// the order is not inserted into the book.
    OrderId add_limit_order_with_id(OrderId id, Side side, Price price, Quantity qty);

    /// Cancel order by id. Returns true if an active order was cancelled.
    bool cancel(OrderId id);

    /// Execute a market order against the book.
    /// For side == Buy it hits the best asks, for Sell — the best bids.
    MatchResult execute_market_order(Side side, Quantity qty);

private:
    struct Order
    {
        OrderId  id{0};
        Side     side{Side::Buy};
        Price    price{0};
        Quantity qty{0};
        bool     active{false};
    };

    using OrderIndex = std::uint32_t;

    struct Level
    {
        // Indices into orders_.
        std::vector<OrderIndex> indices;
    };

    using BidBook = std::map<Price, Level, std::greater<Price>>;
    using AskBook = std::map<Price, Level, std::less<Price>>;

    OrderIndex allocate_slot();

    /// Core matching routine: matches qty against book until either qty == 0
    /// or should_cross(level_price) returns false.
    template <typename Book, typename PricePredicate>
    Quantity match_on_book(Book& book, Quantity qty, PricePredicate&& should_cross);

    /// Match an incoming LIMIT order (taker part) against the opposite side.
    /// Returns remaining quantity that should rest as maker (0 if fully filled).
    Quantity match_incoming_limit(Side side, Price price, Quantity qty);

    BidBook bids_;
    AskBook asks_;

    std::vector<Order>      orders_;        // indexed by OrderIndex (0-based)
    std::vector<OrderIndex> free_indices_;  // free slots for reuse
    std::unordered_map<OrderId, OrderIndex> id_to_index_;

    OrderId next_id_{1};
};

} // namespace trading
