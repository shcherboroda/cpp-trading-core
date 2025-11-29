#include <iostream>
#include <string>
#include <map>
#include <limits>
#include <cmath> 

#include <nlohmann/json.hpp>

#include "exchange/bybit_public_ws.hpp"
#include "trading/order_book.hpp"

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

struct SimpleLevelBook {
    // Aggregated book per price level
    std::map<double, double, std::greater<double>> bids; // price -> qty
    std::map<double, double, std::less<double>>    asks; // price -> qty
};

// Build level-book from snapshot JSON: data.b / data.a
void build_level_book_from_snapshot(SimpleLevelBook& lvl_book, const json& data)
{
    lvl_book.bids.clear();
    lvl_book.asks.clear();

    const auto& bids = data.at("b");
    const auto& asks = data.at("a");

    for (const auto& lvl : bids) {
        // lvl = ["price", "qty"]
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double price = std::stod(price_str);
        double qty   = std::stod(qty_str);
        if (qty > 0.0) {
            lvl_book.bids[price] = qty;
        }
    }

    for (const auto& lvl : asks) {
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double price = std::stod(price_str);
        double qty   = std::stod(qty_str);
        if (qty > 0.0) {
            lvl_book.asks[price] = qty;
        }
    }
}

// Apply delta to existing level-book: qty==0 -> erase, else set qty
void apply_level_book_delta(SimpleLevelBook& lvl_book, const json& data)
{
    const auto& bids = data.at("b");
    const auto& asks = data.at("a");

    for (const auto& lvl : bids) {
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double price = std::stod(price_str);
        double qty   = std::stod(qty_str);

        if (qty == 0.0) {
            lvl_book.bids.erase(price);
        } else {
            lvl_book.bids[price] = qty;
        }
    }

    for (const auto& lvl : asks) {
        const std::string price_str = lvl.at(0).get<std::string>();
        const std::string qty_str   = lvl.at(1).get<std::string>();

        double price = std::stod(price_str);
        double qty   = std::stod(qty_str);

        if (qty == 0.0) {
            lvl_book.asks.erase(price);
        } else {
            lvl_book.asks[price] = qty;
        }
    }
}

// Rebuild your trading::OrderBook from SimpleLevelBook
void build_order_book_from_levels(trading::OrderBook& book,
                                  const SimpleLevelBook& lvl_book)
{
    // simplest reset: assign a fresh empty book
    book = trading::OrderBook{};

    // bids
    for (const auto& [price, qty] : lvl_book.bids) {
        auto p = to_price_ticks(price);
        auto q = to_qty_ticks(qty);
        if (q > 0) {
            book.add_limit_order(trading::Side::Buy, p, q);
        }
    }

    // asks
    for (const auto& [price, qty] : lvl_book.asks) {
        auto p = to_price_ticks(price);
        auto q = to_qty_ticks(qty);
        if (q > 0) {
            book.add_limit_order(trading::Side::Sell, p, q);
        }
    }
}


void print_best(const trading::OrderBook& book, const char* tag)
{
    auto bb = book.best_bid();
    auto ba = book.best_ask();

    std::cout << tag << " "
              << "best bid=";
    if (bb.valid) {
        std::cout << from_price_ticks(bb.price)
                  << " x "
                  << from_qty_ticks(bb.qty);
    } else {
        std::cout << "none";
    }

    std::cout << ", best ask=";
    if (ba.valid) {
        std::cout << from_price_ticks(ba.price)
                  << " x "
                  << from_qty_ticks(ba.qty);
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

    SimpleLevelBook lvl_book;
    trading::OrderBook book;
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
            build_level_book_from_snapshot(lvl_book, data);
            build_order_book_from_levels(book, lvl_book);
            snapshot_ready = true;
            // 3) фиксируем время обработки
            auto t_end   = SteadyClock::now();
            double proc_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                    .count();

            stats.add(proc_ns, latency_ms, true);
            if (kVerbosePrint) {
                print_best(book, "[SNAPSHOT]");
            }
        } else if (type == "delta") {
            if (!snapshot_ready) {
                // Bybit гарантирует сначала снапшот, но подстрахуемся
                return;
            }
            apply_level_book_delta(lvl_book, data);
            build_order_book_from_levels(book, lvl_book);
            // 3) фиксируем время обработки
            auto t_end   = SteadyClock::now();
            double proc_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start)
                    .count();

            stats.add(proc_ns, latency_ms, false);
            if (kVerbosePrint) {
                print_best(book, "[DELTA]");
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
