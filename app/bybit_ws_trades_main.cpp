// app/bybit_ws_trades_main.cpp
#include "exchange/bybit_public_ws.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

struct PublicTrade {
    std::string  symbol;
    double       price = 0.0;
    double       qty   = 0.0;
    std::int64_t ts_ms = 0;
    bool         is_buy = false; // true = buy, false = sell
};

struct TradeStats {
    std::size_t count = 0;
    double buy_volume = 0.0;
    double sell_volume = 0.0;
    double min_price = std::numeric_limits<double>::infinity();
    double max_price = 0.0;
    double last_price = 0.0;

    void update(const PublicTrade& t) {
        ++count;
        last_price = t.price;
        if (t.price < min_price) min_price = t.price;
        if (t.price > max_price) max_price = t.price;
        if (t.is_buy) buy_volume += t.qty;
        else          sell_volume += t.qty;
    }
};

void handle_public_trade_message(const json& msg,
                                 const std::function<void(const PublicTrade&)>& on_trade)
{
    // Ожидаем, что data — это массив трейдов.
    if (!msg.contains("data")) {
        return;
    }

    const auto& data = msg.at("data");

    // Bybit v5 publicTrade возвращает массив объектов.
    if (!data.is_array()) {
        return;
    }

    for (const auto& tr : data) {
        PublicTrade t;

        // symbol: либо в самом объекте tr["s"], либо fallback
        t.symbol = tr.value("s", std::string{});

        // price
        std::string p_str = tr.value("p", std::string{});
        if (!p_str.empty()) {
            try {
                t.price = std::stod(p_str);
            } catch (...) {
                t.price = 0.0;
            }
        }

        // qty: иногда "q", иногда "v" — подстраховываемся
        std::string q_str;
        if (tr.contains("q")) {
            q_str = tr.at("q").get<std::string>();
        } else if (tr.contains("v")) {
            q_str = tr.at("v").get<std::string>();
        } else {
            q_str = "0";
        }

        if (!q_str.empty()) {
            try {
                t.qty = std::stod(q_str);
            } catch (...) {
                t.qty = 0.0;
            }
        }

        // timestamp
        t.ts_ms = tr.value("T", 0LL);

        // side: Bybit обычно даёт m = isBuyerMaker
        // m == true → maker is seller → агрессивная сторона buy
        bool m = tr.value("m", false);
        t.is_buy = !m;

        on_trade(t);
    }
}

int main(int argc, char** argv) {
    std::string symbol       = "BTCUSDT";
    int max_messages         = 50;  // по умолчанию немного, чтобы не заспамить
    bool print_raw_non_trade = false;

    if (argc >= 2) {
        symbol = argv[1];
    }
    if (argc >= 3) {
        max_messages = std::atoi(argv[2]);
    }

    const std::string channel = "publicTrade." + symbol;
    std::vector<std::string> channels{channel};

    exchange::BybitPublicWs client;

    std::cout << "Connecting to Bybit WS public trades for " << symbol
              << ", max_messages=" << max_messages << "...\n";

    TradeStats stats;

    auto on_message = [&](const json& msg) {
        // 1. Игнорируем служебные сообщения (success/subscribe и т.п.)
        if (msg.contains("success") && msg.contains("op")) {
            if (print_raw_non_trade) {
                std::cout << "[sub-ack] " << msg.dump() << "\n";
            }
            return;
        }

        // 2. Интересуют только publicTrade.*
        const std::string topic = msg.value("topic", "");
        if (topic.rfind("publicTrade.", 0) != 0) {
            if (print_raw_non_trade) {
                std::cout << "[non-trade] " << msg.dump() << "\n";
            }
            return;
        }

        // 3. Передаём JSON в наш обработчик, а тот уже вытащит все trades
        handle_public_trade_message(msg, [&](const PublicTrade& t) {
            stats.update(t);
            std::cout << "Trade: " << t.symbol
                    << " price=" << t.price
                    << " qty="   << t.qty
                    << " ts_ms=" << t.ts_ms
                    << " side="  << (t.is_buy ? "BUY" : "SELL")
                    << "\n";
        });
    };

    client.run(channels, on_message, max_messages);

    std::cout << "\nSummary for " << symbol << ":\n"
          << "  trades:     " << stats.count << "\n"
          << "  buy volume: " << stats.buy_volume << "\n"
          << "  sell volume:" << stats.sell_volume << "\n"
          << "  min price:  " << stats.min_price << "\n"
          << "  max price:  " << stats.max_price << "\n"
          << "  last price: " << stats.last_price << "\n";

    std::cout << "Done.\n";
    return 0;
}
