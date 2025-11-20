#include "trading/order_book.hpp"
#include "trading/types.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>

using namespace trading;

enum class EventType {
    Add,
    Market,
    Cancel
};

struct Event {
    EventType type{};
    Side side{};
    Price price{};
    Quantity qty{};
    OrderId cancel_id{};
};

std::optional<EventType> parseEventType(const std::string& token) {
    if (token == "ADD") {
        return EventType::Add;
    } else if (token == "MKT") {
        return EventType::Market;
    } else if (token == "CANCEL") {
        return EventType::Cancel;
    }
    return std::nullopt;
}

std::optional<Side> parseSide(const std::string& token) {
    if (token == "BUY") {
        return Side::Buy;
    } else if (token == "SELL") {
        return Side::Sell;
    }
    return std::nullopt;
}

std::optional<Price> parsePrice(const std::string& token) {
    try {
        const auto v = static_cast<Price>(std::stoll(token));
        if (v < 0) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

std::optional<Quantity> parseQuantity(const std::string& token) {
    try {
        const auto v = static_cast<Quantity>(std::stoll(token));
        if (v < 0) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

std::optional<Event> parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#') {
        return std::nullopt;
    }

    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string item;

    while (std::getline(ss, item, ',')) {
        if (!item.empty() && item.back() == '\r')
            item.pop_back(); // in case of Windows \r\n
        tokens.push_back(item);
    }

    if (tokens.empty()) {
        return std::nullopt;
    }
    if (tokens.size() < 2) {
        return std::nullopt;
    }
    const auto& type = parseEventType(tokens[0]);
    if (not type.has_value()) {
        return std::nullopt;
    }

    switch (type.value()) {
        case EventType::Add: {
            if (tokens.size() != 4) {
                return std::nullopt;
            }

            Event ev;
            ev.type = EventType::Add;
            const auto& side = parseSide(tokens[1]);
            if (not side.has_value()) {
                return std::nullopt;
            }
            ev.side = side.value();

            const auto& price = parsePrice(tokens[2]);
            if (not price.has_value()) {
                return std::nullopt;
            }
            ev.price = price.value();

            const auto& qty = parseQuantity(tokens[3]);
            if (not qty.has_value()) {
                return std::nullopt;
            }
            ev.qty = qty.value();

            return ev;
        }
        case EventType::Market: {
            if (tokens.size() != 3) {
                return std::nullopt;
            }

            Event ev;
            ev.type = EventType::Market;
            const auto& side = parseSide(tokens[1]);
            if (not side.has_value()) {
                return std::nullopt;
            }
            ev.side = side.value();

            const auto& qty = parseQuantity(tokens[2]);
            if (not qty.has_value()) {
                return std::nullopt;
            }
            ev.qty = qty.value();

            return ev;
        }
        case EventType::Cancel: {
            if (tokens.size() != 2) {
                return std::nullopt;
            }
            Event ev;
            ev.type = EventType::Cancel;
            try {
                ev.cancel_id = static_cast<OrderId>(std::stoull(tokens[1]));
            } catch (const std::exception& e) {
                return std::nullopt;
            }

            return ev;
        }
        default: {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: trading_replay <events_file>\n";
        return 1;
    }

    const std::string path = argv[1];
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open file: " << path << "\n";
        return 1;
    }

    OrderBook book;

    std::size_t add_count    = 0;
    std::size_t mkt_count    = 0;
    std::size_t cancel_count = 0;
    Quantity    total_filled = 0;

    std::string line;
    while (std::getline(in, line)) {
        auto ev_opt = parse_line(line);
        if (!ev_opt) {
            continue; // можно логировать "skipped invalid line"
        }
        const Event& ev = *ev_opt;

        switch (ev.type) {
        case EventType::Add: {
            book.add_limit_order(ev.side, ev.price, ev.qty);
            ++add_count;
            break;
        }
        case EventType::Market: {
            auto res = book.execute_market_order(ev.side, ev.qty);
            total_filled += res.filled;
            ++mkt_count;
            break;
        }
        case EventType::Cancel: {
            book.cancel(ev.cancel_id);
            ++cancel_count;
            break;
        }
        }
    }

    auto bb = book.best_bid();
    auto ba = book.best_ask();

    std::cout << "=== Replay summary ===\n";
    std::cout << "ADD events:    " << add_count    << "\n";
    std::cout << "MKT events:    " << mkt_count    << "\n";
    std::cout << "CANCEL events: " << cancel_count << "\n";
    std::cout << "Total filled:  " << total_filled << "\n\n";

    std::cout << "Final best bid: ";
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

    return 0;
}
