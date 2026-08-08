#include <gtest/gtest.h>

#include "trading/flat_market_data_order_book.hpp"
#include "trading/market_data_order_book.hpp"
#include "trading/decimal_ticks.hpp"
#include "exchange/bybit_l2_sax_decoder.hpp"

using trading::FlatMarketDataOrderBook;
using trading::MarketDataOrderBook;
using trading::Price;
using trading::PriceLevel;
using trading::Quantity;

template <typename Book>
void expect_snapshot_replacement(Book& book)
{
    book.apply_snapshot({{100, 4}, {99, 3}}, {{101, 5}, {102, 2}});
    book.apply_snapshot({{98, 7}}, {{103, 6}});

    Price price{};
    Quantity quantity{};
    ASSERT_TRUE(book.best_bid(price, quantity));
    EXPECT_EQ(price, 98);
    EXPECT_EQ(quantity, 7);
    ASSERT_TRUE(book.best_ask(price, quantity));
    EXPECT_EQ(price, 103);
    EXPECT_EQ(quantity, 6);
}

template <typename Book>
void expect_delta_semantics(Book& book)
{
    book.apply_snapshot({{100, 4}, {99, 3}}, {{101, 5}, {102, 2}});
    book.apply_delta({{100, 9}, {99, 0}, {98, 7}}, {{101, 0}, {103, 6}});

    Price price{};
    Quantity quantity{};
    ASSERT_TRUE(book.best_bid(price, quantity));
    EXPECT_EQ(price, 100);
    EXPECT_EQ(quantity, 9);
    ASSERT_TRUE(book.best_ask(price, quantity));
    EXPECT_EQ(price, 102);
    EXPECT_EQ(quantity, 2);
}

TEST(MarketDataOrderBook, SnapshotReplacesStaleLevels)
{
    MarketDataOrderBook book;
    expect_snapshot_replacement(book);
}

TEST(MarketDataOrderBook, DeltaUpdatesInsertsAndErasesLevels)
{
    MarketDataOrderBook book;
    expect_delta_semantics(book);
}

TEST(FlatMarketDataOrderBook, SnapshotReplacesStaleLevels)
{
    FlatMarketDataOrderBook book;
    expect_snapshot_replacement(book);
}

TEST(FlatMarketDataOrderBook, DeltaUpdatesInsertsAndErasesLevels)
{
    FlatMarketDataOrderBook book;
    expect_delta_semantics(book);
}

TEST(DecimalTicks, ParsesBybitDecimalStrings)
{
    std::int64_t value{};
    EXPECT_TRUE(trading::parse_decimal_ticks("30247.20", 10, value));
    EXPECT_EQ(value, 302472);
    EXPECT_TRUE(trading::parse_decimal_ticks("0.006", 1'000'000, value));
    EXPECT_EQ(value, 6000);
}

TEST(DecimalTicks, MatchesLlroundForExtraDecimalPlaces)
{
    std::int64_t value{};
    EXPECT_TRUE(trading::parse_decimal_ticks("1.26", 10, value));
    EXPECT_EQ(value, 13);
    EXPECT_TRUE(trading::parse_decimal_ticks("-1.26", 10, value));
    EXPECT_EQ(value, -13);
}

TEST(BybitL2SaxDecoder, DecodesSnapshotAndDelta)
{
    exchange::BybitL2Message message;
    std::vector<PriceLevel> bids, asks;
    EXPECT_TRUE(exchange::decode_bybit_l2(
        R"({"topic":"orderbook.50.BTCUSDT","type":"snapshot","ts":7,"data":{"b":[["100.1","1.000000"]],"a":[["100.2","2.000000"]]}})",
        10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT"));
    EXPECT_EQ(message.type, "snapshot");
    ASSERT_EQ(bids.size(), 1U); ASSERT_EQ(asks.size(), 1U);
    EXPECT_EQ(bids[0].price, 1001); EXPECT_EQ(asks[0].qty, 2'000'000);
    EXPECT_TRUE(exchange::decode_bybit_l2(
        R"({"topic":"orderbook.50.BTCUSDT","type":"delta","cts":9,"data":{"b":[["100.1","0"]],"a":[]}})",
        10, 1'000'000, message, bids, asks, "orderbook.50.BTCUSDT"));
    EXPECT_EQ(message.type, "delta"); EXPECT_EQ(message.cts, 9);
    ASSERT_EQ(bids.size(), 1U); EXPECT_EQ(bids[0].qty, 0);
}
