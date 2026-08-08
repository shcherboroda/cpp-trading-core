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
enum class Parser { Dom, Sax };
enum class Input { Direct, Copy };

Conversion parse_conversion(int argc, char** argv) {
    const std::string mode = argc > 3 ? argv[3] : "stod-copy";
    if (mode == "stod-copy") return Conversion::StodCopy;
    if (mode == "stod-ref") return Conversion::StodRef;
    if (mode == "fixed-ref") return Conversion::FixedRef;
    throw std::invalid_argument("conversion must be stod-copy, stod-ref, or fixed-ref");
}

Parser parse_parser(int argc, char** argv) {
    const std::string mode = argc > 4 ? argv[4] : "dom";
    if (mode == "dom") return Parser::Dom;
    if (mode == "sax") return Parser::Sax;
    throw std::invalid_argument("parser must be dom or sax");
}

Input parse_input(int argc, char** argv) {
    const std::string mode = argc > 5 ? argv[5] : "direct";
    if (mode == "direct") return Input::Direct;
    if (mode == "copy") return Input::Copy;
    throw std::invalid_argument("input must be direct or copy");
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

class BybitLevelsSax final : public nlohmann::json_sax<json> {
public:
    BybitLevelsSax(std::vector<trading::PriceLevel>& bids,
                   std::vector<trading::PriceLevel>& asks)
        : bids_(bids), asks_(asks) {}

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return true; }
    bool end_object() override { return true; }

    bool key(string_t& value) override {
        pending_ = value == "b" ? Side::Bid : value == "a" ? Side::Ask : Side::None;
        return true;
    }

    bool start_array(std::size_t) override {
        if (pending_ != Side::None) {
            active_ = pending_;
            pending_ = Side::None;
            array_depth_ = 1;
            expects_price_ = true;
        } else if (active_ != Side::None) {
            ++array_depth_;
        }
        return true;
    }

    bool end_array() override {
        if (active_ == Side::None) return true;
        if (array_depth_ == 2 && !expects_price_) return false;
        if (--array_depth_ == 0) active_ = Side::None;
        return true;
    }

    bool string(string_t& value) override {
        if (active_ == Side::None || array_depth_ != 2) return true;
        if (expects_price_) {
            if (!trading::parse_decimal_ticks(value, kPriceMultiplier, price_)) return false;
        } else {
            trading::Quantity quantity{};
            if (!trading::parse_decimal_ticks(value, kQuantityMultiplier, quantity)) return false;
            (active_ == Side::Bid ? bids_ : asks_).push_back({price_, quantity});
        }
        expects_price_ = !expects_price_;
        return true;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }

private:
    enum class Side { None, Bid, Ask };
    std::vector<trading::PriceLevel>& bids_;
    std::vector<trading::PriceLevel>& asks_;
    Side pending_{Side::None};
    Side active_{Side::None};
    int array_depth_{0};
    bool expects_price_{true};
    trading::Price price_{};
};

void verify_levels(const std::vector<trading::PriceLevel>& bids,
                   const std::vector<trading::PriceLevel>& asks,
                   std::size_t levels) {
    if (bids.size() != levels || asks.size() != levels ||
        bids.front().price != 1'000'000 || bids.front().qty != 1'000'000 ||
        bids.back().price != static_cast<trading::Price>(1'000'000 - (levels - 1) * 10) ||
        asks.front().price != 1'000'010 || asks.front().qty != 1'000'000) {
        throw std::runtime_error("decoded levels do not match the generated payload");
    }
}

long long percentile(std::vector<long long>& values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * (values.size() - 1))];
}

int main(int argc, char** argv) {
    const std::size_t levels = argc > 1 ? std::stoull(argv[1]) : 1000;
    const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 1000;
    const Conversion conversion = parse_conversion(argc, argv);
    const Parser parser = parse_parser(argc, argv);
    const Input input = parse_input(argc, argv);
    if (parser == Parser::Sax && conversion != Conversion::FixedRef) {
        throw std::invalid_argument("the sax parser is only implemented for fixed-ref conversion");
    }
    const std::size_t warmup = 100;
    json data;
    data["b"] = json::array(); data["a"] = json::array();
    for (std::size_t i = 0; i < levels; ++i) {
        data["b"].push_back({std::to_string(100000 - i), "1.000000"});
        data["a"].push_back({std::to_string(100001 + i), "1.000000"});
    }
    const std::string payload = json{{"type", "snapshot"}, {"data", data}}.dump();
    if (parser == Parser::Sax) {
        std::vector<trading::PriceLevel> bids, asks;
        BybitLevelsSax sax{bids, asks};
        if (!json::sax_parse(payload, &sax)) throw std::runtime_error("invalid Bybit snapshot payload");
        verify_levels(bids, asks, levels);
    }
    trading::MarketDataOrderBook book;
    std::vector<long long> parse, convert, apply, total;
    parse.reserve(iterations); convert.reserve(iterations); apply.reserve(iterations); total.reserve(iterations);
    for (std::size_t n = 0; n < warmup + iterations; ++n) {
        const auto t0 = Clock::now();
        std::vector<trading::PriceLevel> bids, asks;
        bids.reserve(levels); asks.reserve(levels);
        Clock::time_point t1, t2;
        const std::string frame = input == Input::Copy ? payload : std::string{};
        const auto& input_payload = input == Input::Copy ? frame : payload;
        if (parser == Parser::Dom) {
            const json message = json::parse(input_payload);
            t1 = Clock::now();
            for (const auto& level : message.at("data").at("b")) bids.push_back(to_level(level, conversion));
            for (const auto& level : message.at("data").at("a")) asks.push_back(to_level(level, conversion));
            t2 = Clock::now();
        } else {
            BybitLevelsSax sax{bids, asks};
            if (!json::sax_parse(input_payload, &sax)) throw std::runtime_error("invalid Bybit snapshot payload");
            t1 = t2 = Clock::now();
        }
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
    const char* parser_mode = parser == Parser::Dom ? "dom" : "sax";
    const char* input_mode = input == Input::Direct ? "direct" : "copy";
    std::cout << "levels,conversion,parser,input,parse_p50_ns,parse_p99_ns,convert_p50_ns,convert_p99_ns,apply_p50_ns,apply_p99_ns,total_p50_ns,total_p99_ns\n";
    std::cout << levels << ',' << mode << ',' << parser_mode << ',' << input_mode << ',' << percentile(parse,.50) << ',' << percentile(parse,.99) << ',' << percentile(convert,.50) << ',' << percentile(convert,.99) << ',' << percentile(apply,.50) << ',' << percentile(apply,.99) << ',' << percentile(total,.50) << ',' << percentile(total,.99) << '\n';
}
