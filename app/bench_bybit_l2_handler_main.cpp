#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "trading/decimal_ticks.hpp"
#include "trading/market_data_order_book.hpp"
#include "trading/types.hpp"

using nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr std::int64_t kPriceMultiplier = 10;
constexpr std::int64_t kQuantityMultiplier = 1'000'000;

enum class Conversion { StodCopy, StodRef, FixedRef };

Conversion parse_conversion(int argc, char** argv) {
    const std::string mode = argc > 3 ? argv[3] : "stod-copy";
    if (mode == "stod-copy") return Conversion::StodCopy;
    if (mode == "stod-ref") return Conversion::StodRef;
    if (mode == "fixed-ref") return Conversion::FixedRef;
    throw std::invalid_argument("conversion must be stod-copy, stod-ref, or fixed-ref");
}

trading::PriceLevel to_level(const json& level, Conversion conversion) {
    if (conversion == Conversion::StodCopy) {
        return {trading::encode_price(std::stod(level.at(0).get<std::string>())),
                trading::encode_qty(std::stod(level.at(1).get<std::string>()))};
    }
    const auto& price_text = level.at(0).get_ref<const std::string&>();
    const auto& qty_text = level.at(1).get_ref<const std::string&>();
    if (conversion == Conversion::StodRef) {
        return {trading::encode_price(std::stod(price_text)), trading::encode_qty(std::stod(qty_text))};
    }
    trading::Price price{};
    trading::Quantity quantity{};
    if (!trading::parse_decimal_ticks(price_text, kPriceMultiplier, price) ||
        !trading::parse_decimal_ticks(qty_text, kQuantityMultiplier, quantity)) {
        throw std::runtime_error("invalid decimal level");
    }
    return {price, quantity};
}

long long percentile(std::vector<long long>& values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * (values.size() - 1))];
}

int main(int argc, char** argv) {
    const std::size_t levels = argc > 1 ? std::stoull(argv[1]) : 1000;
    const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 1000;
    const Conversion conversion = parse_conversion(argc, argv);
    const std::size_t warmup = 100;
    json data;
    data["b"] = json::array(); data["a"] = json::array();
    for (std::size_t i = 0; i < levels; ++i) {
        data["b"].push_back({std::to_string(100000 - i), "1.000000"});
        data["a"].push_back({std::to_string(100001 + i), "1.000000"});
    }
    const std::string payload = json{{"type", "snapshot"}, {"data", data}}.dump();
    trading::MarketDataOrderBook book;
    std::vector<long long> parse, convert, apply, total;
    parse.reserve(iterations); convert.reserve(iterations); apply.reserve(iterations); total.reserve(iterations);
    for (std::size_t n = 0; n < warmup + iterations; ++n) {
        const auto t0 = Clock::now();
        const json message = json::parse(payload);
        const auto t1 = Clock::now();
        std::vector<trading::PriceLevel> bids, asks;
        bids.reserve(levels); asks.reserve(levels);
        for (const auto& level : message.at("data").at("b")) bids.push_back(to_level(level, conversion));
        for (const auto& level : message.at("data").at("a")) asks.push_back(to_level(level, conversion));
        const auto t2 = Clock::now();
        book.apply_snapshot(bids, asks);
        const auto t3 = Clock::now();
        if (n >= warmup) {
            parse.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count());
            convert.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count());
            apply.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t3-t2).count());
            total.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t3-t0).count());
        }
    }
    const char* mode = conversion == Conversion::StodCopy ? "stod-copy" : conversion == Conversion::StodRef ? "stod-ref" : "fixed-ref";
    std::cout << "levels,conversion,parse_p50_ns,parse_p99_ns,convert_p50_ns,convert_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns\n";
    std::cout << levels << ',' << mode << ',' << percentile(parse,.50) << ',' << percentile(parse,.99) << ',' << percentile(convert,.50) << ',' << percentile(convert,.99) << ',' << percentile(apply,.50) << ',' << percentile(apply,.99) << ',' << percentile(total,.50) << ',' << percentile(total,.99) << '\n';
}
