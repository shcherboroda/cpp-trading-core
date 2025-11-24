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

OrderId OrderBook::add_limit_order_with_id(OrderId id, Side side, Price price, Quantity qty) {
    // Сначала пытаемся исполнить входящий лимитный ордер
    // против противоположной стороны до его цены.
    Quantity remaining = match_incoming_limit(side, price, qty);

    // Если всё съели — ордер полностью исполнен, в книгу его не вешаем.
    if (remaining <= 0) {
        return id;
    }

    // Остаток вешаем в книгу как обычную лимитку
    auto& book = (side == Side::Buy) ? bids_ : asks_;

    auto level_it = book.find(price);
    if (level_it == book.end()) {
        level_it = book.emplace(price, Level{}).first;
    }

    Level& level = level_it->second;
    level.push_back(Order{ id, side, price, remaining });
    auto it = std::prev(level.end());

    index_[id] = OrderRef{ side, price, it };

    return id;
}


OrderId OrderBook::add_limit_order(Side side, Price price, Quantity qty) {
    OrderId id = next_id_++;
    return add_limit_order_with_id(id, side, price, qty);
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

            // записываем сделку
            result.trades.push_back(Trade{
                ord.id,   // maker (лимитный ордер из книги)
                side,     // сторона агрессора
                ord.price,
                trade_qty
            });

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

Quantity OrderBook::match_incoming_limit(Side side, Price price, Quantity qty) {
    // Матчим входящий лимитный ордер против противоположной стороны
    // до его лимитной цены (price).

    // Для Buy — берём asks_, для Sell — bids_
    auto& book = (side == Side::Buy) ? asks_ : bids_;

    while (qty > 0 && !book.empty()) {
        // Для Buy: лучшая цена — минимальный ask (begin()).
        // Для Sell: лучшая цена — максимальный bid (std::prev(end())).
        auto level_it = (side == Side::Buy) ? book.begin()
                                            : std::prev(book.end());
        Price level_price = level_it->first;

        // Проверяем, пересекается ли рынок с лимитной ценой:
        // - Buy готов покупать только до своей цены: best_ask <= price
        // - Sell готов продавать только до своей цены: best_bid >= price
        if (side == Side::Buy) {
            if (level_price > price) {
                break; // дальше уровни только дороже, не пересекаем
            }
        } else { // Sell
            if (level_price < price) {
                break; // дальше уровни только дешевле, не пересекаем
            }
        }

        Level& level = level_it->second;

        while (qty > 0 && !level.empty()) {
            Order& ord = level.front();

            Quantity trade_qty = std::min(qty, ord.qty);

            ord.qty -= trade_qty;
            qty     -= trade_qty;

            if (ord.qty == 0) {
                // ордер полностью исполнен — убираем из индекса и уровня
                index_.erase(ord.id);
                level.pop_front();
            }
        }

        if (level.empty()) {
            book.erase(level_it);
        }
    }

    // Возвращаем остаток входящего лимитного ордера,
    // который НЕ был исполнен против книги.
    return qty;
}


} // namespace trading
