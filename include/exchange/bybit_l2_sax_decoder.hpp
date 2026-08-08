#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "trading/decimal_ticks.hpp"
#include "trading/types.hpp"

namespace exchange {

struct BybitL2Message {
    std::string type;
    bool topic_matches{};
    std::int64_t ts{};
    std::int64_t cts{};
};

namespace detail {
class BybitL2Sax final : public nlohmann::json_sax<nlohmann::json> {
public:
    BybitL2Sax(BybitL2Message& message, std::vector<trading::PriceLevel>& bids,
               std::vector<trading::PriceLevel>& asks, std::int64_t price_scale,
               std::int64_t quantity_scale, std::string_view expected_topic)
        : message_(message), bids_(bids), asks_(asks), price_scale_(price_scale), quantity_scale_(quantity_scale), expected_topic_(expected_topic) {}
    bool null() override { return true; } bool boolean(bool) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; } bool start_object(std::size_t) override { return true; }
    bool end_object() override { return true; }
    bool key(string_t& key) override { key_ = key == "b" ? Key::Bids : key == "a" ? Key::Asks : key == "topic" ? Key::Topic : key == "type" ? Key::Type : key == "ts" ? Key::Ts : key == "cts" ? Key::Cts : Key::Other; return true; }
    bool start_array(std::size_t) override { if (key_ == Key::Bids || key_ == Key::Asks) { side_ = key_ == Key::Bids ? Side::Bid : Side::Ask; depth_ = 1; expect_price_ = true; key_ = Key::Other; } else if (side_ != Side::None) ++depth_; return true; }
    bool end_array() override { if (side_ == Side::None) return true; if (depth_ == 2 && !expect_price_) return false; if (--depth_ == 0) side_ = Side::None; return true; }
    bool string(string_t& value) override { if (side_ != Side::None && depth_ == 2) { if (expect_price_) { if (!trading::parse_decimal_ticks(value, price_scale_, price_)) return false; } else { trading::Quantity quantity{}; if (!trading::parse_decimal_ticks(value, quantity_scale_, quantity)) return false; (side_ == Side::Bid ? bids_ : asks_).push_back({price_, quantity}); } expect_price_ = !expect_price_; return true; } if (key_ == Key::Topic) message_.topic_matches = value == expected_topic_; else if (key_ == Key::Type) message_.type = value; key_ = Key::Other; return true; }
    bool number_integer(number_integer_t value) override { return number(value); }
    bool number_unsigned(number_unsigned_t value) override { return number(static_cast<std::int64_t>(value)); }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
private:
    enum class Key { Other, Bids, Asks, Topic, Type, Ts, Cts }; enum class Side { None, Bid, Ask };
    bool number(std::int64_t value) { if (key_ == Key::Ts) message_.ts = value; else if (key_ == Key::Cts) message_.cts = value; key_ = Key::Other; return true; }
    BybitL2Message& message_; std::vector<trading::PriceLevel>& bids_; std::vector<trading::PriceLevel>& asks_; std::int64_t price_scale_, quantity_scale_; std::string_view expected_topic_; Key key_{Key::Other}; Side side_{Side::None}; int depth_{}; bool expect_price_{true}; trading::Price price_{};
};
} // namespace detail

inline bool decode_bybit_l2(std::string_view payload, std::int64_t price_scale,
                            std::int64_t quantity_scale, BybitL2Message& message,
                            std::vector<trading::PriceLevel>& bids,
                            std::vector<trading::PriceLevel>& asks,
                            std::string_view expected_topic) {
    message = {}; bids.clear(); asks.clear();
    detail::BybitL2Sax sax{message, bids, asks, price_scale, quantity_scale, expected_topic};
    return nlohmann::json::sax_parse(payload, &sax);
}
} // namespace exchange
