#pragma once

#include "trading/types.hpp"

#include <cstdint>

namespace trading {

enum class EventType : std::uint8_t {
    Add,
    Market,
    Cancel,
    End    // sentinel for synthetic generators (mt_bench, etc.)
};

// Simple event type for feeding the OrderBook from any source
struct Event {
    EventType type      = EventType::Market;
    Side      side      = Side::Buy;
    Price     price     = 0;   // valid for Add
    Quantity  qty       = 0;   // valid for Add/Market
    OrderId   id        = 0;   // valid for Cancel (optional for Add)
    std::int64_t ts_ns  = 0;   // feed timestamp in ns (optional)
};

} // namespace trading
