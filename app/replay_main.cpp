#include "trading/order_book.hpp"
#include "trading/types.hpp"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "trading/event.hpp"

using namespace trading;

// --------- parsing helpers ---------

static std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

static std::string to_upper(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

static std::optional<Side> parse_side(const std::string& token) {
    auto up = to_upper(token);
    if (up == "BUY" || up == "B")  return Side::Buy;
    if (up == "SELL" || up == "S") return Side::Sell;
    return std::nullopt;
}

static bool is_comment_or_empty(const std::string& line) {
    auto t = trim(line);
    return t.empty() || (!t.empty() && t[0] == '#');
}

static std::optional<Event> parse_line(const std::string& line) {
    if (is_comment_or_empty(line)) {
        return std::nullopt;
    }

    std::stringstream ss(line);
    std::string token;

    auto next_token = [&]() -> std::optional<std::string> {
        if (!std::getline(ss, token, ',')) {
            return std::nullopt;
        }
        return trim(token);
    };

    auto type_tok_opt = next_token();
    if (!type_tok_opt) return std::nullopt;
    auto type_str = to_upper(*type_tok_opt);

    Event ev;

    try {
        if (type_str == "ADD") {
            // Формат: ADD,side,price,qty,id
            auto side_tok_opt  = next_token();
            auto price_tok_opt = next_token();
            auto qty_tok_opt   = next_token();
            auto id_tok_opt    = next_token();

            if (!side_tok_opt || !price_tok_opt || !qty_tok_opt || !id_tok_opt) {
                return std::nullopt;
            }

            auto side_opt = parse_side(*side_tok_opt);
            if (!side_opt) return std::nullopt;

            ev.type  = EventType::Add;
            ev.side  = *side_opt;
            ev.price = static_cast<Price>(std::stoll(*price_tok_opt));
            ev.qty   = static_cast<Quantity>(std::stoll(*qty_tok_opt));
            ev.id    = static_cast<OrderId>(std::stoull(*id_tok_opt));

            return ev;
        } else if (type_str == "MKT" || type_str == "MARKET") {
            // Формат: MKT,side,qty
            auto side_tok_opt = next_token();
            auto qty_tok_opt  = next_token();
            if (!side_tok_opt || !qty_tok_opt) {
                return std::nullopt;
            }

            auto side_opt = parse_side(*side_tok_opt);
            if (!side_opt) return std::nullopt;

            ev.type = EventType::Market;
            ev.side = *side_opt;
            ev.qty  = static_cast<Quantity>(std::stoll(*qty_tok_opt));
            // ev.id не используется для MKT
            return ev;
        } else if (type_str == "CANCEL" || type_str == "CXL") {
            // Формат: CANCEL,id
            auto id_tok_opt = next_token();
            if (!id_tok_opt) {
                return std::nullopt;
            }

            ev.type = EventType::Cancel;
            ev.id   = static_cast<OrderId>(std::stoull(*id_tok_opt));
            return ev;
        } else {
            // неизвестный тип события
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// --------- stats struct ---------

struct ReplayStats {
    std::size_t add_count       = 0;
    std::size_t mkt_count       = 0;
    std::size_t cancel_count    = 0;

    Quantity total_added_buy    = 0;
    Quantity total_added_sell   = 0;

    Quantity total_mkt_req_buy  = 0; // requested by aggressive BUYs
    Quantity total_mkt_req_sell = 0; // requested by aggressive SELLs

    Quantity total_mkt_fill_buy  = 0; // filled for aggressive BUYs
    Quantity total_mkt_fill_sell = 0; // filled for aggressive SELLs

    std::size_t mkt_full_fill_count    = 0;
    std::size_t mkt_partial_fill_count = 0;
    std::size_t mkt_zero_fill_count    = 0;

    std::size_t cancel_success = 0;
    std::size_t cancel_fail    = 0;

    // best bid/ask stats
    bool  seen_bid = false;
    bool  seen_ask = false;
    Price min_best_bid = std::numeric_limits<Price>::max();
    Price max_best_bid = std::numeric_limits<Price>::lowest();
    Price min_best_ask = std::numeric_limits<Price>::max();
    Price max_best_ask = std::numeric_limits<Price>::lowest();

    Quantity max_best_bid_qty = 0;
    Quantity max_best_ask_qty = 0;

    // spread stats (ask - bid)
    double spread_sum = 0.0;
    double spread_min = std::numeric_limits<double>::infinity();
    double spread_max = 0.0;
    std::size_t spread_count = 0;

    // === НОВОЕ: денежные метрики агрессивных сделок ===
    double traded_notional_buy  = 0.0; // сумма price*qty по сделкам, где агрессор BUY
    double traded_notional_sell = 0.0; // аналогично для SELL
};

static void update_book_stats(const OrderBook& book, ReplayStats& stats) {
    auto bb = book.best_bid();
    auto ba = book.best_ask();

    if (bb.valid) {
        stats.seen_bid = true;
        if (bb.price < stats.min_best_bid) stats.min_best_bid = bb.price;
        if (bb.price > stats.max_best_bid) stats.max_best_bid = bb.price;
        if (bb.qty   > stats.max_best_bid_qty) stats.max_best_bid_qty = bb.qty;
    }

    if (ba.valid) {
        stats.seen_ask = true;
        if (ba.price < stats.min_best_ask) stats.min_best_ask = ba.price;
        if (ba.price > stats.max_best_ask) stats.max_best_ask = ba.price;
        if (ba.qty   > stats.max_best_ask_qty) stats.max_best_ask_qty = ba.qty;
    }

    if (bb.valid && ba.valid) {
        double spread = static_cast<double>(ba.price - bb.price);
        stats.spread_sum += spread;
        if (spread < stats.spread_min) stats.spread_min = spread;
        if (spread > stats.spread_max) stats.spread_max = spread;
        ++stats.spread_count;
    }
}

static void print_stats(const ReplayStats& st, const OrderBook& book) {
    auto bb = book.best_bid();
    auto ba = book.best_ask();

    std::cout << "=== Replay summary ===\n\n";

    std::cout << "Events:\n";
    std::cout << "  ADD    : " << st.add_count    << "\n";
    std::cout << "  MARKET : " << st.mkt_count    << "\n";
    std::cout << "  CANCEL : " << st.cancel_count << "\n\n";

    std::cout << "Added volume:\n";
    std::cout << "  Buy  : " << st.total_added_buy  << "\n";
    std::cout << "  Sell : " << st.total_added_sell << "\n\n";

    std::cout << "Aggressive (market) volume:\n";
    std::cout << "  Buy requested : " << st.total_mkt_req_buy
              << ", filled: "        << st.total_mkt_fill_buy;
    if (st.total_mkt_req_buy > 0) {
        double ratio = static_cast<double>(st.total_mkt_fill_buy)
                     / static_cast<double>(st.total_mkt_req_buy) * 100.0;
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << ratio << "%)\n";
    } else {
        std::cout << " (n/a)\n";
    }

    std::cout << "  Sell requested: " << st.total_mkt_req_sell
              << ", filled: "         << st.total_mkt_fill_sell;
    if (st.total_mkt_req_sell > 0) {
        double ratio = static_cast<double>(st.total_mkt_fill_sell)
                     / static_cast<double>(st.total_mkt_req_sell) * 100.0;
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << ratio << "%)\n";
    } else {
        std::cout << " (n/a)\n";
    }
    std::cout << "\n";

    std::cout << "\nAggressive VWAP (based on trades):\n";

    if (st.total_mkt_fill_buy > 0) {
        double vwap_buy = st.traded_notional_buy
                        / static_cast<double>(st.total_mkt_fill_buy);
        std::cout << "  Buy  VWAP: " << std::fixed << std::setprecision(2)
                  << vwap_buy << "\n";
    } else {
        std::cout << "  Buy  VWAP: n/a\n";
    }

    if (st.total_mkt_fill_sell > 0) {
        double vwap_sell = st.traded_notional_sell
                         / static_cast<double>(st.total_mkt_fill_sell);
        std::cout << "  Sell VWAP: " << std::fixed << std::setprecision(2)
                  << vwap_sell << "\n";
    } else {
        std::cout << "  Sell VWAP: n/a\n";
    }
    std::cout << "\n";

    std::cout << "Market order outcomes:\n";
    std::cout << "  full fills   : " << st.mkt_full_fill_count    << "\n";
    std::cout << "  partial fills: " << st.mkt_partial_fill_count << "\n";
    std::cout << "  zero fills   : " << st.mkt_zero_fill_count    << "\n\n";

    std::cout << "Cancel stats:\n";
    std::cout << "  success: " << st.cancel_success << "\n";
    std::cout << "  fail   : " << st.cancel_fail    << "\n\n";

    std::cout << "Order book stats (over replay):\n";
    if (st.seen_bid) {
        std::cout << "  Best bid price range : [" << st.min_best_bid
                  << ", " << st.max_best_bid << "]\n";
        std::cout << "  Max best bid depth   : " << st.max_best_bid_qty << "\n";
    } else {
        std::cout << "  No best bid observed\n";
    }

    if (st.seen_ask) {
        std::cout << "  Best ask price range : [" << st.min_best_ask
                  << ", " << st.max_best_ask << "]\n";
        std::cout << "  Max best ask depth   : " << st.max_best_ask_qty << "\n";
    } else {
        std::cout << "  No best ask observed\n";
    }

    std::cout << "\n";

    std::cout << "Spread stats (ask - bid):\n";
    if (st.spread_count > 0) {
        double avg_spread = st.spread_sum / static_cast<double>(st.spread_count);
        std::cout << "  mean : " << std::fixed << std::setprecision(2)
                  << avg_spread << "\n";
        std::cout << "  min  : " << st.spread_min << "\n";
        std::cout << "  max  : " << st.spread_max << "\n";
        std::cout << "  count: " << st.spread_count << "\n";
    } else {
        std::cout << "  not enough data (no simultaneous best bid & ask)\n";
    }

    std::cout << "\nFinal best bid: ";
    if (bb.valid) {
        std::cout << bb.price << " x " << bb.qty << "\n";
    } else {
        std::cout << "none\n";
    }

    std::cout << "Final best ask: ";
    if (ba.valid) {
        std::cout << ba.price << " x " << ba.qty << "\n";
    } else {
        std::cout << "none\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: trading_replay <events_file>\n";
        return 1;
    }

    const char* path = argv[1];
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    OrderBook   book;
    ReplayStats stats;

    std::string line;
    std::size_t line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        auto ev_opt = parse_line(line);
        if (!ev_opt) {
            // можно раскомментировать для отладки
            // std::cerr << "Skipping line " << line_no << ": " << line << "\n";
            continue;
        }

        const Event& ev = *ev_opt;

        switch (ev.type) {
        case EventType::Add: {
            ++stats.add_count;
            if (ev.side == Side::Buy) {
                stats.total_added_buy += ev.qty;
            } else {
                stats.total_added_sell += ev.qty;
            }
            book.add_limit_order_with_id(ev.id, ev.side, ev.price, ev.qty);
            break;
        }
        case EventType::Market: {
            ++stats.mkt_count;

            if (ev.side == Side::Buy) {
                stats.total_mkt_req_buy += ev.qty;
            } else {
                stats.total_mkt_req_sell += ev.qty;
            }

            MatchResult mr = book.execute_market_order(ev.side, ev.qty);

            if (mr.filled == 0) {
                ++stats.mkt_zero_fill_count;
            } else if (mr.remaining == 0) {
                ++stats.mkt_full_fill_count;
            } else {
                ++stats.mkt_partial_fill_count;
            }

            if (ev.side == Side::Buy) {
                stats.total_mkt_fill_buy += mr.filled;
            } else {
                stats.total_mkt_fill_sell += mr.filled;
            }

            for (const auto& tr : mr.trades) {
                double notional = static_cast<double>(tr.price)
                                * static_cast<double>(tr.qty);

                if (tr.taker_side == Side::Buy) {
                    stats.traded_notional_buy += notional;
                } else {
                    stats.traded_notional_sell += notional;
                }
            }
            break;
        }
        case EventType::Cancel: {
            ++stats.cancel_count;
            bool ok = book.cancel(ev.id);
            if (ok) {
                ++stats.cancel_success;
            } else {
                ++stats.cancel_fail;
            }
            break;
        }
        default:
            // не должно быть других типов
            break;
        }

        // обновляем статистику по книге после каждого события
        update_book_stats(book, stats);
    }

    print_stats(stats, book);
    return 0;
}
