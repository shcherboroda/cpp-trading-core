// src/exchange/bybit_public_rest.cpp
#include "exchange/bybit_public_rest.hpp"
#include "utils/http_client.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

using nlohmann::json;

namespace {

// j — это уже разобранный JSON c полями как в /v5/market/orderbook
exchange::OrderBookSnapshot parse_spot_orderbook_snapshot_json(const nlohmann::json& j,
                                                               const std::string& symbol_fallback) {
    exchange::OrderBookSnapshot snap;

    const auto& result = j.at("result");

    snap.symbol = result.value("s", symbol_fallback);
    snap.seq    = result.value("seq", 0LL);
    snap.ts_ms  = result.value("ts", 0LL);
    snap.cts_ms = result.value("cts", 0LL);

    if (result.contains("b")) {
        for (const auto& lvl : result.at("b")) {
            if (!lvl.is_array() || lvl.size() < 2) continue;
            exchange::OrderBookLevel ob;
            ob.price = std::stod(lvl.at(0).get<std::string>());
            ob.qty   = std::stod(lvl.at(1).get<std::string>());
            snap.bids.push_back(ob);
        }
    }

    if (result.contains("a")) {
        for (const auto& lvl : result.at("a")) {
            if (!lvl.is_array() || lvl.size() < 2) continue;
            exchange::OrderBookLevel ob;
            ob.price = std::stod(lvl.at(0).get<std::string>());
            ob.qty   = std::stod(lvl.at(1).get<std::string>());
            snap.asks.push_back(ob);
        }
    }

    return snap;
}

} // namespace

namespace exchange {

BybitPublicRest::BybitPublicRest(std::string base_url)
    : base_url_(std::move(base_url)) {}

std::string BybitPublicRest::get_server_time_raw() const {
    utils::HttpClient client(base_url_);
    return client.get("/v5/market/time");
}

std::int64_t BybitPublicRest::get_server_time_ms() const {
    utils::HttpClient client(base_url_);
    auto body = client.get("/v5/market/time");
    auto j = json::parse(body);

    if (j.value("retCode", -1) != 0) {
        throw std::runtime_error("Bybit get_server_time error: " + j.dump());
    }

    // "time" is a top-level field with server time in ms
    return j.value("time", 0LL);
}

Ticker BybitPublicRest::get_spot_ticker(const std::string& symbol) const {
    utils::HttpClient client(base_url_);

    std::string query = "category=spot&symbol=" + symbol;
    auto body = client.get("/v5/market/tickers", query);
    auto j = json::parse(body);

    if (j.value("retCode", -1) != 0) {
        throw std::runtime_error("Bybit get_spot_ticker error: " + j.dump());
    }

    const auto& result = j.at("result");
    const auto& list   = result.at("list");
    if (list.empty()) {
        throw std::runtime_error("Bybit ticker list is empty for " + symbol);
    }

    const auto& t0 = list.at(0);

    Ticker t;
    t.symbol     = t0.value("symbol", symbol);
    t.last_price = std::stod(t0.value("lastPrice", "0"));
    t.best_bid   = std::stod(t0.value("bid1Price", "0"));
    t.best_ask   = std::stod(t0.value("ask1Price", "0"));

    return t;
}

OrderBookSnapshot BybitPublicRest::get_spot_orderbook_snapshot(const std::string& symbol,
                                                               int                 limit) const {
    utils::HttpClient client(base_url_);

    // /v5/market/orderbook?category=spot&symbol=BTCUSDT&limit=50 
    std::string query = "category=spot&symbol=" + symbol +
                        "&limit=" + std::to_string(limit);

    auto body = client.get("/v5/market/orderbook", query);
    auto j    = json::parse(body);

    if (j.value("retCode", -1) != 0) {
        throw std::runtime_error("Bybit get_spot_orderbook_snapshot error: " + j.dump());
    }

    return parse_spot_orderbook_snapshot_json(j, symbol);
}

} // namespace exchange
