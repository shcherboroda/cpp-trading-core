// app/bybit_orderbook_snapshot_main.cpp
#include "exchange/bybit_public_rest.hpp"
#include "trading/order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using Clock = std::chrono::high_resolution_clock;

// Keep these consistent with the Python WS feed scaling.
constexpr std::int64_t PRICE_SCALE = 100;   // price -> cents
constexpr std::int64_t QTY_SCALE   = 1000;  // qty   -> 1e-3 units

} // namespace

int main(int argc, char** argv) {
    std::string symbol = "BTCUSDT";
    int limit          = 50;
    int runs           = 1000;

    if (argc >= 2) {
        symbol = argv[1];
    }
    if (argc >= 3) {
        limit = std::atoi(argv[2]);
    }
    if (argc >= 4) {
        runs = std::atoi(argv[3]);
    }

    try {
        exchange::BybitPublicRest client;

        std::cout << "Requesting orderbook snapshot for " << symbol
                  << " (limit=" << limit << ")...\n";

        auto http_start = Clock::now();
        auto snap       = client.get_spot_orderbook_snapshot(symbol, limit);
        auto http_end   = Clock::now();

        auto http_ns = std::chrono::duration_cast<std::chrono::microseconds>(
                           http_end - http_start)
                           .count();

        std::cout << "HTTP snapshot done in " << http_ns << " us\n";
        std::cout << "Snapshot meta:\n";
        std::cout << "  symbol   : " << snap.symbol << "\n";
        std::cout << "  seq      : " << snap.seq << "\n";
        std::cout << "  ts_ms    : " << snap.ts_ms << "\n";
        std::cout << "  cts_ms   : " << snap.cts_ms << "\n";
        std::cout << "  bids     : " << snap.bids.size() << "\n";
        std::cout << "  asks     : " << snap.asks.size() << "\n";

        const std::size_t total_levels = snap.bids.size() + snap.asks.size();
        if (total_levels == 0) {
            std::cout << "No levels in snapshot, nothing to benchmark.\n";
            return 0;
        }

        // Warm-up: build once to touch code & caches
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

        std::cout << "\nBenchmarking OrderBook snapshot build...\n";
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

            // Optional: read best bid/ask at the end of each run to
            // ensure the optimizer does not completely remove the work.
            auto bb = book.best_bid();
            auto ba = book.best_ask();
            (void)bb;
            (void)ba;
        }
        auto t_end = Clock::now();

        auto total_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                .count();

        double ns_per_run   = static_cast<double>(total_ns) / runs;
        double ns_per_level = ns_per_run / static_cast<double>(total_levels);

        std::cout << "\nBuild timings (OrderBook from snapshot):\n";
        std::cout << "  total time:   " << total_ns << " ns\n";
        std::cout << "  per run:      " << ns_per_run << " ns/snapshot\n";
        std::cout << "  per level:    " << ns_per_level << " ns/level\n";

        // Build once more and print human-readable best bid/ask.
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

            std::cout << "\nFinal best bid/ask from OrderBook:\n";
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

    } catch (const std::exception& ex) {
        std::cerr << "bybit_orderbook_snapshot error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
