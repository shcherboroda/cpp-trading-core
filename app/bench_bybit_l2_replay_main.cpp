#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "exchange/bybit_l2_sax_decoder.hpp"
#include "trading/market_data_order_book.hpp"

using Clock = std::chrono::steady_clock;

long long percentile(std::vector<long long>& values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * (values.size() - 1))];
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const bool scanner = argc > 2 && std::string(argv[2]) == "scanner"; const bool bounded = argc > 2 && std::string(argv[2]) == "bounded"; const bool one_pass = argc > 2 && std::string(argv[2]) == "one-pass"; const bool verify_bounded = argc > 2 && std::string(argv[2]) == "verify-bounded"; const bool copy_topic = argc > 2 && std::string(argv[2]) == "copy-topic";
    const std::size_t repeats = argc > 3 ? std::stoull(argv[3]) : 20;
    const auto inter_frame_delay = argc > 4 ? std::chrono::microseconds(std::stoll(argv[4])) : std::chrono::microseconds::zero();
    std::ifstream input(argv[1]); std::vector<std::string> frames; std::string line;
    while (std::getline(input, line)) frames.push_back(line);
    if (frames.empty()) return 1;
    if (verify_bounded) {
        const auto same_levels = [](const auto& lhs, const auto& rhs) {
            return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                [](const auto& a, const auto& b) { return a.price == b.price && a.qty == b.qty; });
        };
        for (std::size_t i = 0; i < frames.size(); ++i) {
            exchange::BybitL2Message sax_message, bounded_message, one_pass_message;
            std::vector<trading::PriceLevel> sax_bids, sax_asks, bounded_bids, bounded_asks, one_pass_bids, one_pass_asks;
            const bool sax_ok = exchange::decode_bybit_l2(frames[i], 10, 1'000'000, sax_message, sax_bids, sax_asks, "orderbook.50.BTCUSDT");
            const bool bounded_ok = exchange::decode_bybit_l2_bounded(frames[i], 10, 1'000'000, bounded_message, bounded_bids, bounded_asks, "orderbook.50.BTCUSDT");
            const bool one_pass_ok = exchange::decode_bybit_l2_bounded_one_pass(frames[i], 10, 1'000'000, one_pass_message, one_pass_bids, one_pass_asks, "orderbook.50.BTCUSDT");
            if (sax_ok != bounded_ok || sax_ok != one_pass_ok || sax_message.type != bounded_message.type || sax_message.type != one_pass_message.type || sax_message.topic_matches != bounded_message.topic_matches || sax_message.topic_matches != one_pass_message.topic_matches || sax_message.ts != bounded_message.ts || sax_message.ts != one_pass_message.ts || sax_message.cts != bounded_message.cts || sax_message.cts != one_pass_message.cts || !same_levels(sax_bids, bounded_bids) || !same_levels(sax_bids, one_pass_bids) || !same_levels(sax_asks, bounded_asks) || !same_levels(sax_asks, one_pass_asks)) {
                std::cerr << "bounded verification failed at frame " << i + 1 << '\n'; return 2;
            }
        }
        std::cout << "bounded verification passed," << frames.size() << " frames\n";
        return 0;
    }
    std::vector<long long> decode, level_arrays, apply, total;
    for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
        trading::MarketDataOrderBook book; std::vector<trading::PriceLevel> bids, asks;
        for (const auto& frame : frames) {
            if (inter_frame_delay.count() > 0) std::this_thread::sleep_for(inter_frame_delay);
            const auto t0 = Clock::now(); exchange::BybitL2Message message; exchange::BybitL2DecodeDiagnostics diagnostics;
            if (scanner) { if (!exchange::decode_bybit_l2_scanner(frame, bids, asks)) continue; message.type = frame.find("\"type\":\"snapshot\"") != std::string::npos ? "snapshot" : "delta"; } else if (bounded) { if (!exchange::decode_bybit_l2_bounded(frame, 10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT") || !message.topic_matches) continue; } else if (one_pass) { if (!exchange::decode_bybit_l2_bounded_one_pass(frame, 10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT") || !message.topic_matches) continue; } else if (!exchange::decode_bybit_l2(frame, 10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT", copy_topic, &diagnostics) || !message.topic_matches) continue;
            const auto t1 = Clock::now();
            if (message.type == "snapshot") book.apply_snapshot(bids, asks); else if (message.type == "delta") book.apply_delta(bids, asks); else continue;
            const auto t2 = Clock::now();
            decode.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            level_arrays.push_back(diagnostics.level_arrays_ns);
            apply.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
            total.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0).count());
        }
    }
    std::cout << "variant,frames,decode_p50_ns,decode_p99_ns,level_arrays_p50_ns,level_arrays_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns\n";
    std::cout << (scanner ? "scanner" : bounded ? "bounded" : one_pass ? "one-pass" : copy_topic ? "copy-topic" : "direct-topic") << ',' << total.size() << ',' << percentile(decode,.5) << ',' << percentile(decode,.99) << ',' << percentile(level_arrays,.5) << ',' << percentile(level_arrays,.99) << ',' << percentile(apply,.5) << ',' << percentile(apply,.99) << ',' << percentile(total,.5) << ',' << percentile(total,.99) << '\n';
}
