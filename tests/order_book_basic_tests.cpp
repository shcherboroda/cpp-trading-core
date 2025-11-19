#include <gtest/gtest.h>

#include "trading/order_book.hpp"
#include "trading/types.hpp"

using namespace trading;
constexpr Quantity qty8 = 8;
constexpr Quantity qty12 = 12;

TEST(OrderBookBasic, IsEmptyOnInit) {
    OrderBook book;
    EXPECT_TRUE(book.empty());

    auto bb = book.best_bid();
    auto ba = book.best_ask();

    EXPECT_FALSE(bb.valid);
    EXPECT_FALSE(ba.valid);
}

TEST(OrderBookAddLimit, SingleBidSetsBestBid) {
    OrderBook book;

    auto id = book.add_limit_order(Side::Buy, 100, 10);
    (void)id; // потом ещё проверим, что id можно отменить

    auto bb = book.best_bid();
    auto ba = book.best_ask();

    EXPECT_TRUE(bb.valid);
    EXPECT_EQ(bb.price, 100);
    EXPECT_EQ(bb.qty, 10);

    EXPECT_FALSE(ba.valid);
}

TEST(OrderBookAddLimit, BestBidIsMaxPrice) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 10);
    book.add_limit_order(Side::Buy, 101, 5);

    auto bb = book.best_bid();
    EXPECT_TRUE(bb.valid);
    EXPECT_EQ(bb.price, 101);
    EXPECT_EQ(bb.qty, 5); // или 5+10, если решишь суммировать объём на уровне
}

TEST(OrderBookAddLimit, ExecuteMarketBuyLessThanAvailable) {
    OrderBook book;

    book.add_limit_order(Side::Sell, 100, 10);

    auto result = book.execute_market_order(Side::Buy, qty8);
    EXPECT_EQ(result.requested, qty8);
    EXPECT_EQ(result.filled, qty8);
    EXPECT_EQ(result.remaining, 0);
}

TEST(OrderBookAddLimit, ExecuteMarketBuyMoreThanAvailable) {
    OrderBook book;

    book.add_limit_order(Side::Sell, 100, 10);

    auto result = book.execute_market_order(Side::Buy, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 10);
    EXPECT_EQ(result.remaining, 2);
}

TEST(OrderBookAddLimit, ExecuteMarketBuyFillingMultipleOrders) {
    OrderBook book;

    book.add_limit_order(Side::Sell, 100, 5);
    book.add_limit_order(Side::Sell, 100, 3);

    auto result = book.execute_market_order(Side::Buy, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 8);
    EXPECT_EQ(result.remaining, 4);
    EXPECT_EQ(book.best_ask().valid, false);
}

TEST(OrderBookAddLimit, ExecuteMarketBuyFillingMultipleLayers) {
    OrderBook book;

    book.add_limit_order(Side::Sell, 100, 5);
    book.add_limit_order(Side::Sell, 101, 3);

    auto result = book.execute_market_order(Side::Buy, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 8);
    EXPECT_EQ(result.remaining, 4);
    EXPECT_EQ(book.best_ask().valid, false);
}

TEST(OrderBookAddLimit, ExecuteMarketSellLessThanAvailable) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 10);

    auto result = book.execute_market_order(Side::Sell, qty8);
    EXPECT_EQ(result.requested, qty8);
    EXPECT_EQ(result.filled, qty8);
    EXPECT_EQ(result.remaining, 0);
}

TEST(OrderBookAddLimit, ExecuteMarketSellMoreThanAvailable) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 10);

    auto result = book.execute_market_order(Side::Sell, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 10);
    EXPECT_EQ(result.remaining, 2);
}

TEST(OrderBookAddLimit, ExecuteMarketSellFillingMultipleOrders) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 5);
    book.add_limit_order(Side::Buy, 100, 3);

    auto result = book.execute_market_order(Side::Sell, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 8);
    EXPECT_EQ(result.remaining, 4);
    EXPECT_EQ(book.best_bid().valid, false);
}

TEST(OrderBookAddLimit, ExecuteMarketSellFillingMultipleLayers) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 5);
    book.add_limit_order(Side::Buy, 101, 3);

    auto result = book.execute_market_order(Side::Sell, qty12);
    EXPECT_EQ(result.requested, qty12);
    EXPECT_EQ(result.filled, 8);
    EXPECT_EQ(result.remaining, 4);
    EXPECT_EQ(book.best_bid().valid, false);
}

TEST(OrderBookMarketSell, FillsBestBidFirstAcrossLevels) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 2);
    book.add_limit_order(Side::Buy, 101, 2);

    auto result = book.execute_market_order(Side::Sell, 3);
    EXPECT_EQ(result.requested, 3);
    EXPECT_EQ(result.filled, 3);
    EXPECT_EQ(result.remaining, 0);

    auto bb = book.best_bid();
    EXPECT_TRUE(bb.valid);
    EXPECT_EQ(bb.price, 100);
    EXPECT_EQ(bb.qty, 1);
}

TEST(OrderBookMarketSell, FillsBestAskFirstAcrossLevels) {
    OrderBook book;

    book.add_limit_order(Side::Sell, 100, 2);
    book.add_limit_order(Side::Sell, 101, 2);

    auto result = book.execute_market_order(Side::Buy, 3);
    EXPECT_EQ(result.requested, 3);
    EXPECT_EQ(result.filled, 3);
    EXPECT_EQ(result.remaining, 0);

    auto ba = book.best_ask();
    EXPECT_TRUE(ba.valid);
    EXPECT_EQ(ba.price, 101);
    EXPECT_EQ(ba.qty, 1);
}

TEST(OrderBookCancel, CancelSingleBidMakesBookEmpty) {
    OrderBook book;

    auto id = book.add_limit_order(Side::Buy, 100, 10);

    auto bb_before = book.best_bid();
    EXPECT_TRUE(bb_before.valid);
    EXPECT_EQ(bb_before.price, 100);
    EXPECT_EQ(bb_before.qty, 10);
    EXPECT_TRUE(book.cancel(id));

    auto bb_after = book.best_bid();
    auto ba_after = book.best_ask();
    EXPECT_FALSE(bb_after.valid);
    EXPECT_FALSE(ba_after.valid);
    EXPECT_TRUE(book.empty());
}

TEST(OrderBookCancel, CancelSingleAskMakesBookEmpty) {
    OrderBook book;

    auto id = book.add_limit_order(Side::Sell, 105, 7);

    auto ba_before = book.best_ask();
    EXPECT_TRUE(ba_before.valid);
    EXPECT_EQ(ba_before.price, 105);
    EXPECT_EQ(ba_before.qty, 7);
    EXPECT_TRUE(book.cancel(id));

    auto bb_after = book.best_bid();
    auto ba_after = book.best_ask();
    EXPECT_FALSE(bb_after.valid);
    EXPECT_FALSE(ba_after.valid);
    EXPECT_TRUE(book.empty());
}

TEST(OrderBookCancel, CancelOneOfTwoOnSamePriceKeepsOther) {
    OrderBook book;

    auto id1 = book.add_limit_order(Side::Buy, 100, 2);
    /*auto id2 = */book.add_limit_order(Side::Buy, 100, 3);

    auto bb_before = book.best_bid();
    EXPECT_TRUE(bb_before.valid);
    EXPECT_EQ(bb_before.price, 100);
    EXPECT_EQ(bb_before.qty, 5); // 2 + 3

    EXPECT_TRUE(book.cancel(id1)); // отменяем первый

    auto bb_after = book.best_bid();
    EXPECT_TRUE(bb_after.valid);
    EXPECT_EQ(bb_after.price, 100);
    EXPECT_EQ(bb_after.qty, 3); // остался только второй ордер

    // Дополнительно можно проверить, что повторная отмена id1 вернёт false
    EXPECT_FALSE(book.cancel(id1));
}

TEST(OrderBookCancel, CancelDoesNotChangeOtherSide) {
    OrderBook book;

    auto bid_id = book.add_limit_order(Side::Buy, 100, 2);
    book.add_limit_order(Side::Sell, 105, 4);

    auto ba_before = book.best_ask();
    EXPECT_TRUE(ba_before.valid);
    EXPECT_EQ(ba_before.price, 105);
    EXPECT_EQ(ba_before.qty, 4);

    EXPECT_TRUE(book.cancel(bid_id)); // отменяем bid

    auto ba_after = book.best_ask();
    EXPECT_TRUE(ba_after.valid);
    EXPECT_EQ(ba_after.price, 105);
    EXPECT_EQ(ba_after.qty, 4); // ask не изменился
}

TEST(OrderBookCancel, CancelNonExistingOrderReturnsFalseAndKeepsState) {
    OrderBook book;

    book.add_limit_order(Side::Buy, 100, 2);
    book.add_limit_order(Side::Sell, 105, 4);

    auto bb_before = book.best_bid();
    auto ba_before = book.best_ask();

    EXPECT_FALSE(book.cancel(999999)); // такого id не было

    auto bb_after = book.best_bid();
    auto ba_after = book.best_ask();

    EXPECT_TRUE(bb_after.valid);
    EXPECT_TRUE(ba_after.valid);
    EXPECT_EQ(bb_after.price, bb_before.price);
    EXPECT_EQ(bb_after.qty,   bb_before.qty);
    EXPECT_EQ(ba_after.price, ba_before.price);
    EXPECT_EQ(ba_after.qty,   ba_before.qty);
}
