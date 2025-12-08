// app/bybit_orderbook_snapshot_main.cpp
#include "exchange/bybit_public_rest.hpp"
#include "trading/market_data_order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using Clock = std::chrono::high_resolution_clock;
using trading::Price;
using trading::Quantity;
using trading::PriceLevel;
using trading::decode_price;
using trading::decode_qty;

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
            trading::MarketDataOrderBook book;
            book.apply_snapshot(snap.bids, snap.asks);
        }

        std::cout << "\nBenchmarking MarketDataOrderBook snapshot build...\n";
        std::cout << "  runs          : " << runs << "\n";
        std::cout << "  total levels  : " << total_levels << "\n";

        auto t_start = Clock::now();
        for (int r = 0; r < runs; ++r) {
            trading::MarketDataOrderBook book;
            book.apply_snapshot(snap.bids, snap.asks);

            // Optional: read best bid/ask at the end of each run to
            // ensure the optimizer does not completely remove the work.
            Price price;
            Quantity qty;
            if (book.best_bid(price, qty)) {
                // do nothing
            }
            if (book.best_ask(price, qty)) {
                // do nothing
            }
        }
        auto t_end = Clock::now();

        auto total_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                .count();

        double ns_per_run   = static_cast<double>(total_ns) / runs;
        double ns_per_level = ns_per_run / static_cast<double>(total_levels);

        std::cout << "\nBuild timings (MarketDataOrderBook from snapshot):\n";
        std::cout << "  total time:   " << total_ns << " ns\n";
        std::cout << "  per run:      " << ns_per_run << " ns/snapshot\n";
        std::cout << "  per level:    " << ns_per_level << " ns/level\n";

        // Build once more and print human-readable best bid/ask.
        {
            trading::MarketDataOrderBook book;
            book.apply_snapshot(snap.bids, snap.asks);

            Price price;
            Quantity qty;

            std::cout << "\nFinal best bid/ask from MarketDataOrderBook:\n";
            if (book.best_bid(price, qty)) {
                double px  = decode_price(price);
                double qty_d = decode_qty(qty);
                std::cout << "  best bid: " << px << " x " << qty_d << "\n";
            } else {
                std::cout << "  best bid: none\n";
            }
            if (book.best_ask(price, qty)) {
                double px  = decode_price(price);
                double qty_d = decode_qty(qty);
                std::cout << "  best ask: " << px << " x " << qty_d << "\n";
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
