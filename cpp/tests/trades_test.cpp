#include "orderbook/sim/trades.hpp"

#include "itch_builders.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

using namespace itch_test;  // message builders: add(), executed(), ...

constexpr std::uint16_t kAapl = 13;

std::optional<Trade> trade(const Bytes& b, const Replayer& replay) {
  return trade_of(b.data(), b.size(), replay);
}

// A resting bid #7, 300 @ 1.0000.
Replayer book_with_a_bid() {
  Replayer replay;
  const Bytes a = add(kAapl, 7, 'B', 300, 10'000);
  replay.apply(a.data(), a.size());
  return replay;
}

// E carries no price: the trade is at the resting order's price.
TEST(TradesTest, ExecutionTradesAtTheRestingPrice) {
  const Replayer replay = book_with_a_bid();
  const std::optional<Trade> t = trade(executed(kAapl, 7, 100, 5'000), replay);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->timestamp_ns, 5'000u);
  EXPECT_EQ(t->locate, kAapl);
  EXPECT_EQ(t->price, 10'000);
  EXPECT_EQ(t->shares, 100u);
}

TEST(TradesTest, ExecutionWithPriceUsesItsOwnPrice) {
  const Replayer replay = book_with_a_bid();
  const std::optional<Trade> t = trade(executed_with_price(kAapl, 7, 200, 5'000, 9'990), replay);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->price, 9'990);
  EXPECT_EQ(t->shares, 200u);
}

// Not printable: Nasdaq counted these shares elsewhere; counting them here doubles them.
TEST(TradesTest, NonPrintableExecutionIsNotATrade) {
  const Replayer replay = book_with_a_bid();
  EXPECT_FALSE(trade(executed_with_price(kAapl, 7, 200, 5'000, 9'990, 'N'), replay));
}

// P: a hidden order traded. Nothing on the book, but real volume.
TEST(TradesTest, HiddenOrderTradeCounts) {
  const Replayer replay;
  const std::optional<Trade> t = trade(non_cross_trade(kAapl, 'S', 500, 10'010, 6'000), replay);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->price, 10'010);
  EXPECT_EQ(t->shares, 500u);
  EXPECT_EQ(t->timestamp_ns, 6'000u);
}

TEST(TradesTest, OrderMessagesAreNotTrades) {
  const Replayer replay = book_with_a_bid();
  EXPECT_FALSE(trade(add(kAapl, 8, 'S', 100, 10'010), replay));
  EXPECT_FALSE(trade(cancelled(kAapl, 7, 100), replay));
  EXPECT_FALSE(trade(deleted(kAapl, 7), replay));
}

// An E for an order the book doesn't have: no price to give it, so not counted.
TEST(TradesTest, ExecutionOfAnUnknownOrderIsSkipped) {
  const Replayer replay;
  EXPECT_FALSE(trade(executed(kAapl, 99, 100), replay));
}

}  // namespace
}  // namespace ob::sim
