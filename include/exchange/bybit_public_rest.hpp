// include/exchange/bybit_public_rest.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace exchange {

struct Ticker {
    std::string symbol;
    double last_price = 0.0;
    double best_bid   = 0.0;
    double best_ask   = 0.0;
};

struct OrderBookLevel {
    double price = 0.0;
    double qty   = 0.0;
};

struct OrderBookSnapshot {
    std::string symbol;
    std::int64_t seq    = 0;    // cross sequence
    std::int64_t ts_ms  = 0;    // system ts
    std::int64_t cts_ms = 0;    // engine ts

    std::vector<OrderBookLevel> bids; // sorted desc
    std::vector<OrderBookLevel> asks; // sorted asc
};

class BybitPublicRest {
public:
    explicit BybitPublicRest(std::string base_url = "https://api.bybit.com");

    std::string   get_server_time_raw() const;
    std::int64_t  get_server_time_ms() const;

    Ticker        get_spot_ticker(const std::string& symbol) const;

    // New: full depth snapshot for spot order book
    OrderBookSnapshot get_spot_orderbook_snapshot(const std::string& symbol,
                                                  int                 limit = 50) const;

private:
    std::string base_url_;
};

} // namespace exchange
