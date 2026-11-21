#include "orderbook/order.hpp"

#include <gtest/gtest.h>

namespace ob {
namespace {

// One resting buy: 300 shares at price 10025 (integer ticks), the 4th order of the day.
Order make_buy(Sequence seq, Quantity qty = 300) {
  return Order{.id = 17, .side = Side::Buy, .price = 10025, .remaining = qty, .seq = seq};
}

// --- time priority -----------------------------------------------------------

TEST(OrderTest, LowerSequenceArrivedFirst) {
  const Order early = make_buy(4);
  const Order late = make_buy(5);
  EXPECT_TRUE(arrived_before(early, late));
  EXPECT_FALSE(arrived_before(late, early));
}

// An order never comes before itself. std::map/std::sort rely on this (strict ordering).
TEST(OrderTest, OrderDidNotArriveBeforeItself) {
  const Order o = make_buy(4);
  EXPECT_FALSE(arrived_before(o, o));
}

// --- fills ---------------------------------------------------------------------

TEST(OrderTest, PartialFillReducesRemaining) {
  Order o = make_buy(4, 300);
  EXPECT_TRUE(o.fill(100));
  EXPECT_EQ(o.remaining, 200u);
  EXPECT_FALSE(o.is_filled());
}

TEST(OrderTest, ExactFillLeavesOrderFilled) {
  Order o = make_buy(4, 300);
  EXPECT_TRUE(o.fill(300));
  EXPECT_EQ(o.remaining, 0u);
  EXPECT_TRUE(o.is_filled());
}

// The unsigned trap: 300 - 301 on uint32_t is 4'294'967'295, not -1.
// An overfill must be refused and leave the order exactly as it was.
TEST(OrderTest, OverfillIsRejectedAndOrderUnchanged) {
  Order o = make_buy(4, 300);
  EXPECT_FALSE(o.fill(301));
  EXPECT_EQ(o.remaining, 300u);
  EXPECT_FALSE(o.is_filled());
}

}  // namespace
}  // namespace ob
