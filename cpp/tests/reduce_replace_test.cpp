#include "orderbook/book.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob {
namespace {

Order buy(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Buy, .price = price, .remaining = qty, .seq = 0};
}
Order sell(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Sell, .price = price, .remaining = qty, .seq = 0};
}

using Ids = std::vector<OrderId>;

// Three buys queued at one price: #1 first, #2 second, #3 third.
OrderBook three_buys_at_10000() {
  OrderBook book;
  EXPECT_TRUE(book.add(buy(1, 10000, 500)));
  EXPECT_TRUE(book.add(buy(2, 10000, 300)));
  EXPECT_TRUE(book.add(buy(3, 10000, 200)));
  return book;
}

// --- reduce: ITCH E / C (executed) and X (partial cancel) -------------------------

// The whole point of reduce vs cancel+add: #2 is still second in line.
TEST(ReduceTest, KeepsQueuePosition) {
  OrderBook book = three_buys_at_10000();
  ASSERT_TRUE(book.reduce(2, 100));
  EXPECT_EQ(book.queue_at(Side::Buy, 10000), (Ids{1, 2, 3}));
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 900u);  // 500 + 200 + 200
}

TEST(ReduceTest, ToZeroRemovesTheOrder) {
  OrderBook book = three_buys_at_10000();
  ASSERT_TRUE(book.reduce(1, 500));
  EXPECT_EQ(book.queue_at(Side::Buy, 10000), (Ids{2, 3}));
  EXPECT_EQ(book.order_count(), 2u);
  EXPECT_FALSE(book.reduce(1, 1));  // gone, so it can't be reduced again
}

// Same rule as cancel (D10): an empty level must not stay behind as the best bid.
TEST(ReduceTest, LastOrderAtLevelRemovesTheLevel) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10000, 500)));
  ASSERT_TRUE(book.add(buy(2, 9999, 100)));
  ASSERT_TRUE(book.reduce(1, 500));
  EXPECT_EQ(book.best_bid(), 9999);
  EXPECT_EQ(book.level_count(Side::Buy), 1u);
}

// In replay this means our bookkeeping is off; refuse and leave the order as it was.
TEST(ReduceTest, MoreThanRemainingIsRejectedAndBookUnchanged) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.reduce(2, 301));
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 1000u);
}

TEST(ReduceTest, UnknownIdIsRejected) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.reduce(42, 100));
}

TEST(ReduceTest, ZeroIsRejected) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.reduce(2, 0));
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 1000u);
}

// --- replace: ITCH U ---------------------------------------------------------------

TEST(ReplaceTest, MovesToNewPriceUnderNewId) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10000, 500)));
  ASSERT_TRUE(book.replace(1, 9, 200, 10001));

  EXPECT_EQ(book.best_bid(), 10001);
  EXPECT_EQ(book.queue_at(Side::Buy, 10001), (Ids{9}));
  EXPECT_EQ(book.volume_at(Side::Buy, 10001), 200u);
  EXPECT_TRUE(book.queue_at(Side::Buy, 10000).empty());  // old level removed
  EXPECT_FALSE(book.cancel(1));                          // old id is gone
  EXPECT_EQ(book.order_count(), 1u);
}

// Even at the same price, a replaced order goes to the back of the queue.
TEST(ReplaceTest, SamePriceStillLosesPriority) {
  OrderBook book = three_buys_at_10000();
  ASSERT_TRUE(book.replace(1, 9, 500, 10000));
  EXPECT_EQ(book.queue_at(Side::Buy, 10000), (Ids{2, 3, 9}));
}

// ITCH U doesn't repeat the side; the new order keeps the old one's.
TEST(ReplaceTest, KeepsTheSide) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(1, 10005, 500)));
  ASSERT_TRUE(book.replace(1, 9, 500, 10004));
  EXPECT_EQ(book.best_ask(), 10004);
  EXPECT_EQ(book.level_count(Side::Buy), 0u);
}

TEST(ReplaceTest, UnknownOldIdIsRejected) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.replace(42, 9, 100, 10000));
  EXPECT_EQ(book.order_count(), 3u);
}

// Checked before anything changes: the old order must still be there afterwards.
TEST(ReplaceTest, NewIdAlreadyInBookIsRejectedAndBookUnchanged) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.replace(1, 2, 100, 10001));
  EXPECT_EQ(book.queue_at(Side::Buy, 10000), (Ids{1, 2, 3}));
  EXPECT_FALSE(book.best_bid() == 10001);
}

TEST(ReplaceTest, ZeroQuantityIsRejectedAndBookUnchanged) {
  OrderBook book = three_buys_at_10000();
  EXPECT_FALSE(book.replace(1, 9, 0, 10000));
  EXPECT_EQ(book.queue_at(Side::Buy, 10000), (Ids{1, 2, 3}));
}

}  // namespace
}  // namespace ob
