#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "exchange/bybit_l2_sax_decoder.hpp"
#include "trading/market_data_order_book.hpp"
#include "utils/burst_arrival_model.hpp"
#include "utils/latency_stats.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Config {
    std::string corpus_path;
    std::size_t repeats{20};
    std::size_t burst_size{1};
    std::int64_t burst_gap_ns{1'000'000};
    bool csv{};
};

bool parse_size(std::string_view text, std::size_t& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool parse_duration_ns(std::string_view text, std::int64_t& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value >= 0;
}

bool parse_option(std::string_view arg, std::string_view prefix, std::size_t& value)
{
    return arg.starts_with(prefix) && parse_size(arg.substr(prefix.size()), value);
}

bool parse_option(std::string_view arg, std::string_view prefix, std::int64_t& value)
{
    return arg.starts_with(prefix) && parse_duration_ns(arg.substr(prefix.size()), value);
}

void print_csv_header()
{
    std::cout << "model,repeats,burst_size,burst_gap_ns,frames,service_p50_ns,service_p95_ns,service_p99_ns,"
                 "queue_p50_ns,queue_p95_ns,queue_p99_ns,queue_max_ns,burst_start_queue_p50_ns,burst_start_queue_p95_ns,"
                 "burst_start_queue_p99_ns,burst_start_queue_max_ns,e2e_p50_ns,e2e_p95_ns,e2e_p99_ns,e2e_max_ns,checksum\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: trading_bench_bybit_l2_arrival_replay <corpus.ndjson> "
                     "[--repeats=N] [--burst-size=N] [--burst-gap-ns=N] [--format=csv]\n";
        return 1;
    }

    Config config;
    config.corpus_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--format=csv") {
            config.csv = true;
        } else if (!parse_option(arg, "--repeats=", config.repeats) &&
                   !parse_option(arg, "--burst-size=", config.burst_size) &&
                   !parse_option(arg, "--burst-gap-ns=", config.burst_gap_ns)) {
            std::cerr << "Invalid option: " << arg << '\n';
            return 1;
        }
    }
    if (config.repeats == 0 || config.burst_size == 0) {
        std::cerr << "repeats and burst-size must be positive\n";
        return 1;
    }

    std::ifstream input(config.corpus_path);
    if (!input) {
        std::cerr << "Cannot open corpus: " << config.corpus_path << '\n';
        return 1;
    }
    std::vector<std::string> frames;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) frames.push_back(line);
    }
    if (frames.empty()) {
        std::cerr << "Corpus is empty\n";
        return 1;
    }

    utils::LatencyStats service_stats;
    utils::LatencyStats queue_stats;
    utils::LatencyStats burst_start_queue_stats;
    utils::LatencyStats e2e_stats;
    utils::BurstArrivalModel arrivals(config.burst_size, config.burst_gap_ns);
    utils::VirtualSingleServer server;
    std::size_t event_index{};
    std::uint64_t checksum{};

    for (std::size_t repeat = 0; repeat < config.repeats; ++repeat) {
        trading::MarketDataOrderBook book;
        std::vector<trading::PriceLevel> bids;
        std::vector<trading::PriceLevel> asks;
        bool snapshot_ready{};

        for (const auto& frame : frames) {
            const auto started = Clock::now();
            exchange::BybitL2Message message;
            if (!exchange::decode_bybit_l2_bounded(
                    frame, 10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT")) {
                std::cerr << "Invalid L2 frame during replay\n";
                return 2;
            }
            if (!message.topic_matches) continue;

            if (message.type == "snapshot") {
                book.apply_snapshot(bids, asks);
                snapshot_ready = true;
            } else if (message.type == "delta" && snapshot_ready) {
                book.apply_delta(bids, asks);
            } else {
                std::cerr << "Unexpected orderbook sequence during replay\n";
                return 2;
            }
            const auto completed = Clock::now();
            const auto service_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(completed - started).count();
            const auto arrival_ns = arrivals.arrival_ns(event_index++);
            const auto virtual_result = server.process(arrival_ns, service_ns);
            service_stats.add(service_ns);
            queue_stats.add(virtual_result.queue_delay_ns);
            if ((event_index - 1) % config.burst_size == 0) {
                burst_start_queue_stats.add(virtual_result.queue_delay_ns);
            }
            e2e_stats.add(virtual_result.end_to_end_ns);
        }
        checksum += static_cast<std::uint64_t>(book.bids().size() + book.asks().size());
    }

    if (config.csv) {
        print_csv_header();
    } else {
        std::cout << "Arrival model: virtual single-server batch replay (no sleep/network)\n"
                  << "repeats=" << config.repeats << ", messages_per_batch=" << config.burst_size
                  << ", gap_between_batches_ns=" << config.burst_gap_ns << ", frames=" << event_index << "\n\n";
        std::cout << "Measured decode+apply service:\n";
        service_stats.print_summary(std::cout, "ns");
        std::cout << "\nVirtual queue delay:\n";
        queue_stats.print_summary(std::cout, "ns");
        std::cout << "\nQueue delay at the first frame of each batch (unfinished work from the prior batch):\n";
        burst_start_queue_stats.print_summary(std::cout, "ns");
        std::cout << "\nVirtual end-to-end completion:\n";
        e2e_stats.print_summary(std::cout, "ns");
        std::cout << "\n";
    }
    std::cout << "virtual_single_server," << config.repeats << ',' << config.burst_size << ','
              << config.burst_gap_ns << ',' << event_index << ',' << service_stats.p50() << ','
              << service_stats.p95() << ',' << service_stats.p99() << ',' << queue_stats.p50() << ','
              << queue_stats.p95() << ',' << queue_stats.p99() << ',' << queue_stats.max() << ','
              << burst_start_queue_stats.p50() << ',' << burst_start_queue_stats.p95() << ','
              << burst_start_queue_stats.p99() << ',' << burst_start_queue_stats.max() << ','
              << e2e_stats.p50() << ',' << e2e_stats.p95() << ',' << e2e_stats.p99() << ','
              << e2e_stats.max() << ',' << checksum << '\n';
}
