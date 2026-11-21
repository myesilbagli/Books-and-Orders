#include "orderbook/sim/queue.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

Order buy(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Buy, .price = price, .remaining = qty, .seq = 0};
}
Order sell(OrderId id, Price price, Quantity qty) {
  return Order{.id = id, .side = Side::Sell, .price = price, .remaining = qty, .seq = 0};
}

// The queue from the explanation, at bid 10000:  #11 (300) -> #12 (200) -> [US 100]
// and then #13 (500) arrives behind us.
struct Scenario {
  OrderBook book;
  VirtualOrder us{};

  Scenario() {
    EXPECT_TRUE(book.add(buy(11, 10'000, 300)));
    EXPECT_TRUE(book.add(buy(12, 10'000, 200)));
    us = place(book, Side::Buy, 10'000, 100);
    EXPECT_TRUE(book.add(buy(13, 10'000, 500)));
  }

  // What the simulator will do for every ITCH execution: ask first, then let the book
  // apply it. The order must be looked up BEFORE the book changes (it may disappear).
  Quantity execute(OrderId id, Quantity shares) {
    const Order* resting = book.find(id);
    EXPECT_NE(resting, nullptr);
    const Quantity filled = fill_from_execution(us, *resting, shares);
    EXPECT_TRUE(book.reduce(id, shares));
    return filled;
  }
};

// --- where we stand in the queue -------------------------------------------------------

TEST(SimQueueTest, PlacedBehindEverythingAlreadyResting) {
  Scenario s;
  EXPECT_EQ(shares_ahead(s.book, s.us), 500u);  // #11 + #12; #13 came later
}

TEST(SimQueueTest, EmptyLevelMeansFrontOfTheQueue) {
  OrderBook book;
  ASSERT_TRUE(book.add(buy(11, 10'000, 300)));
  const VirtualOrder us = place(book, Side::Buy, 10'001, 100);
  EXPECT_EQ(shares_ahead(book, us), 0u);
}

TEST(SimQueueTest, CancelsAheadMoveUsUp) {
  Scenario s;
  ASSERT_TRUE(s.book.reduce(11, 200));  // X: partial cancel
  EXPECT_EQ(shares_ahead(s.book, s.us), 300u);
  ASSERT_TRUE(s.book.cancel(12));  // D
  EXPECT_EQ(shares_ahead(s.book, s.us), 100u);
}

// A replace loses priority (D19): the order ahead of us goes behind us.
TEST(SimQueueTest, ReplacedOrderAheadGoesBehindUs) {
  Scenario s;
  ASSERT_TRUE(s.book.replace(11, 21, 300, 10'000));
  EXPECT_EQ(shares_ahead(s.book, s.us), 200u);  // only #12 is ahead now
  EXPECT_EQ(s.execute(12, 200), 0u);
  EXPECT_EQ(s.execute(21, 100), 100u);  // #21 is behind us, so the seller reached us first
}

// --- fills ---------------------------------------------------------------------------

// Executions of orders ahead of us just move us up.
TEST(SimQueueTest, ExecutionAheadDoesNotFillUs) {
  Scenario s;
  EXPECT_EQ(s.execute(11, 300), 0u);
  EXPECT_EQ(s.execute(12, 200), 0u);
  EXPECT_EQ(shares_ahead(s.book, s.us), 0u);
  EXPECT_EQ(s.us.remaining, 100u);
}

// The key rule: an order BEHIND us traded, so the seller would have hit us first.
TEST(SimQueueTest, ExecutionBehindUsFillsUs) {
  Scenario s;
  EXPECT_EQ(s.execute(13, 100), 100u);
  EXPECT_EQ(s.us.remaining, 0u);
}

// Each execution that passes us gives us at most its own size.
TEST(SimQueueTest, SmallExecutionBehindFillsUsPartly) {
  Scenario s;
  EXPECT_EQ(s.execute(13, 30), 30u);
  EXPECT_EQ(s.us.remaining, 70u);
  EXPECT_EQ(s.execute(13, 100), 70u);  // only what we have left
  EXPECT_EQ(s.us.remaining, 0u);
}

TEST(SimQueueTest, FilledOrderTakesNothingMore) {
  Scenario s;
  ASSERT_EQ(s.execute(13, 100), 100u);
  EXPECT_EQ(s.execute(13, 100), 0u);
}

// A seller traded BELOW our bid: walking down from the best bid, they'd have hit us first.
TEST(SimQueueTest, TradeThroughOurPriceFillsUs) {
  Scenario s;
  ASSERT_TRUE(s.book.add(buy(14, 9'999, 400)));
  EXPECT_EQ(s.execute(14, 100), 100u);
}

// A trade at a BETTER bid than ours doesn't reach us.
TEST(SimQueueTest, TradeAtBetterPriceDoesNotFillUs) {
  Scenario s;
  ASSERT_TRUE(s.book.add(buy(15, 10'001, 400)));
  EXPECT_EQ(s.execute(15, 100), 0u);
}

// Our bid is filled by sellers only; an ask being lifted has nothing to do with it.
TEST(SimQueueTest, OppositeSideExecutionDoesNotFillUs) {
  Scenario s;
  ASSERT_TRUE(s.book.add(sell(16, 9'000, 400)));  // even at a "through" price
  EXPECT_EQ(s.execute(16, 100), 0u);
}

// The same rules, mirrored, for an ask: behind us at our price, or a buyer paying more.
TEST(SimQueueTest, AskSideMirrors) {
  OrderBook book;
  ASSERT_TRUE(book.add(sell(31, 10'010, 200)));
  VirtualOrder us = place(book, Side::Sell, 10'010, 100);
  ASSERT_TRUE(book.add(sell(32, 10'010, 200)));
  ASSERT_TRUE(book.add(sell(33, 10'011, 200)));
  ASSERT_TRUE(book.add(sell(34, 10'009, 200)));

  EXPECT_EQ(shares_ahead(book, us), 200u);
  EXPECT_EQ(fill_from_execution(us, *book.find(34), 50), 0u);  // better ask: not us
  EXPECT_EQ(fill_from_execution(us, *book.find(31), 50), 0u);  // ahead of us
  EXPECT_EQ(fill_from_execution(us, *book.find(32), 40), 40u);  // behind us
  EXPECT_EQ(fill_from_execution(us, *book.find(33), 80), 60u);  // through our price
  EXPECT_EQ(us.remaining, 0u);
}

}  // namespace
}  // namespace ob::sim
