// app/ws_orderbook_snapshot_main.cpp
#include "exchange/bybit_public_rest.hpp"
#include "trading/order_book.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

using Clock = std::chrono::high_resolution_clock;
using nlohmann::json;

// Must be consistent with other places in the project.
constexpr std::int64_t PRICE_SCALE = 100;   // price -> cents
constexpr std::int64_t QTY_SCALE   = 1000;  // qty   -> 1e-3 units

exchange::OrderBookSnapshot parse_ws_snapshot(const json& msg,
                                              const std::string& expected_symbol) {
    exchange::OrderBookSnapshot snap;

    const auto& data = msg.at("data");

    snap.symbol = data.value("s", expected_symbol);
    snap.seq    = data.value("seq", 0LL);

    // In WS format ts/cts are top-level fields, not inside "data".
    snap.ts_ms  = msg.value("ts", 0LL);
    snap.cts_ms = msg.value("cts", 0LL);

    if (data.contains("b")) {
        for (const auto& lvl : data.at("b")) {
            if (!lvl.is_array() || lvl.size() < 2) continue;
            exchange::OrderBookLevel ob;
            ob.price = std::stod(lvl.at(0).get<std::string>());
            ob.qty   = std::stod(lvl.at(1).get<std::string>());
            snap.bids.push_back(ob);
        }
    }

    if (data.contains("a")) {
        for (const auto& lvl : data.at("a")) {
            if (!lvl.is_array() || lvl.size() < 2) continue;
            exchange::OrderBookLevel ob;
            ob.price = std::stod(lvl.at(0).get<std::string>());
            ob.qty   = std::stod(lvl.at(1).get<std::string>());
            snap.asks.push_back(ob);
        }
    }

    return snap;
}

} // namespace

int main(int argc, char** argv) {
    std::string symbol = "BTCUSDT";
    int runs           = 1000;

    if (argc >= 2) {
        symbol = argv[1];
    }
    if (argc >= 3) {
        runs = std::atoi(argv[2]);
        if (runs <= 0) runs = 1000;
    }

    const std::string expected_topic = "orderbook.50." + symbol;

    std::cerr << "Reading WS messages from stdin...\n";
    std::cerr << "  symbol: " << symbol << "\n";
    std::cerr << "  topic:  " << expected_topic << "\n";

    std::string line;
    json snapshot_msg;
    bool have_snapshot = false;

    // 1) Read lines until we find the first snapshot for the topic we want.
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json msg;
        try {
            msg = json::parse(line);
        } catch (const std::exception& ex) {
            std::cerr << "JSON parse error: " << ex.what() << "\n";
            continue;
        }

        // Ignore subscription acks etc.
        if (msg.contains("success") && msg.contains("op")) {
            continue;
        }

        const std::string topic = msg.value("topic", "");
        const std::string type  = msg.value("type", "");

        if (topic != expected_topic) {
            continue;
        }

        if (type == "snapshot") {
            snapshot_msg = msg;
            have_snapshot = true;
            std::cerr << "Got snapshot for topic=" << topic << "\n";
            break;
        }

        // For now we ignore delta messages.
    }

    if (!have_snapshot) {
        std::cerr << "No snapshot message found for topic=" << expected_topic << "\n";
        return 1;
    }

    // 2) Parse snapshot JSON -> OrderBookSnapshot (WS format).
    exchange::OrderBookSnapshot snap;
    try {
        snap = parse_ws_snapshot(snapshot_msg, symbol);
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse WS snapshot: " << ex.what() << "\n";
        return 1;
    }

    const std::size_t total_levels = snap.bids.size() + snap.asks.size();
    if (total_levels == 0) {
        std::cerr << "Snapshot has no levels, nothing to benchmark.\n";
        return 0;
    }

    std::cout << "WS snapshot meta:\n";
    std::cout << "  symbol   : " << snap.symbol << "\n";
    std::cout << "  seq      : " << snap.seq << "\n";
    std::cout << "  ts_ms    : " << snap.ts_ms << "\n";
    std::cout << "  cts_ms   : " << snap.cts_ms << "\n";
    std::cout << "  bids     : " << snap.bids.size() << "\n";
    std::cout << "  asks     : " << snap.asks.size() << "\n";

    // 3) Warm-up: one build
    {
        trading::OrderBook book;
        for (const auto& lvl : snap.bids) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Buy, px, qty);
        }
        for (const auto& lvl : snap.asks) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Sell, px, qty);
        }
    }

    std::cout << "\nBenchmarking OrderBook build from WS snapshot...\n";
    std::cout << "  runs          : " << runs << "\n";
    std::cout << "  total levels  : " << total_levels << "\n";

    auto t_start = Clock::now();
    for (int r = 0; r < runs; ++r) {
        trading::OrderBook book;

        for (const auto& lvl : snap.bids) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Buy, px, qty);
        }
        for (const auto& lvl : snap.asks) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Sell, px, qty);
        }

        auto bb = book.best_bid();
        auto ba = book.best_ask();
        (void)bb;
        (void)ba;
    }
    auto t_end = Clock::now();

    auto total_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

    double ns_per_run   = static_cast<double>(total_ns) / runs;
    double ns_per_level = ns_per_run / static_cast<double>(total_levels);

    std::cout << "\nBuild timings (OrderBook from WS snapshot):\n";
    std::cout << "  total time:   " << total_ns << " ns\n";
    std::cout << "  per run:      " << ns_per_run << " ns/snapshot\n";
    std::cout << "  per level:    " << ns_per_level << " ns/level\n";

    // 4) One more build + print human-readable best bid/ask.
    {
        trading::OrderBook book;

        for (const auto& lvl : snap.bids) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Buy, px, qty);
        }
        for (const auto& lvl : snap.asks) {
            auto px  = static_cast<trading::Price>(lvl.price * PRICE_SCALE);
            auto qty = static_cast<trading::Quantity>(lvl.qty * QTY_SCALE);
            book.add_limit_order(trading::Side::Sell, px, qty);
        }

        auto bb = book.best_bid();
        auto ba = book.best_ask();

        std::cout << "\nFinal best bid/ask from OrderBook (WS snapshot):\n";
        if (bb.valid) {
            double px  = static_cast<double>(bb.price) / PRICE_SCALE;
            double qty = static_cast<double>(bb.qty) / QTY_SCALE;
            std::cout << "  best bid: " << px << " x " << qty << "\n";
        } else {
            std::cout << "  best bid: none\n";
        }
        if (ba.valid) {
            double px  = static_cast<double>(ba.price) / PRICE_SCALE;
            double qty = static_cast<double>(ba.qty) / QTY_SCALE;
            std::cout << "  best ask: " << px << " x " << qty << "\n";
        } else {
            std::cout << "  best ask: none\n";
        }
    }

    return 0;
}
