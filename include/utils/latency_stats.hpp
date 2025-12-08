#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

namespace utils {

/**
 * @brief Simple latency statistics collector.
 *
 * Stores all samples in a vector (int64 nanoseconds, milliseconds, etc.)
 * and computes basic stats (count, min, max, mean, percentiles).
 *
 * This is designed for benchmarking and diagnostics, not for
 * unbounded high-frequency production use. For typical use-cases
 * in this project (up to tens of thousands of samples), storing
 * all values and sorting a copy is perfectly acceptable and keeps
 * the implementation straightforward and debuggable.
 */
class LatencyStats {
public:
    using value_type = std::int64_t;

    LatencyStats()
        : min_(std::numeric_limits<value_type>::max()),
          max_(std::numeric_limits<value_type>::min()),
          sum_(0)
    {
    }

    /// Remove all collected samples and reset stats.
    void clear()
    {
        values_.clear();
        min_ = std::numeric_limits<value_type>::max();
        max_ = std::numeric_limits<value_type>::min();
        sum_ = 0;
    }

    /// Add a new sample value.
    void add(value_type v)
    {
        values_.push_back(v);
        if (v < min_) min_ = v;
        if (v > max_) max_ = v;
        sum_ += static_cast<long double>(v);
    }

    /// Number of collected samples.
    std::size_t count() const noexcept { return values_.size(); }

    /// True if no samples were recorded.
    bool empty() const noexcept { return values_.empty(); }

    /// Minimum value (undefined if empty()).
    value_type min() const noexcept { return min_; }

    /// Maximum value (undefined if empty()).
    value_type max() const noexcept { return max_; }

    /// Arithmetic mean (returns 0.0 if empty()).
    double mean() const noexcept
    {
        if (values_.empty()) return 0.0;
        return static_cast<double>(sum_ / static_cast<long double>(values_.size()));
    }

    /// 50th percentile (median).
    value_type p50() const { return percentile(50.0); }

    /// 95th percentile.
    value_type p95() const { return percentile(95.0); }

    /// 99th percentile.
    value_type p99() const { return percentile(99.0); }

    /**
     * @brief Compute a percentile in [0, 100].
     *
     * Uses a sorted copy of the collected samples and the nearest-rank method.
     * For example, p = 50 -> median, p = 95 -> 95th percentile.
     */
    value_type percentile(double p) const
    {
        if (values_.empty()) {
            return 0;
        }
        if (p <= 0.0) {
            return min_;
        }
        if (p >= 100.0) {
            return max_;
        }

        std::vector<value_type> tmp(values_);
        std::sort(tmp.begin(), tmp.end());

        const double rank = (p / 100.0) * static_cast<double>(tmp.size() - 1);
        const auto idx = static_cast<std::size_t>(rank + 0.5); // nearest index
        return tmp[idx];
    }

    /**
     * @brief Print a human-readable summary into a stream.
     *
     * Example output (unit_label = \"ns\"):
     *   count=1000, mean=12345.6 ns, p50=12000 ns, p95=20000 ns, p99=30000 ns
     */
    void print_summary(std::ostream& os, const char* unit_label) const
    {
        os << "count=" << count()
           << ", mean=" << mean() << " " << unit_label
           << ", p50=" << p50() << " " << unit_label
           << ", p95=" << p95() << " " << unit_label
           << ", p99=" << p99() << " " << unit_label;
    }

private:
    std::vector<value_type> values_;
    value_type min_;
    value_type max_;
    long double sum_;
};

} // namespace utils
