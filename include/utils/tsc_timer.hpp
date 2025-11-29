#pragma once

#include <cstdint>
#include <chrono>
#include <thread>

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
    #include <x86intrin.h>
#endif

namespace bench::detail {

inline std::uint64_t read_tsc() noexcept {
#if defined(_MSC_VER)
    return __rdtsc();
#elif defined(__i386__) || defined(__x86_64__)
    return __rdtsc();
#else
    // If we ever build on non-x86, fail at compile time.
    #error "TscTimer is only supported on x86/x64"
#endif
}

// Calibrate TSC against steady_clock to get ns per tick.
inline double tsc_ns_per_tick() {
    static double value = [] {
        using Clock = std::chrono::steady_clock;

        const auto sleep_duration = std::chrono::milliseconds(200);

        auto t0 = Clock::now();
        auto c0 = read_tsc();
        std::this_thread::sleep_for(sleep_duration);
        auto t1 = Clock::now();
        auto c1 = read_tsc();

        auto ns    = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        auto ticks = c1 - c0;

        if (ticks == 0) {
            return 0.0;
        }

        // ns per tick
        return static_cast<double>(ns) / static_cast<double>(ticks);
    }();

    return value;
}

} // namespace bench::detail

namespace bench {

// TSC-based timer compatible with benchmark.hpp timer interface.
struct TscTimer {
    using time_point = std::uint64_t;

    static time_point now() noexcept {
        return detail::read_tsc();
    }

    static double to_ns(time_point start, time_point end) noexcept {
        std::uint64_t ticks = end - start;
        double ns_per_tick  = detail::tsc_ns_per_tick();
        return static_cast<double>(ticks) * ns_per_tick;
    }
};

} // namespace bench
