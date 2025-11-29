#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>

namespace bench {

// Result for a single benchmark (and aggregated multi-run)
struct Result {
    std::string  name;
    double       mean_ns_per_op = 0.0;
    double       p50_ns         = 0.0;
    double       p95_ns         = 0.0;
    double       p99_ns         = 0.0;
    std::size_t  iterations     = 0;
    std::size_t  runs           = 1;
    std::size_t  batch_size     = 1;
};

// Chrono-based timer (steady_clock)
struct ChronoTimer {
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;

    static time_point now() noexcept {
        return clock::now();
    }

    static double to_ns(time_point start, time_point end) noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
};

namespace detail {

template<typename F>
Result run_benchmark_with_percentiles_batched_impl(
    std::string_view name,
    std::size_t      iterations,
    std::size_t      batch_size,
    F&&              fn,
    std::size_t      warmup_iters)
{
    if (batch_size == 0) {
        batch_size = 1;
    }

    Result stats;
    stats.name       = std::string(name);
    stats.iterations = iterations;
    stats.batch_size = batch_size;
    stats.runs       = 1;

    if (iterations == 0) {
        return stats;
    }

    warmup_iters = std::min(warmup_iters, iterations);

    // Warmup: fn(i) for i in [0, warmup_iters)
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

        auto t0 = ChronoTimer::now();
        for (std::size_t j = batch_start; j < batch_end; ++j) {
            fn(j);
        }
        auto t1 = ChronoTimer::now();

        double ns        = ChronoTimer::to_ns(t0, t1);
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
    std::sort(samples_ns_per_op.begin(), samples_ns_per_op.end());
    const std::size_t n = samples_ns_per_op.size();

    auto pick = [&](double p) -> double {
        if (n == 0) return 0.0;
        double pos      = p * static_cast<double>(n - 1);
        std::size_t idx = static_cast<std::size_t>(pos + 0.5);
        if (idx >= n) idx = n - 1;
        return samples_ns_per_op[idx];
    };

    stats.p50_ns = pick(0.50);
    stats.p95_ns = pick(0.95);
    stats.p99_ns = pick(0.99);

    return stats;
}

} // namespace detail

// Single-run batched benchmark (chrono-based)
template<typename F>
Result run_benchmark_with_percentiles_batched(
    std::string_view name,
    std::size_t      iterations,
    std::size_t      batch_size,
    F&&              fn,
    std::size_t      warmup_iters = 0)
{
    return detail::run_benchmark_with_percentiles_batched_impl(
        name, iterations, batch_size, std::forward<F>(fn), warmup_iters
    );
}

// Multi-run aggregator, как ожидает bench_order_book_main.cpp
template<typename F>
Result run_multi_benchmark(
    std::string_view name,
    std::size_t      runs,
    F&&              make_single)
{
    Result agg;
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
        Result s = make_single(r);

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

// Printer (как ожидает print_multi)
inline void print_multi(const Result& s)
{
    std::cout << "[bench-multi] " << s.name
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

} // namespace bench
