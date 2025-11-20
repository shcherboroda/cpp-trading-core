#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

struct BenchResult {
    std::string  name;
    std::size_t  iterations{};
    double       total_ns{}; // total time in nanoseconds

    double ns_per_iter() const {
        return (iterations > 0)
            ? (total_ns / static_cast<double>(iterations))
            : 0.0;
    }

    double iters_per_sec() const {
        double ns = ns_per_iter();
        return (ns > 0.0) ? (1e9 / ns) : 0.0;
    }
};

inline void print_result(const BenchResult& r, std::ostream& os = std::cout) {
    os << std::fixed << std::setprecision(2);
    os << "[bench] " << r.name << ": "
       << r.ns_per_iter() << " ns/op, "
       << r.iters_per_sec() / 1e6 << " Mops/s "
       << "(iters=" << r.iterations << ")\n";
}

/// Простой бенч без перцентилей: F: void(std::size_t i)
template <typename F>
BenchResult run_benchmark(std::string_view name,
                          std::size_t iterations,
                          F&& f,
                          std::size_t warmup_iterations = 0)
{
    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        f(i);
    }

    auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        f(i);
    }
    auto end = std::chrono::steady_clock::now();

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    BenchResult r;
    r.name       = std::string{name};
    r.iterations = iterations;
    r.total_ns   = static_cast<double>(total_ns);
    return r;
}

// ===== Перцентильный результат (один прогон) =====

struct BenchPercentiles {
    std::string  name;
    std::size_t  iterations{};
    double       total_ns{};
    double       p50_ns{};
    double       p95_ns{};
    double       p99_ns{};

    double ns_per_iter() const {
        return (iterations > 0)
            ? (total_ns / static_cast<double>(iterations))
            : 0.0;
    }

    double iters_per_sec() const {
        double ns = ns_per_iter();
        return (ns > 0.0) ? (1e9 / ns) : 0.0;
    }
};

inline double percentile(const std::vector<double>& samples, double p) {
    if (samples.empty()) return 0.0;
    if (p <= 0.0)        return samples.front();
    if (p >= 100.0)      return samples.back();

    double     pos = (p / 100.0) * (samples.size() - 1);
    std::size_t idx  = static_cast<std::size_t>(pos);
    double     frac = pos - static_cast<double>(idx);

    if (idx + 1 < samples.size()) {
        return samples[idx] + frac * (samples[idx + 1] - samples[idx]);
    } else {
        return samples[idx];
    }
}

/// Бенч с перцентилями по батчам: F: void(std::size_t i)
template <typename F>
BenchPercentiles run_benchmark_with_percentiles_batched(std::string_view name,
                                                        std::size_t iterations,
                                                        std::size_t batch_size,
                                                        F&& f,
                                                        std::size_t warmup_iterations = 0)
{
    if (batch_size == 0) {
        batch_size = 1;
    }

    // warmup (индексы 0..warmup_iterations-1, следим, чтобы не выйти за массивы)
    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        f(i);
    }

    using clock = std::chrono::steady_clock;

    std::vector<double> samples;
    samples.reserve((iterations + batch_size - 1) / batch_size);

    double      total_ns = 0.0;
    std::size_t i        = 0;

    while (i < iterations) {
        std::size_t this_batch = std::min(batch_size, iterations - i);

        auto t0 = clock::now();
        for (std::size_t k = 0; k < this_batch; ++k) {
            f(i + k);
        }
        auto t1 = clock::now();

        auto   batch_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        double per_op_ns = static_cast<double>(batch_ns) / static_cast<double>(this_batch);

        samples.push_back(per_op_ns);
        total_ns += static_cast<double>(batch_ns);

        i += this_batch;
    }

    std::sort(samples.begin(), samples.end());

    BenchPercentiles r;
    r.name       = std::string{name};
    r.iterations = iterations;
    r.total_ns   = total_ns;
    r.p50_ns     = percentile(samples, 50.0);
    r.p95_ns     = percentile(samples, 95.0);
    r.p99_ns     = percentile(samples, 99.0);

    return r;
}

inline void print_percentiles(const BenchPercentiles& r, std::ostream& os = std::cout) {
    os << std::fixed << std::setprecision(2);
    os << "[bench] " << r.name << ":\n"
       << "  mean: " << r.ns_per_iter()      << " ns/op, "
       << r.iters_per_sec() / 1e6           << " Mops/s\n"
       << "  p50:  " << r.p50_ns             << " ns\n"
       << "  p95:  " << r.p95_ns             << " ns\n"
       << "  p99:  " << r.p99_ns             << " ns\n"
       << "  iters: " << r.iterations        << "\n";
}

// ===== Мульти-ран (несколько прогонов, усреднение) =====

struct MultiRunSummary {
    std::string  name;
    std::size_t  runs{};
    std::size_t  iterations{};

    double       mean_ns{};    // среднее по mean ns/op
    double       mean_p50_ns{};
    double       mean_p95_ns{};
    double       mean_p99_ns{};
};

inline void print_multi(const MultiRunSummary& s, std::ostream& os = std::cout) {
    os << std::fixed << std::setprecision(2);
    os << "[bench-multi] " << s.name
       << " (runs=" << s.runs
       << ", iters=" << s.iterations << "):\n"
       << "  mean ns/op: " << s.mean_ns;
    if (s.mean_ns > 0.0) {
        os << ", " << (1e9 / s.mean_ns) / 1e6 << " Mops/s";
    }
    os << "\n"
       << "  p50 ns:     " << s.mean_p50_ns << "\n"
       << "  p95 ns:     " << s.mean_p95_ns << "\n"
       << "  p99 ns:     " << s.mean_p99_ns << "\n";
}

/// Runner: BenchPercentiles(std::size_t run_index)
template <typename Runner>
MultiRunSummary run_multi_benchmark(std::string_view name,
                                    std::size_t runs,
                                    Runner&& runner)
{
    MultiRunSummary summary;
    summary.name = std::string{name};
    summary.runs = runs;

    if (runs == 0) {
        return summary;
    }

    std::vector<BenchPercentiles> results;
    results.reserve(runs);

    for (std::size_t r = 0; r < runs; ++r) {
        results.emplace_back(runner(r));
    }

    summary.iterations = results.front().iterations;

    auto avg = [&](auto getter) {
        double sum = 0.0;
        for (const auto& res : results) {
            sum += getter(res);
        }
        return sum / static_cast<double>(results.size());
    };

    summary.mean_ns     = avg([](const BenchPercentiles& r) { return r.ns_per_iter(); });
    summary.mean_p50_ns = avg([](const BenchPercentiles& r) { return r.p50_ns; });
    summary.mean_p95_ns = avg([](const BenchPercentiles& r) { return r.p95_ns; });
    summary.mean_p99_ns = avg([](const BenchPercentiles& r) { return r.p99_ns; });

    return summary;
}

} // namespace bench
