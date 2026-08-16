#pragma once

#include <charconv>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "trading/decimal_ticks.hpp"
#include "trading/types.hpp"

namespace exchange {

struct BybitL2Message {
    std::string type;
    std::string topic_copy;
    bool topic_matches{};
    std::int64_t ts{};
    std::int64_t cts{};
};
struct BybitL2DecodeDiagnostics { std::int64_t level_arrays_ns{}; };

namespace detail {
class BybitL2Sax final : public nlohmann::json_sax<nlohmann::json> {
public:
    BybitL2Sax(BybitL2Message& message, std::vector<trading::PriceLevel>& bids,
               std::vector<trading::PriceLevel>& asks, std::int64_t price_scale,
               std::int64_t quantity_scale, std::string_view expected_topic, bool copy_topic, BybitL2DecodeDiagnostics* diagnostics)
        : message_(message), bids_(bids), asks_(asks), price_scale_(price_scale), quantity_scale_(quantity_scale), expected_topic_(expected_topic), copy_topic_(copy_topic), diagnostics_(diagnostics) {}
    bool null() override { return true; } bool boolean(bool) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; } bool start_object(std::size_t) override { return true; }
    bool end_object() override { return true; }
    bool key(string_t& key) override { key_ = key == "b" ? Key::Bids : key == "a" ? Key::Asks : key == "topic" ? Key::Topic : key == "type" ? Key::Type : key == "ts" ? Key::Ts : key == "cts" ? Key::Cts : Key::Other; return true; }
    bool start_array(std::size_t) override { if (key_ == Key::Bids || key_ == Key::Asks) { side_ = key_ == Key::Bids ? Side::Bid : Side::Ask; depth_ = 1; expect_price_ = true; key_ = Key::Other; if (diagnostics_) arrays_start_ = Clock::now(); } else if (side_ != Side::None) ++depth_; return true; }
    bool end_array() override { if (side_ == Side::None) return true; if (depth_ == 2 && !expect_price_) return false; if (--depth_ == 0) { if (diagnostics_) diagnostics_->level_arrays_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - arrays_start_).count(); side_ = Side::None; } return true; }
    bool string(string_t& value) override { if (side_ != Side::None && depth_ == 2) { if (expect_price_) { if (!trading::parse_decimal_ticks(value, price_scale_, price_)) return false; } else { trading::Quantity quantity{}; if (!trading::parse_decimal_ticks(value, quantity_scale_, quantity)) return false; (side_ == Side::Bid ? bids_ : asks_).push_back({price_, quantity}); } expect_price_ = !expect_price_; return true; } if (key_ == Key::Topic) { message_.topic_matches = value == expected_topic_; if (copy_topic_) message_.topic_copy = value; } else if (key_ == Key::Type) message_.type = value; key_ = Key::Other; return true; }
    bool number_integer(number_integer_t value) override { return number(value); }
    bool number_unsigned(number_unsigned_t value) override { return number(static_cast<std::int64_t>(value)); }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
private:
    enum class Key { Other, Bids, Asks, Topic, Type, Ts, Cts }; enum class Side { None, Bid, Ask };
    bool number(std::int64_t value) { if (key_ == Key::Ts) message_.ts = value; else if (key_ == Key::Cts) message_.cts = value; key_ = Key::Other; return true; }
    using Clock = std::chrono::steady_clock; BybitL2Message& message_; std::vector<trading::PriceLevel>& bids_; std::vector<trading::PriceLevel>& asks_; std::int64_t price_scale_, quantity_scale_; std::string_view expected_topic_; bool copy_topic_; BybitL2DecodeDiagnostics* diagnostics_; Clock::time_point arrays_start_{}; Key key_{Key::Other}; Side side_{Side::None}; int depth_{}; bool expect_price_{true}; trading::Price price_{};
};
} // namespace detail

inline bool decode_bybit_l2(std::string_view payload, std::int64_t price_scale,
                            std::int64_t quantity_scale, BybitL2Message& message,
                            std::vector<trading::PriceLevel>& bids,
                            std::vector<trading::PriceLevel>& asks,
                            std::string_view expected_topic, bool copy_topic = false, BybitL2DecodeDiagnostics* diagnostics = nullptr) {
    message = {}; bids.clear(); asks.clear();
    if (diagnostics) {
        *diagnostics = {};
    }
    detail::BybitL2Sax sax{message, bids, asks, price_scale, quantity_scale, expected_topic, copy_topic, diagnostics};
    return nlohmann::json::sax_parse(payload, &sax);
}

inline bool decode_bybit_l2_scanner(std::string_view text, std::vector<trading::PriceLevel>& bids, std::vector<trading::PriceLevel>& asks) {
    auto parse_side = [&](std::string_view key, std::vector<trading::PriceLevel>& out) {
        const auto found = text.find(key);
        if (found == std::string_view::npos) { out.clear(); return true; }
        std::size_t p = found + key.size();
        out.clear();
        while (p < text.size() && text[p] != ']') {
            if (text.compare(p, 2, "[\"") != 0) return false;
            p += 2; const auto pe = text.find('"', p);
            if (pe == std::string_view::npos) return false;
            const auto price = text.substr(p, pe - p); p = pe + 1;
            if (text.compare(p, 2, ",\"") != 0) return false;
            p += 2; const auto qe = text.find('"', p);
            if (qe == std::string_view::npos) return false;
            trading::Price px{}; trading::Quantity qty{};
            if (!trading::parse_decimal_ticks(price, 10, px) || !trading::parse_decimal_ticks(text.substr(p, qe - p), 1'000'000, qty)) return false;
            out.push_back({px, qty}); p = qe + 1;
            if (text.compare(p, 1, "]") != 0) return false;
            ++p;
            if (p < text.size() && text[p] == ',') ++p;
        } return p < text.size();
    };
    return parse_side("\"b\":[", bids) && parse_side("\"a\":[", asks);
}

namespace detail {
inline void skip_json_space(std::string_view text, std::size_t& pos) noexcept {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
}

inline bool member_value(std::string_view text, std::string_view key, std::size_t& pos,
                         std::size_t from = 0) noexcept {
    const auto found = text.find(key, from);
    if (found == std::string_view::npos) return false;
    pos = found + key.size(); skip_json_space(text, pos);
    if (pos == text.size() || text[pos++] != ':') return false;
    skip_json_space(text, pos);
    return pos < text.size();
}

inline bool quoted_string(std::string_view text, std::size_t& pos, std::string_view& value) noexcept {
    if (pos == text.size() || text[pos++] != '"') return false;
    const auto end = text.find('"', pos);
    if (end == std::string_view::npos) return false;
    value = text.substr(pos, end - pos); pos = end + 1;
    return true;
}

inline bool integer(std::string_view text, std::size_t& pos, std::int64_t& value) noexcept {
    const auto first = text.data() + pos;
    const auto last = text.data() + text.size();
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc{}) return false;
    pos = static_cast<std::size_t>(end - text.data());
    return true;
}

inline bool bounded_levels(std::string_view text, std::string_view key, std::size_t from,
                           std::int64_t price_scale, std::int64_t quantity_scale,
                           std::vector<trading::PriceLevel>& out) noexcept {
    std::size_t pos{};
    if (!member_value(text, key, pos, from) || text[pos++] != '[') return false;
    out.clear(); skip_json_space(text, pos);
    while (pos < text.size() && text[pos] != ']') {
        if (text[pos++] != '[') return false;
        skip_json_space(text, pos); std::string_view price, quantity;
        if (!quoted_string(text, pos, price)) return false;
        skip_json_space(text, pos); if (pos == text.size() || text[pos++] != ',') return false;
        skip_json_space(text, pos); if (!quoted_string(text, pos, quantity)) return false;
        skip_json_space(text, pos); if (pos == text.size() || text[pos++] != ']') return false;
        trading::Price parsed_price{}; trading::Quantity parsed_quantity{};
        if (!trading::parse_decimal_ticks(price, price_scale, parsed_price) ||
            !trading::parse_decimal_ticks(quantity, quantity_scale, parsed_quantity)) return false;
        out.push_back({parsed_price, parsed_quantity});
        skip_json_space(text, pos);
        if (pos < text.size() && text[pos] == ',') { ++pos; skip_json_space(text, pos); }
        else if (pos == text.size() || text[pos] != ']') return false;
    }
    return pos < text.size() && text[pos] == ']';
}
} // namespace detail

// Experimental bounded decoder for Bybit's documented orderbook payload schema.
// It intentionally handles only unescaped string fields and numeric timestamps.
inline bool decode_bybit_l2_bounded(std::string_view payload, std::int64_t price_scale,
                                    std::int64_t quantity_scale, BybitL2Message& message,
                                    std::vector<trading::PriceLevel>& bids,
                                    std::vector<trading::PriceLevel>& asks,
                                    std::string_view expected_topic) noexcept {
    message = {}; bids.clear(); asks.clear();
    std::size_t pos{}; std::string_view topic;
    if (!detail::member_value(payload, "\"topic\"", pos)) return true;
    if (!detail::quoted_string(payload, pos, topic)) return false;
    message.topic_matches = topic == expected_topic;
    if (!message.topic_matches) return true;

    std::string_view type;
    if (!detail::member_value(payload, "\"type\"", pos) || !detail::quoted_string(payload, pos, type)) return false;
    if (type != "snapshot" && type != "delta") return false;
    message.type.assign(type);

    if (detail::member_value(payload, "\"ts\"", pos) && !detail::integer(payload, pos, message.ts)) return false;
    if (detail::member_value(payload, "\"cts\"", pos) && !detail::integer(payload, pos, message.cts)) return false;
    if (!detail::member_value(payload, "\"data\"", pos) || payload[pos] != '{') return false;
    return detail::bounded_levels(payload, "\"b\"", pos, price_scale, quantity_scale, bids) &&
           detail::bounded_levels(payload, "\"a\"", pos, price_scale, quantity_scale, asks);
}
} // namespace exchange
