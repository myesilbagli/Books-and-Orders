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

// Checks one trade field by field, so a failure says exactly which field is wrong.
void expect_trade(const Trade& t, OrderId aggressor, OrderId resting, Price price, Quantity qty) {
  EXPECT_EQ(t.aggressor_id, aggressor);
  EXPECT_EQ(t.resting_id, resting);
  EXPECT_EQ(t.price, price);
  EXPECT_EQ(t.quantity, qty);
}

// --- no cross ------------------------------------------------------------------

TEST(MatchTest, NonCrossingBuyJustRests) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  const SubmitResult r = book.submit(buy(20, 10002, 100));
  EXPECT_TRUE(r.accepted);
  EXPECT_TRUE(r.trades.empty());
  EXPECT_EQ(book.best_bid(), 10002);
  EXPECT_EQ(book.best_ask(), 10003);
}

// --- price-time priority ----------------------------------------------------------

// The example from the explanation: #2 was first in the queue, so it fills first.
TEST(MatchTest, BuyFillsQueueInArrivalOrder) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  ASSERT_TRUE(book.add(sell(9, 10003, 300)));

  const SubmitResult r = book.submit(buy(20, 10003, 250));

  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 2u);
  expect_trade(r.trades[0], 20, 2, 10003, 100);
  expect_trade(r.trades[1], 20, 9, 10003, 150);

  EXPECT_EQ(book.queue_at(Side::Sell, 10003), (Ids{9}));  // #9 keeps its place with 150 left
  EXPECT_EQ(book.volume_at(Side::Sell, 10003), 150u);
  EXPECT_FALSE(book.cancel(2));             // fully filled orders are gone from the book
  EXPECT_FALSE(book.best_bid().has_value());  // the buy was fully filled, nothing rests
}

// The resting order set its price first, so the trade happens at the resting price.
// The aggressor was willing to pay 100.05 and gets 100.03: price improvement.
TEST(MatchTest, TradeHappensAtRestingPrice) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  const SubmitResult r = book.submit(buy(20, 10005, 100));
  ASSERT_EQ(r.trades.size(), 1u);
  expect_trade(r.trades[0], 20, 2, 10003, 100);
}

// Price priority beats time priority: the better price fills first, even if it arrived later.
TEST(MatchTest, BuySweepsLevelsBestPriceFirst) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(3, 10004, 200)));  // arrived first, worse price
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));  // arrived later, better price

  const SubmitResult r = book.submit(buy(20, 10004, 250));

  ASSERT_EQ(r.trades.size(), 2u);
  expect_trade(r.trades[0], 20, 2, 10003, 100);
  expect_trade(r.trades[1], 20, 3, 10004, 150);
  EXPECT_EQ(book.best_ask(), 10004);
  EXPECT_EQ(book.volume_at(Side::Sell, 10004), 50u);
  EXPECT_EQ(book.level_count(Side::Sell), 1u);  // the emptied 100.03 level is gone
}

// The limit price is a hard stop; whatever doesn't fill rests on the book.
TEST(MatchTest, StopsAtLimitAndRestsRemainder) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  ASSERT_TRUE(book.add(sell(3, 10005, 200)));

  const SubmitResult r = book.submit(buy(20, 10004, 300));

  ASSERT_EQ(r.trades.size(), 1u);
  expect_trade(r.trades[0], 20, 2, 10003, 100);
  EXPECT_EQ(book.best_bid(), 10004);
  EXPECT_EQ(book.queue_at(Side::Buy, 10004), (Ids{20}));
  EXPECT_EQ(book.volume_at(Side::Buy, 10004), 200u);
  EXPECT_EQ(book.best_ask(), 10005);  // never crossed: best bid < best ask
}

// Same rules mirrored: a sell hits the highest bids first.
TEST(MatchTest, SellHitsHighestBidsFirst) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(buy(7, 10002, 200)));

  const SubmitResult r = book.submit(sell(30, 10001, 600));

  ASSERT_EQ(r.trades.size(), 2u);
  expect_trade(r.trades[0], 30, 7, 10002, 200);
  expect_trade(r.trades[1], 30, 1, 10001, 400);
  EXPECT_EQ(book.best_bid(), 10001);
  EXPECT_EQ(book.volume_at(Side::Buy, 10001), 100u);
  EXPECT_FALSE(book.best_ask().has_value());
}

// --- rejects -------------------------------------------------------------------------

TEST(MatchTest, SubmitRejectsZeroQuantity) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  const SubmitResult r = book.submit(buy(20, 10003, 0));
  EXPECT_FALSE(r.accepted);
  EXPECT_TRUE(r.trades.empty());
  EXPECT_EQ(book.volume_at(Side::Sell, 10003), 100u);
}

// A duplicate id must be refused before any matching, or it would trade and then fail to rest.
TEST(MatchTest, SubmitRejectsDuplicateIdBeforeTrading) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(1, 10001, 500)));
  ASSERT_TRUE(book.add(sell(2, 10003, 100)));
  const SubmitResult r = book.submit(buy(1, 10003, 100));
  EXPECT_FALSE(r.accepted);
  EXPECT_TRUE(r.trades.empty());
  EXPECT_EQ(book.volume_at(Side::Sell, 10003), 100u);
}

}  // namespace
}  // namespace ob
