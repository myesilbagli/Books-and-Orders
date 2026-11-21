#include "orderbook/book.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob {
namespace {

// seq is left at 0: the book stamps its own arrival counter on every order it accepts.
Order buy(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Buy, .price = price, .remaining = qty, .seq = 0};
}
Order sell(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Sell, .price = price, .remaining = qty, .seq = 0};
}

using Ids = std::vector<OrderId>;

// --- empty book ----------------------------------------------------------------

TEST(BookTest, EmptyBookHasNoBestPrices) {
  const OrderBook book;
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.order_count(), 0u);
}

// --- adding ----------------------------------------------------------------------

TEST(BookTest, BestBidIsHighestBuy) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(buy(2, 10002, 200)));
  ASSERT_TRUE(book.add(buy(3, 9999, 100)));
  EXPECT_EQ(book.best_bid(), 10002);
}

TEST(BookTest, BestAskIsLowestSell) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(1, 10005, 400)));
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  ASSERT_TRUE(book.add(sell(3, 10004, 300)));
  EXPECT_EQ(book.best_ask(), 10003);
}

// Time priority: at one price, the order that arrived first is first in the queue.
TEST(BookTest, SamePriceQueuesInArrivalOrder) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(buy(4, 10001, 100)));
  EXPECT_EQ(book.queue_at(Side::Buy, 10001), (Ids{1, 4}));
  EXPECT_EQ(book.volume_at(Side::Buy, 10001), 600u);
  EXPECT_EQ(book.level_count(Side::Buy), 1u);
}

// Bids and asks are separate: a buy and a sell at the same price don't share a queue.
TEST(BookTest, SidesAreKeptApart) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  EXPECT_EQ(book.queue_at(Side::Buy, 10001), (Ids{1}));
  EXPECT_TRUE(book.queue_at(Side::Sell, 10001).empty());
  EXPECT_EQ(book.order_count(), 2u);
}

TEST(BookTest, DuplicateIdIsRejected) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  EXPECT_FALSE(book.add(sell(1, 10003, 100)));
  EXPECT_EQ(book.order_count(), 1u);
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(BookTest, ZeroQuantityIsRejected) {
  OrderBook book;
  EXPECT_FALSE(book.add(buy(1, 10001, 0)));
  EXPECT_EQ(book.order_count(), 0u);
  EXPECT_FALSE(book.best_bid().has_value());
}

// --- cancelling ------------------------------------------------------------------

// The case std::list is for: removing from the middle keeps everyone else in order.
TEST(BookTest, CancelMiddleOrderKeepsQueueOrder) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(buy(4, 10001, 100)));
  ASSERT_TRUE(book.add(buy(5, 10001, 300)));
  EXPECT_TRUE(book.cancel(4));
  EXPECT_EQ(book.queue_at(Side::Buy, 10001), (Ids{1, 5}));
  EXPECT_EQ(book.volume_at(Side::Buy, 10001), 800u);
  EXPECT_EQ(book.order_count(), 2u);
}

// An empty price level must disappear, otherwise best_bid would point at a price with no orders.
TEST(BookTest, CancelLastOrderAtPriceRemovesLevel) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(buy(7, 10002, 200)));
  EXPECT_TRUE(book.cancel(7));
  EXPECT_EQ(book.best_bid(), 10001);
  EXPECT_EQ(book.level_count(Side::Buy), 1u);
}

TEST(BookTest, CancelUnknownIdIsRejected) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  EXPECT_FALSE(book.cancel(999));
  EXPECT_EQ(book.order_count(), 1u);
}

TEST(BookTest, CancelTwiceIsRejected) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  EXPECT_TRUE(book.cancel(2));
  EXPECT_FALSE(book.cancel(2));
  EXPECT_FALSE(book.best_ask().has_value());
}

}  // namespace
}  // namespace ob
