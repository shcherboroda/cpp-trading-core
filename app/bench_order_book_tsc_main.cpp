#include "trading/order_book.hpp"
#include "trading/types.hpp"
#include "utils/tsc_timer.hpp"   // your TscTimer
#include <random>
#include <vector>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <string_view>

using namespace trading;

struct AddParams {
    Side     side;
    Price    price;
    Quantity qty;
};

struct MktParams {
    Side     side;
    Quantity qty;
};

namespace tsc_bench {

// Simple stats for a single/multi run
struct Stats {
    std::string name;
    double      mean_ns_per_op = 0.0;
    double      p50_ns         = 0.0;
    double      p95_ns         = 0.0;
    double      p99_ns         = 0.0;
    std::size_t iterations     = 0;
    std::size_t runs           = 1;
    std::size_t batch_size     = 1;
};

inline double pick_percentile(std::vector<double>& samples, double p) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const std::size_t n   = samples.size();
    double pos            = p * static_cast<double>(n - 1);
    std::size_t idx       = static_cast<std::size_t>(pos + 0.5);
    if (idx >= n) idx = n - 1;
    return samples[idx];
}

// Single-run batched benchmark using TscTimer
template<typename F>
Stats run_single_tsc(
    std::string_view name,
    std::size_t      iterations,
    std::size_t      batch_size,
    F&&              fn,
    std::size_t      warmup_iters
) {
    if (batch_size == 0) {
        batch_size = 1;
    }

    Stats stats;
    stats.name       = std::string(name);
    stats.iterations = iterations;
    stats.batch_size = batch_size;
    stats.runs       = 1;

    if (iterations == 0) {
        return stats;
    }

    warmup_iters = std::min(warmup_iters, iterations);

    // Warmup: do not record anything here
    for (std::size_t i = 0; i < warmup_iters; ++i) {
        fn(i);
    }

    std::vector<double> samples_ns_per_op;
    samples_ns_per_op.reserve(
        (iterations - warmup_iters + batch_size - 1) / batch_size
    );

    std::size_t i = warmup_iters;
    while (i < iterations) {
        const std::size_t batch_start  = i;
        const std::size_t batch_end    = std::min(iterations, batch_start + batch_size);
        const std::size_t ops_in_batch = batch_end - batch_start;

        auto t0 = bench::TscTimer::now();
        for (std::size_t j = batch_start; j < batch_end; ++j) {
            fn(j);
        }
        auto t1 = bench::TscTimer::now();

        double ns = bench::TscTimer::to_ns(t0, t1);
        double ns_per_op = ns / static_cast<double>(ops_in_batch);
        samples_ns_per_op.push_back(ns_per_op);

        i = batch_end;
    }

    if (samples_ns_per_op.empty()) {
        return stats;
    }

    // mean(ns/op)
    double sum = std::accumulate(samples_ns_per_op.begin(),
                                 samples_ns_per_op.end(), 0.0);
    stats.mean_ns_per_op = sum / static_cast<double>(samples_ns_per_op.size());

    // percentiles
    stats.p50_ns = pick_percentile(samples_ns_per_op, 0.50);
    stats.p95_ns = pick_percentile(samples_ns_per_op, 0.95);
    stats.p99_ns = pick_percentile(samples_ns_per_op, 0.99);

    return stats;
}

// Multi-run aggregator on top of run_single_tsc
template<typename F>
Stats run_multi_tsc(
    std::string_view name,
    std::size_t      runs,
    F&&              make_single
) {
    Stats agg;
    agg.name = std::string(name);
    agg.runs = runs;

    if (runs == 0) {
        return agg;
    }

    double sum_mean = 0.0;
    double sum_p50  = 0.0;
    double sum_p95  = 0.0;
    double sum_p99  = 0.0;

    for (std::size_t r = 0; r < runs; ++r) {
        (void)r;
        Stats s = make_single();
        // On the first run, copy structural fields
        if (r == 0) {
            agg.iterations = s.iterations;
            agg.batch_size = s.batch_size;
        }
        sum_mean += s.mean_ns_per_op;
        sum_p50  += s.p50_ns;
        sum_p95  += s.p95_ns;
        sum_p99  += s.p99_ns;
    }

    const double inv_runs = 1.0 / static_cast<double>(runs);
    agg.mean_ns_per_op = sum_mean * inv_runs;
    agg.p50_ns         = sum_p50  * inv_runs;
    agg.p95_ns         = sum_p95  * inv_runs;
    agg.p99_ns         = sum_p99  * inv_runs;

    return agg;
}

inline void print_stats(const Stats& s) {
    std::cout << "[tsc-bench-multi] " << s.name
              << " (runs=" << s.runs
              << ", iters=" << s.iterations
              << ", batch=" << s.batch_size << "):\n";

    if (s.iterations == 0) {
        std::cout << "  no iterations\n";
        return;
    }

    std::cout << "  mean ns/op: " << s.mean_ns_per_op;
    if (s.mean_ns_per_op > 0.0) {
        double mops = 1e3 / s.mean_ns_per_op; // 1e3 / ns/op = Mops/s
        std::cout << ", " << mops << " Mops/s\n";
    } else {
        std::cout << "\n";
    }

    std::cout << "  p50 ns:     " << s.p50_ns << "\n";
    std::cout << "  p95 ns:     " << s.p95_ns << "\n";
    std::cout << "  p99 ns:     " << s.p99_ns << "\n";
}

} // namespace tsc_bench

int main(int argc, char** argv) {
    using tsc_bench::run_multi_tsc;
    using tsc_bench::run_single_tsc;
    using tsc_bench::print_stats;

    // ---- Benchmark parameters (similar to chrono version) ----
    std::size_t iterations = (argc > 1) ? std::stoull(argv[1]) : 200'000;
    std::size_t runs       = (argc > 2) ? std::stoull(argv[2]) : 5;
    std::size_t batch_size = (argc > 3) ? std::stoull(argv[3]) : 128;

    if (iterations == 0) {
        std::cerr << "iterations must be > 0\n";
        return 1;
    }
    if (runs == 0) {
        std::cerr << "runs must be > 0\n";
        return 1;
    }

    std::size_t warmup = iterations / 10;

    std::cout << "TSC bench config:\n"
              << "  iterations = " << iterations << "\n"
              << "  runs       = " << runs << "\n"
              << "  batch_size = " << batch_size << "\n"
              << "  warmup     = " << warmup << "\n\n";

    // ---------- Random generators (same as chrono bench) ----------
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<std::int64_t> price_dist(95, 105);
    std::uniform_int_distribution<std::int64_t> qty_dist(1, 10);

    // Pre-generate add params
    std::vector<AddParams> add_params;
    add_params.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        Side side   = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = price_dist(rng);
        Quantity qty = qty_dist(rng);
        add_params.push_back(AddParams{side, price, qty});
    }

    // Pre-generate market params
    std::vector<MktParams> mkt_params;
    mkt_params.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        Side side    = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Quantity qty = qty_dist(rng);
        mkt_params.push_back(MktParams{side, qty});
    }

    // Initial liquidity for market benchmark
    constexpr std::size_t INIT_ORDERS = 50'000;
    std::vector<AddParams> init_orders;
    init_orders.reserve(INIT_ORDERS);
    for (std::size_t i = 0; i < INIT_ORDERS; ++i) {
        Side side   = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
        Price price = price_dist(rng);
        Quantity qty = qty_dist(rng);
        init_orders.push_back(AddParams{side, price, qty});
    }

    // ---------- empty_loop (TSC overhead) ----------

    auto empty_summary = run_multi_tsc(
        "empty_loop_tsc",
        runs,
        [&]() {
            return run_single_tsc(
                "empty_loop_tsc_single",
                iterations,
                batch_size,
                [](std::size_t) {
                    // empty body
                },
                warmup
            );
        }
    );

    print_stats(empty_summary);
    std::cout << "\n";

    // ---------- OrderBook::add_limit_order (TSC) ----------

    auto add_summary = run_multi_tsc(
        "OrderBook::add_limit_order_tsc",
        runs,
        [&]() {
            OrderBook book;

            return run_single_tsc(
                "OrderBook::add_limit_order_tsc_single",
                iterations,
                batch_size,
                [&](std::size_t i) {
                    const auto& p = add_params[i];
                    book.add_limit_order(p.side, p.price, p.qty);
                },
                warmup
            );
        }
    );

    print_stats(add_summary);
    std::cout << "\n";

    // ---------- OrderBook::execute_market_order (TSC) ----------

    auto mkt_summary = run_multi_tsc(
        "OrderBook::execute_market_order_tsc",
        runs,
        [&]() {
            OrderBook book;

            for (const auto& p : init_orders) {
                book.add_limit_order(p.side, p.price, p.qty);
            }

            return run_single_tsc(
                "OrderBook::execute_market_order_tsc_single",
                iterations,
                batch_size,
                [&](std::size_t i) {
                    const auto& p = mkt_params[i];
                    book.execute_market_order(p.side, p.qty);
                },
                warmup
            );
        }
    );

    print_stats(mkt_summary);
    std::cout << "\n";

    return 0;
}
