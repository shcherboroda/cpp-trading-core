#include <iostream>
#include <string>
#include <map>
#include <limits>
#include <cmath> 

#include <nlohmann/json.hpp>

#include "exchange/bybit_public_ws.hpp"
#include "trading/market_data_order_book.hpp"

using nlohmann::json;

namespace {

constexpr bool kVerbosePrint = false;  // или true, когда хочешь посмотреть вживую

constexpr double PRICE_MULT = 10.0;       // хранить цену с точностью 0.1
constexpr double QTY_MULT   = 1'000'000.; // хранить объём с точностью 1e-6

using SteadyClock = std::chrono::steady_clock;
using SysClock    = std::chrono::system_clock;

struct LiveStats {
    std::vector<double> process_ns;      // handler time per msg
    std::vector<double> data_latency_ms; // now_ms - msg.ts
    std::size_t snapshots = 0;
    std::size_t deltas    = 0;

    void add(double proc_ns, double lat_ms, bool is_snapshot) {
        process_ns.push_back(proc_ns);
        data_latency_ms.push_back(lat_ms);
        if (is_snapshot)
            ++snapshots;
        else
            ++deltas;
    }
};

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = (p / 100.0) * (v.size() - 1);
    std::size_t i = static_cast<std::size_t>(idx);
    return v[i];
}

void print_stats(const LiveStats& s) {
    if (s.process_ns.empty()) {
        std::cout << "\n[stats] no messages processed\n";
        return;
    }

    auto proc = s.process_ns;
    auto lat  = s.data_latency_ms;

    double mean_proc = std::accumulate(proc.begin(), proc.end(), 0.0) / proc.size();
    double mean_lat  = std::accumulate(lat.begin(),  lat.end(),  0.0) / lat.size();

    std::cout << "\n=== Live WS orderbook stats ===\n";
    std::cout << "Messages: " << s.process_ns.size()
              << " (snapshots=" << s.snapshots
              << ", deltas="    << s.deltas  << ")\n\n";

    std::cout << "Processing time (handler):\n";
    std::cout << "  mean: " << mean_proc             << " ns\n";
    std::cout << "  p50 : " << percentile(proc, 50.) << " ns\n";
    std::cout << "  p95 : " << percentile(proc, 95.) << " ns\n";
    std::cout << "  p99 : " << percentile(proc, 99.) << " ns\n\n";

    std::cout << "Data latency (local_now_ms - msg.ts_ms):\n";
    std::cout << "  mean: " << mean_lat              << " ms\n";
    std::cout << "  p50 : " << percentile(lat, 50.)  << " ms\n";
    std::cout << "  p95 : " << percentile(lat, 95.)  << " ms\n";
    std::cout << "  p99 : " << percentile(lat, 99.)  << " ms\n";
}

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

// JSON -> vectors of PriceLevel in ticks
void json_to_price_levels(const json& data,
                          std::vector<trading::PriceLevel>& bids,
                          std::vector<trading::PriceLevel>& asks)
{
    bids.clear();
    asks.clear();

    const auto& j_bids = data.at("b");
    const auto& j_asks = data.at("a");

    bids.reserve(j_bids.size());
    asks.reserve(j_asks.size());

    for (const auto& lvl : j_bids) {
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double px = std::stod(price_str);
        double q  = std::stod(qty_str);

        trading::PriceLevel pl{
            trading::encode_price(px),
            trading::encode_qty(q),
        };
        bids.push_back(pl);
    }

    for (const auto& lvl : j_asks) {
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double px = std::stod(price_str);
        double q  = std::stod(qty_str);

        trading::PriceLevel pl{
            trading::encode_price(px),
            trading::encode_qty(q),
        };
        asks.push_back(pl);
    }
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

    LiveStats stats;

    std::string expected_topic = "orderbook.50." + symbol;

    auto on_message = [&](const json& msg) {
        // 1) отметим время и local now в мс
        auto t_start = SteadyClock::now();
        auto now_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                        SysClock::now().time_since_epoch())
                        .count();

        long long msg_ts_ms = 0;
        if (msg.contains("ts") && msg["ts"].is_number_integer()) {
            msg_ts_ms = msg["ts"].get<long long>();
        } else if (msg.contains("cts") && msg["cts"].is_number_integer()) {
            // fallback, если потребуется
            msg_ts_ms = msg["cts"].get<long long>();
        }
        double latency_ms = 0.0;
        if (msg_ts_ms > 0) {
            latency_ms = static_cast<double>(now_ms - msg_ts_ms);
        }
        
        if (!msg.contains("topic")) return;

        const std::string topic = msg.value("topic", "");
        if (topic != expected_topic) {
            return;
        }

        const std::string type = msg.value("type", "");
        const auto& data = msg.at("data");

        if (type == "snapshot") {
            json_to_price_levels(data, bids, asks);
            md_book.apply_snapshot(bids, asks);
            snapshot_ready = true;
            // 3) фиксируем время обработки
            auto t_end   = SteadyClock::now();
            double proc_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                    .count();

            stats.add(proc_ns, latency_ms, true);
            if (kVerbosePrint) {
                print_best(md_book, "[SNAPSHOT]");
            }
        } else if (type == "delta") {
            if (!snapshot_ready) {
                // Bybit гарантирует сначала снапшот, но подстрахуемся
                return;
            }
            json_to_price_levels(data, bids, asks);
            md_book.apply_delta(bids, asks);
            // 3) фиксируем время обработки
            auto t_end   = SteadyClock::now();
            double proc_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                    .count();

            stats.add(proc_ns, latency_ms, false);
            if (kVerbosePrint) {
                print_best(md_book, "[DELTA]");
            }
        }

    };

    std::vector<std::string> topics = {
        "orderbook.50." + symbol
    };

    client.run(topics, on_message, max_messages);

    print_stats(stats);

    std::cout << "Done.\n";
    return 0;
}
