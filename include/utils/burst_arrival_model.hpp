#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace utils {

// A deterministic arrival schedule and single-server backlog model for replay.
// It uses virtual time: measured service durations are replayed against the
// configured arrival timestamps without sleeping or involving the OS scheduler.
class BurstArrivalModel {
public:
    BurstArrivalModel(std::size_t burst_size, std::int64_t burst_gap_ns) noexcept
        : burst_size_(burst_size), burst_gap_ns_(burst_gap_ns)
    {
    }

    std::int64_t arrival_ns(std::size_t event_index) const noexcept
    {
        const auto burst_index = event_index / burst_size_;
        const auto max = std::numeric_limits<std::int64_t>::max();
        if (burst_gap_ns_ != 0 && burst_index > static_cast<std::size_t>(max / burst_gap_ns_)) {
            return max;
        }
        return static_cast<std::int64_t>(burst_index) * burst_gap_ns_;
    }

private:
    std::size_t burst_size_;
    std::int64_t burst_gap_ns_;
};

struct VirtualServiceResult {
    std::int64_t queue_delay_ns;
    std::int64_t end_to_end_ns;
};

class VirtualSingleServer {
public:
    VirtualServiceResult process(std::int64_t arrival_ns, std::int64_t service_ns) noexcept
    {
        const auto start_ns = std::max(finish_ns_, arrival_ns);
        const auto queue_delay_ns = start_ns - arrival_ns;
        const auto max = std::numeric_limits<std::int64_t>::max();
        finish_ns_ = service_ns > max - start_ns ? max : start_ns + service_ns;
        return {queue_delay_ns, finish_ns_ - arrival_ns};
    }

    std::int64_t finish_ns() const noexcept { return finish_ns_; }

private:
    std::int64_t finish_ns_{};
};

} // namespace utils
