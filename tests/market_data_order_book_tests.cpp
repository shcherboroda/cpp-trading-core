#include <gtest/gtest.h>

#include "trading/flat_market_data_order_book.hpp"
#include "trading/market_data_order_book.hpp"

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
