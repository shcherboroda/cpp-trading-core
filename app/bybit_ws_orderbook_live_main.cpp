#include <iostream>
#include <string>
#include <map>
#include <limits>
#include <cmath> 
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "exchange/bybit_public_ws.hpp"
#include "exchange/bybit_l2_sax_decoder.hpp"
#include "trading/market_data_order_book.hpp"
#include "trading/decimal_ticks.hpp"
#include "utils/latency_stats.hpp"

using nlohmann::json;

namespace {

constexpr bool kVerbosePrint = false;  // или true, когда хочешь посмотреть вживую

constexpr std::int64_t PRICE_MULT = 10;       // хранить цену с точностью 0.1
constexpr std::int64_t QTY_MULT   = 1'000'000; // хранить объём с точностью 1e-6

using SteadyClock = std::chrono::steady_clock;
using SysClock    = std::chrono::system_clock;

inline trading::Price to_price_ticks(double px)
{
    return static_cast<trading::Price>(std::llround(px * PRICE_MULT));
}

inline trading::Quantity to_qty_ticks(double q)
{
    const double scaled = q * QTY_MULT;
    if (scaled <= 0.0) {
        return static_cast<trading::Quantity>(0);
    }
    return static_cast<trading::Quantity>(std::llround(scaled));
}

inline double from_price_ticks(trading::Price p)
{
    return static_cast<double>(p) / PRICE_MULT;
}

inline double from_qty_ticks(trading::Quantity q)
{
    return static_cast<double>(q) / QTY_MULT;
}

void print_best(const trading::MarketDataOrderBook& book, const char* tag)
{
    trading::Price bp, ap;
    trading::Quantity bq, aq;

    std::cout << tag << " "
              << "best bid=";
    if (book.best_bid(bp, bq)) {
        std::cout << from_price_ticks(bp)
                  << " x "
                  << from_qty_ticks(bq);
    } else {
        std::cout << "none";
    }

    std::cout << ", best ask=";
    if (book.best_ask(ap, aq)) {
        std::cout << from_price_ticks(ap)
                  << " x "
                  << from_qty_ticks(aq);
    } else {
        std::cout << "none";
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    std::string symbol = "BTCUSDT";
    if (argc > 1) {
        symbol = argv[1];
    }

    int max_messages = 0; // 0 = run until Ctrl+C
    if (argc > 2) {
        max_messages = std::stoi(argv[2]);
    }

    std::cout << "Connecting to Bybit WS orderbook for " << symbol
              << ", max_messages=" << max_messages << " (0 = infinite)...\n";

    exchange::BybitPublicWs client;

    trading::MarketDataOrderBook md_book;
    std::vector<trading::PriceLevel> bids;
    std::vector<trading::PriceLevel> asks;
    bool snapshot_ready = false;

    // Counters for book events
    std::size_t snapshots = 0;
    std::size_t deltas    = 0;

    utils::LatencyStats handler_stats;      // ns
    utils::LatencyStats decode_stats;       // ns
    utils::LatencyStats apply_stats;        // ns
    utils::LatencyStats data_latency_stats; // ms (int64)
    utils::LatencyStats snapshot_levels_stats;
    utils::LatencyStats delta_levels_stats;

    std::string expected_topic = "orderbook.50." + symbol;

    auto on_message = [&](std::string_view payload) {
        const auto t_start = SteadyClock::now();
        exchange::BybitL2Message msg;
        if (!exchange::decode_bybit_l2(payload, PRICE_MULT, QTY_MULT, msg, bids, asks, expected_topic)) {
            std::cerr << "[orderbook] invalid Bybit L2 message\n";
            return;
        }
        const auto t_decoded = SteadyClock::now();
        // Filter by topic early
        if (!msg.topic_matches) {
            return;
        }

        auto now_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                           SysClock::now().time_since_epoch())
                           .count();

        const long long msg_ts_ms = msg.ts > 0 ? msg.ts : msg.cts;
        if (msg_ts_ms > 0) {
            data_latency_stats.add(static_cast<std::int64_t>(now_ms - msg_ts_ms));
        }

        if (msg.type == "snapshot") {
            md_book.apply_snapshot(bids, asks);
            const auto t_applied = SteadyClock::now();
            snapshot_ready = true;
            ++snapshots;

            handler_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_applied - t_start).count());
            decode_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_decoded - t_start).count());
            apply_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_applied - t_decoded).count());
            snapshot_levels_stats.add(static_cast<std::int64_t>(bids.size() + asks.size()));

            if (kVerbosePrint) {
                print_best(md_book, "[SNAPSHOT]");
            }
        } else if (msg.type == "delta") {
            if (!snapshot_ready) {
                // Bybit guarantees snapshot first, but be defensive
                return;
            }

            md_book.apply_delta(bids, asks);
            const auto t_applied = SteadyClock::now();
            ++deltas;

            handler_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_applied - t_start).count());
            decode_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_decoded - t_start).count());
            apply_stats.add(std::chrono::duration_cast<std::chrono::nanoseconds>(t_applied - t_decoded).count());
            delta_levels_stats.add(static_cast<std::int64_t>(bids.size() + asks.size()));

            if (kVerbosePrint) {
                print_best(md_book, "[DELTA]");
            }
        }
    };

    std::vector<std::string> topics = {
        "orderbook.50." + symbol
    };

    client.run_text(topics, on_message, max_messages);

    const auto messages = handler_stats.count();

    std::cout << "\n=== Live WS orderbook stats ===\n";
    std::cout << "Messages: " << messages
              << " (snapshots=" << snapshots
              << ", deltas=" << deltas << ")\n\n";

    std::cout << "Processing time (handler):\n";
    handler_stats.print_summary(std::cout, "ns");
    std::cout << "\nDecode time:\n";
    decode_stats.print_summary(std::cout, "ns");
    std::cout << "\nBook-apply time:\n";
    apply_stats.print_summary(std::cout, "ns");
    std::cout << "\n\n";

    std::cout << "Data latency (local_now_ms - msg.ts_ms):\n";
    data_latency_stats.print_summary(std::cout, "ms");
    std::cout << "\n";

    std::cout << "Levels per message (snapshot):\n";
    snapshot_levels_stats.print_summary(std::cout, "levels");
    std::cout << "\nLevels per message (delta):\n";
    delta_levels_stats.print_summary(std::cout, "levels");
    std::cout << "\n";

    std::cout << "Done.\n";

    return 0;
}
