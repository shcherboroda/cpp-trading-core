#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "exchange/bybit_l2_sax_decoder.hpp"
#include "trading/market_data_order_book.hpp"
#include "utils/latency_stats.hpp"

namespace {
using Clock = std::chrono::steady_clock;

struct StageStats {
    utils::LatencyStats decode;
    utils::LatencyStats envelope;
    utils::LatencyStats bids;
    utils::LatencyStats asks;
    utils::LatencyStats apply;
    utils::LatencyStats total;
    std::size_t capacity_growths{};
};

void print_csv(const char* type, const StageStats& stats)
{
    std::cout << type << ',' << stats.total.count() << ','
              << stats.decode.p50() << ',' << stats.decode.p95() << ',' << stats.decode.p99() << ','
              << stats.envelope.p50() << ',' << stats.bids.p50() << ',' << stats.asks.p50() << ','
              << stats.apply.p50() << ',' << stats.apply.p95() << ',' << stats.apply.p99() << ','
              << stats.total.p50() << ',' << stats.total.p95() << ',' << stats.total.p99() << ','
              << stats.capacity_growths << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: trading_bench_bybit_l2_live_path <corpus.ndjson> [measured_repeats=20] [warmup_repeats=1] [expected_topic=orderbook.50.BTCUSDT]\n";
        return 1;
    }
    const std::size_t measured_repeats = argc > 2 ? std::stoull(argv[2]) : 20;
    const std::size_t warmup_repeats = argc > 3 ? std::stoull(argv[3]) : 1;
    const std::string expected_topic = argc > 4 ? argv[4] : "orderbook.50.BTCUSDT";
    if (measured_repeats == 0) return 1;

    std::ifstream input(argv[1]);
    std::vector<std::string> frames;
    std::string line;
    while (std::getline(input, line)) if (!line.empty()) frames.push_back(line);
    if (frames.empty()) return 1;

    trading::MarketDataOrderBook book;
    std::vector<trading::PriceLevel> bids;
    std::vector<trading::PriceLevel> asks;
    StageStats snapshot;
    StageStats delta;
    bool snapshot_ready{};
    std::size_t ignored{};

    for (std::size_t repeat = 0; repeat < warmup_repeats + measured_repeats; ++repeat) {
        for (const auto& frame : frames) {
            const auto bid_capacity_before = bids.capacity();
            const auto ask_capacity_before = asks.capacity();
            const auto started = Clock::now();
            exchange::BybitL2Message message;
            exchange::BybitL2BoundedDiagnostics diagnostics;
            if (!exchange::decode_bybit_l2_bounded(frame, 10, 1'000'000, message, bids, asks, expected_topic, &diagnostics)) {
                std::cerr << "Invalid L2 frame\n";
                return 2;
            }
            const auto decoded = Clock::now();
            if (!message.topic_matches) {
                if (repeat >= warmup_repeats) ++ignored;
                continue;
            }

            StageStats* stats = nullptr;
            if (message.type == "snapshot") {
                book.apply_snapshot(bids, asks);
                snapshot_ready = true;
                stats = &snapshot;
            } else if (message.type == "delta" && snapshot_ready) {
                book.apply_delta(bids, asks);
                stats = &delta;
            } else {
                std::cerr << "Unexpected L2 sequence\n";
                return 2;
            }
            const auto completed = Clock::now();
            if (repeat < warmup_repeats) continue;
            stats->decode.add(std::chrono::duration_cast<std::chrono::nanoseconds>(decoded - started).count());
            stats->envelope.add(diagnostics.envelope_ns);
            stats->bids.add(diagnostics.bids_ns);
            stats->asks.add(diagnostics.asks_ns);
            stats->apply.add(std::chrono::duration_cast<std::chrono::nanoseconds>(completed - decoded).count());
            stats->total.add(std::chrono::duration_cast<std::chrono::nanoseconds>(completed - started).count());
            if (bids.capacity() > bid_capacity_before || asks.capacity() > ask_capacity_before) {
                ++stats->capacity_growths;
            }
        }
    }

    std::cout << "type,frames,decode_p50_ns,decode_p95_ns,decode_p99_ns,envelope_p50_ns,bids_p50_ns,asks_p50_ns,apply_p50_ns,apply_p95_ns,apply_p99_ns,total_p50_ns,total_p95_ns,total_p99_ns,vector_capacity_growths\n";
    print_csv("snapshot", snapshot);
    print_csv("delta", delta);
    std::cerr << "ignored_non_orderbook_frames=" << ignored << '\n';
}
