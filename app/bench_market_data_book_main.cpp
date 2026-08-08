#include "trading/market_data_order_book.hpp"
#include "trading/flat_market_data_order_book.hpp"
#include "trading/types.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

using namespace trading;

using Clock = std::chrono::steady_clock;

enum class Mode {
    Snapshot,
    DeltaUpdate,
    DeltaMixed,
};

enum class Implementation {
    Map,
    Flat,
};

const char* implementation_name(Implementation implementation)
{
    return implementation == Implementation::Map ? "map" : "flat";
}

const char* mode_name(Mode mode)
{
    switch (mode) {
    case Mode::Snapshot:    return "snapshot";
    case Mode::DeltaUpdate: return "delta-update";
    case Mode::DeltaMixed:  return "delta-mixed";
    }
    return "unknown";
}

bool parse_size(std::string_view text, std::size_t& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

int current_cpu() noexcept
{
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

std::vector<PriceLevel> make_bid_snapshot(std::size_t levels)
{
    std::vector<PriceLevel> bids;
    bids.reserve(levels);
    for (std::size_t i = 0; i < levels; ++i) {
        bids.push_back(PriceLevel{100000 - static_cast<Price>(i), 100 + static_cast<Quantity>(i % 10)});
    }
    return bids;
}

std::vector<PriceLevel> make_ask_snapshot(std::size_t levels)
{
    std::vector<PriceLevel> asks;
    asks.reserve(levels);
    for (std::size_t i = 0; i < levels; ++i) {
        asks.push_back(PriceLevel{100001 + static_cast<Price>(i), 100 + static_cast<Quantity>(i % 10)});
    }
    return asks;
}

std::vector<PriceLevel> make_update_delta(const std::vector<PriceLevel>& snapshot)
{
    constexpr std::size_t kUpdatesPerSide = 8;
    std::vector<PriceLevel> delta;
    const auto count = std::min(kUpdatesPerSide, snapshot.size());
    delta.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        delta.push_back(PriceLevel{snapshot[i].price, snapshot[i].qty + 1});
    }
    return delta;
}

std::vector<PriceLevel> make_mixed_delta(const std::vector<PriceLevel>& snapshot, bool alternate)
{
    constexpr std::size_t kUpdatesPerSide = 4;
    std::vector<PriceLevel> delta;
    delta.reserve(kUpdatesPerSide + 2);
    for (std::size_t i = 0; i < kUpdatesPerSide; ++i) {
        delta.push_back(PriceLevel{snapshot[i].price, snapshot[i].qty + (alternate ? 2 : 1)});
    }

    const auto toggled = snapshot[kUpdatesPerSide].price;
    const auto outside = snapshot.back().price + (snapshot.front().price < snapshot.back().price ? 1 : -1);
    if (alternate) {
        delta.push_back(PriceLevel{outside, 0});
        delta.push_back(PriceLevel{toggled, snapshot[kUpdatesPerSide].qty});
    } else {
        delta.push_back(PriceLevel{toggled, 0});
        delta.push_back(PriceLevel{outside, snapshot[kUpdatesPerSide].qty});
    }
    return delta;
}

int main(int argc, char** argv)
{
    std::size_t levels_per_side = 50;
    std::size_t iterations = 10000;
    std::size_t warmup = 1000;
    Mode mode = Mode::Snapshot;
    Implementation implementation = Implementation::Map;
    bool csv = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg.starts_with("--levels-per-side=")) {
            if (!parse_size(arg.substr(18), levels_per_side) || levels_per_side < 10) {
                std::cerr << "--levels-per-side must be at least 10\n";
                return 1;
            }
        } else if (arg.starts_with("--iterations=")) {
            if (!parse_size(arg.substr(13), iterations) || iterations == 0) {
                std::cerr << "--iterations must be positive\n";
                return 1;
            }
        } else if (arg.starts_with("--warmup=")) {
            if (!parse_size(arg.substr(9), warmup)) {
                std::cerr << "--warmup must be non-negative\n";
                return 1;
            }
        } else if (arg == "--mode=snapshot") {
            mode = Mode::Snapshot;
        } else if (arg == "--mode=delta-update") {
            mode = Mode::DeltaUpdate;
        } else if (arg == "--mode=delta-mixed") {
            mode = Mode::DeltaMixed;
        } else if (arg == "--implementation=map") {
            implementation = Implementation::Map;
        } else if (arg == "--implementation=flat") {
            implementation = Implementation::Flat;
        } else if (arg == "--format=csv") {
            csv = true;
        } else {
            std::cerr << "Usage: trading_bench_market_data_book [--levels-per-side=N] "
                         "[--iterations=N] [--warmup=N] "
                         "[--mode=snapshot|delta-update|delta-mixed] "
                         "[--implementation=map|flat] [--format=csv]\n";
            return 1;
        }
    }

    const auto bids = make_bid_snapshot(levels_per_side);
    const auto asks = make_ask_snapshot(levels_per_side);
    const auto bid_updates = make_update_delta(bids);
    const auto ask_updates = make_update_delta(asks);
    const auto bid_mixed_a = make_mixed_delta(bids, false);
    const auto ask_mixed_a = make_mixed_delta(asks, false);
    const auto bid_mixed_b = make_mixed_delta(bids, true);
    const auto ask_mixed_b = make_mixed_delta(asks, true);

    MarketDataOrderBook map_book;
    FlatMarketDataOrderBook flat_book;
    if (mode != Mode::Snapshot) {
        if (implementation == Implementation::Map) {
            map_book.apply_snapshot(bids, asks);
        } else {
            flat_book.apply_snapshot(bids, asks);
        }
    }

    auto apply_one = [&](std::size_t index) {
        if (mode == Mode::Snapshot) {
            if (implementation == Implementation::Map) {
                map_book.apply_snapshot(bids, asks);
            } else {
                flat_book.apply_snapshot(bids, asks);
            }
        } else if (mode == Mode::DeltaUpdate) {
            if (implementation == Implementation::Map) {
                map_book.apply_delta(bid_updates, ask_updates);
            } else {
                flat_book.apply_delta(bid_updates, ask_updates);
            }
        } else if ((index & 1U) == 0U) {
            if (implementation == Implementation::Map) {
                map_book.apply_delta(bid_mixed_a, ask_mixed_a);
            } else {
                flat_book.apply_delta(bid_mixed_a, ask_mixed_a);
            }
        } else {
            if (implementation == Implementation::Map) {
                map_book.apply_delta(bid_mixed_b, ask_mixed_b);
            } else {
                flat_book.apply_delta(bid_mixed_b, ask_mixed_b);
            }
        }
    };

    for (std::size_t i = 0; i < warmup; ++i) {
        apply_one(i);
    }

    const int cpu_start = current_cpu();
    std::vector<long long> samples_ns;
    samples_ns.reserve(iterations);
    std::int64_t checksum = 0;
    const auto total_start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        apply_one(i + warmup);
        const auto end = Clock::now();
        samples_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

        Price bid_price{};
        Price ask_price{};
        Quantity bid_qty{};
        Quantity ask_qty{};
        if (implementation == Implementation::Map) {
            (void)map_book.best_bid(bid_price, bid_qty);
            (void)map_book.best_ask(ask_price, ask_qty);
        } else {
            (void)flat_book.best_bid(bid_price, bid_qty);
            (void)flat_book.best_ask(ask_price, ask_qty);
        }
        checksum += bid_price + ask_price + bid_qty + ask_qty;
    }
    const auto total_end = Clock::now();
    const int cpu_end = current_cpu();

    std::sort(samples_ns.begin(), samples_ns.end());
    const auto n = samples_ns.size();
    const auto p50 = samples_ns[static_cast<std::size_t>(0.50 * (n - 1))];
    const auto p95 = samples_ns[static_cast<std::size_t>(0.95 * (n - 1))];
    const auto p99 = samples_ns[static_cast<std::size_t>(0.99 * (n - 1))];
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start).count();
    const auto batches_per_second = static_cast<double>(iterations) * 1e9 / static_cast<double>(elapsed_ns);
    const auto updates_per_batch = mode == Mode::Snapshot
        ? levels_per_side * 2
        : (mode == Mode::DeltaUpdate ? bid_updates.size() + ask_updates.size()
                                     : bid_mixed_a.size() + ask_mixed_a.size());

    if (csv) {
        std::cout << implementation_name(implementation) << ',' << mode_name(mode) << ',' << levels_per_side << ',' << iterations << ',' << warmup << ','
                  << updates_per_batch << ',' << cpu_start << ',' << cpu_end << ',' << elapsed_ns << ','
                  << batches_per_second << ',' << p50 << ',' << p95 << ',' << p99 << ',' << checksum << '\n';
    } else {
        std::cout << "mode=" << mode_name(mode) << " levels_per_side=" << levels_per_side
                  << " batches/s=" << batches_per_second << " p50/p95/p99=" << p50 << '/' << p95 << '/' << p99
                  << " ns checksum=" << checksum << '\n';
    }
}
