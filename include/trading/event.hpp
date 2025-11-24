#pragma once

#include "trading/types.hpp"

namespace trading {

enum class EventType {
    Add,
    Market,
    Cancel,
    End   // в replay просто не будем использовать
};

struct Event {
    EventType type{EventType::Add};
    Side      side{Side::Buy};
    Price     price{0};
    Quantity  qty{0};
    OrderId   id{0};
};

} // namespace trading
