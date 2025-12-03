#include "trading/order_book.hpp"

#include <algorithm> // std::remove_if, std::min
#include <cassert>
#include <limits>

namespace trading {

OrderBook::OrderBook()
{
    orders_.reserve(1024);
    free_indices_.reserve(1024);
}

bool OrderBook::empty() const noexcept
{
    return bids_.empty() && asks_.empty();
}

void OrderBook::clear() noexcept
{
    bids_.clear();
    asks_.clear();
    orders_.clear();
    free_indices_.clear();
    id_to_index_.clear();
    next_id_ = 1;
}

OrderBook::OrderIndex OrderBook::allocate_slot()
{
    if (!free_indices_.empty())
    {
        OrderIndex idx = free_indices_.back();
        free_indices_.pop_back();
        return idx;
    }

    OrderIndex idx = static_cast<OrderIndex>(orders_.size());
    orders_.push_back(Order{});
    return idx;
}

LevelInfo OrderBook::best_bid() const noexcept
{
    LevelInfo info;
    if (bids_.empty())
        return info;

    const auto& [price, level] = *bids_.begin(); // max price due to std::greater

    Quantity agg_qty = 0;
    for (OrderIndex idx : level.indices)
    {
        const Order& ord = orders_[idx];
        if (ord.active && ord.qty > 0)
            agg_qty += ord.qty;
    }

    if (agg_qty == 0)
        return info;

    info.valid = true;
    info.price = price;
    info.qty   = agg_qty;
    return info;
}

LevelInfo OrderBook::best_ask() const noexcept
{
    LevelInfo info;
    if (asks_.empty())
        return info;

    const auto& [price, level] = *asks_.begin(); // min price due to std::less

    Quantity agg_qty = 0;
    for (OrderIndex idx : level.indices)
    {
        const Order& ord = orders_[idx];
        if (ord.active && ord.qty > 0)
            agg_qty += ord.qty;
    }

    if (agg_qty == 0)
        return info;

    info.valid = true;
    info.price = price;
    info.qty   = agg_qty;
    return info;
}

OrderId OrderBook::add_limit_order(Side side, Price price, Quantity qty)
{
    if (qty <= 0)
        return 0;

    // Сначала агрессивная часть — матчим с противоположной стороной.
    qty = match_incoming_limit(side, price, qty);
    if (qty <= 0)
    {
        // Всё исполнилось как такер, в книгу ничего не кладём.
        return 0;
    }

    OrderIndex idx = allocate_slot();
    OrderId    id  = next_id_++;

    Order ord;
    ord.id     = id;
    ord.side   = side;
    ord.price  = price;
    ord.qty    = qty;
    ord.active = true;

    orders_[idx]     = ord;
    id_to_index_[id] = idx;

    if (side == Side::Buy)
    {
        auto& level = bids_[price];
        level.indices.push_back(idx);
    }
    else
    {
        auto& level = asks_[price];
        level.indices.push_back(idx);
    }

    return id;
}

OrderId OrderBook::add_limit_order_with_id(OrderId id, Side side, Price price, Quantity qty)
{
    if (qty <= 0)
        return id;

    // Агрессивная часть.
    qty = match_incoming_limit(side, price, qty);
    if (qty <= 0)
    {
        // Ордер полностью исполнился сразу.
        return id;
    }

    // На всякий случай: если такой id уже есть, помечаем старый ордер мёртвым
    // и возвращаем его слот в пул.
    auto existing = id_to_index_.find(id);
    if (existing != id_to_index_.end())
    {
        Order& old = orders_[existing->second];
        old.active = false;
        old.qty    = 0;
        free_indices_.push_back(existing->second);
        id_to_index_.erase(existing);
        // Индекс в level.indices не чистим — он будет вычищен при следующем матчe.
    }

    OrderIndex idx = allocate_slot();

    Order ord;
    ord.id     = id;
    ord.side   = side;
    ord.price  = price;
    ord.qty    = qty;
    ord.active = true;

    orders_[idx]     = ord;
    id_to_index_[id] = idx;

    if (side == Side::Buy)
    {
        auto& level = bids_[price];
        level.indices.push_back(idx);
    }
    else
    {
        auto& level = asks_[price];
        level.indices.push_back(idx);
    }

    return id;
}

bool OrderBook::cancel(OrderId id)
{
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end())
        return false;

    OrderIndex idx = it->second;
    Order&     ord = orders_[idx];

    if (!ord.active || ord.qty <= 0)
    {
        id_to_index_.erase(it);
        return false;
    }

    ord.active = false;
    ord.qty    = 0;
    free_indices_.push_back(idx);

    // Удаляем индекс из соответствующего уровня.
    if (ord.side == Side::Buy)
    {
        auto lvl_it = bids_.find(ord.price);
        if (lvl_it != bids_.end())
        {
            auto& v = lvl_it->second.indices;
            v.erase(std::remove(v.begin(), v.end(), idx), v.end());
            if (v.empty())
                bids_.erase(lvl_it);
        }
    }
    else
    {
        auto lvl_it = asks_.find(ord.price);
        if (lvl_it != asks_.end())
        {
            auto& v = lvl_it->second.indices;
            v.erase(std::remove(v.begin(), v.end(), idx), v.end());
            if (v.empty())
                asks_.erase(lvl_it);
        }
    }

    id_to_index_.erase(it);
    return true;
}

MatchResult OrderBook::execute_market_order(Side side, Quantity qty)
{
    MatchResult res;
    res.requested = qty;
    res.filled    = 0;
    res.remaining = qty;

    if (qty <= 0)
        return res;

    Quantity remaining = 0;
    if (side == Side::Buy)
    {
        // Buy-market бьёт по книге ask.
        remaining = match_on_book(
            asks_,
            qty,
            [](Price) { return true; } // всегда кроссим
        );
    }
    else
    {
        // Sell-market бьёт по книге bid.
        remaining = match_on_book(
            bids_,
            qty,
            [](Price) { return true; } // всегда кроссим
        );
    }

    res.filled    = qty - remaining;
    res.remaining = remaining;
    return res;
}

template <typename Book, typename PricePredicate>
Quantity OrderBook::match_on_book(Book& book, Quantity qty, PricePredicate&& should_cross)
{
    if (qty <= 0)
        return 0;

    while (qty > 0 && !book.empty())
    {
        // Для обеих книг begin() — лучший уровень (зависит от компаратора).
        auto  lvl_it      = book.begin();
        Price level_price = lvl_it->first;

        if (!should_cross(level_price))
            break;

        auto&  level_indices = lvl_it->second.indices;
        size_t write_pos     = 0;

        for (size_t i = 0; i < level_indices.size() && qty > 0; ++i)
        {
            OrderIndex idx = level_indices[i];
            Order&     ord = orders_[idx];

            if (!ord.active || ord.qty <= 0)
                continue;

            Quantity traded = std::min(qty, ord.qty);
            qty     -= traded;
            ord.qty -= traded;

            if (ord.qty == 0)
            {
                ord.active = false;
                free_indices_.push_back(idx);
                id_to_index_.erase(ord.id);
            }
            else
            {
                level_indices[write_pos++] = idx;
            }
        }

        level_indices.resize(write_pos);
        if (level_indices.empty())
        {
            book.erase(lvl_it);
        }
    }

    return qty;
}

Quantity OrderBook::match_incoming_limit(Side side, Price price, Quantity qty)
{
    if (qty <= 0)
        return 0;

    if (side == Side::Buy)
    {
        // Buy-лимит матчится против ask по ценам <= нашей.
        return match_on_book(
            asks_,
            qty,
            [price](Price top_price) { return top_price <= price; }
        );
    }
    else
    {
        // Sell-лимит матчится против bid по ценам >= нашей.
        return match_on_book(
            bids_,
            qty,
            [price](Price top_price) { return top_price >= price; }
        );
    }
}

} // namespace trading
