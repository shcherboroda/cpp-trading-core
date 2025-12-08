#include "trading/market_data_order_book.hpp"
#include "trading/types.hpp"

#include <chrono>
#include <iostream>
#include <vector>

using namespace trading;

int main() {
    const int runs   = 5000;
    const int levels = 100; // 50 bid + 50 ask

    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    bids.reserve(levels / 2);
    asks.reserve(levels / 2);

    // синтетический snapshot
    for (int i = 0; i < levels / 2; ++i) {
        bids.push_back(PriceLevel{100000 - i * 1, 1});
        asks.push_back(PriceLevel{100000 + i *1, 1});
    }

    MarketDataOrderBook md;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) {
        md.apply_snapshot(bids, asks);
    }
    auto t1 = std::chrono::steady_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double per_snapshot = static_cast<double>(ns) / runs;
    double per_level    = per_snapshot / levels;

    std::cout << "MarketDataOrderBook snapshot bench\n";
    std::cout << "runs=" << runs << " levels=" << levels << "\n";
    std::cout << "ns/snapshot=" << per_snapshot
              << " ns/level=" << per_level << "\n";

    Price bp, ap;
    Quantity bq, aq;
    md.best_bid(bp, bq);
    md.best_ask(ap, aq);
    std::cout << "best bid: " << bp << " x " << bq << "\n";
    std::cout << "best ask: " << ap << " x " << aq << "\n";
}
