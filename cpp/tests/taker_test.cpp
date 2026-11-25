#include "orderbook/sim/taker.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMs = 1'000'000;
constexpr std::uint64_t kSecond = 1'000 * kMs;
constexpr std::uint64_t k1000 = 10 * 3600 * kSecond;  // 10:00:00

// A book around $276.30: asks 60 @ 276.31 and 100 @ 276.32, bids 300 @ 276.30.
OrderBook book() {
  OrderBook b;
  EXPECT_TRUE(b.add(Order{.id = 1, .side = Side::Sell, .price = 2'763'100, .remaining = 60, .seq = 0}));
  EXPECT_TRUE(b.add(Order{.id = 2, .side = Side::Sell, .price = 2'763'200, .remaining = 100, .seq = 0}));
  EXPECT_TRUE(b.add(Order{.id = 3, .side = Side::Buy, .price = 2'763'000, .remaining = 300, .seq = 0}));
  return b;
}

// Buying 100 takes the 60 at the best ask, then 40 at the next tick: 276.314 on average.
TEST(SweepTest, BestPriceFirstThenTheNextTick) {
  const auto p = sweep_price(book(), Side::Buy, 100, 100);
  ASSERT_TRUE(p);
  EXPECT_DOUBLE_EQ(*p, (60.0 * 2'763'100 + 40.0 * 2'763'200) / 100);
}

TEST(SweepTest, SellingHitsTheBids) {
  EXPECT_DOUBLE_EQ(*sweep_price(book(), Side::Sell, 100, 100), 2'763'000);
}

// Nothing to take, or not enough within reach: no fill (never a made-up price).
TEST(SweepTest, EmptyOrThinSide) {
  OrderBook empty;
  EXPECT_FALSE(sweep_price(empty, Side::Buy, 100, 100));
  EXPECT_FALSE(sweep_price(book(), Side::Buy, 1'000, 100, 5));
}

// The plan's trade: B1 reads down at 10:00:00; 100 ms later we sell 100 at the bid; two
// minutes after that we send the buy-back, which arrives 100 ms later and pays the ask.
TEST(TakerBotTest, EntersAfterTheLatencyHoldsThenExits) {
  TakerBot bot;
  const OrderBook b = book();
  bot.on_read(k1000, -1);
  bot.before_message(k1000 + 50 * kMs, b);  // not there yet
  EXPECT_TRUE(bot.trades().empty());
  bot.before_message(k1000 + 150 * kMs, b);  // the first message after it arrived
  bot.before_message(k1000 + 100 * kMs + 120 * kSecond + 50 * kMs, b);  // exit not there yet
  EXPECT_TRUE(bot.trades().empty());
  bot.before_message(k1000 + 100 * kMs + 120 * kSecond + 150 * kMs, b);
  ASSERT_EQ(bot.trades().size(), 1u);
  const TakerTrade& t = bot.trades()[0];
  EXPECT_EQ(t.direction, -1);
  EXPECT_EQ(t.signal_ns, k1000);
  EXPECT_EQ(t.entry_ns, k1000 + 100 * kMs);  // when the order arrived, not the next message
  EXPECT_DOUBLE_EQ(t.entry_price, 2'763'000);
  EXPECT_EQ(t.exit_ns, k1000 + 200 * kMs + 120 * kSecond);
  EXPECT_DOUBLE_EQ(t.exit_price, (60.0 * 2'763'100 + 40.0 * 2'763'200) / 100);
  EXPECT_DOUBLE_EQ(t.pnl_ticks(), 2'763'000 - t.exit_price);  // short: sold high, bought back
}

// While a position is open (or being entered), new reads are ignored.
TEST(TakerBotTest, OnePositionAtATime) {
  TakerBot bot;
  const OrderBook b = book();
  bot.on_read(k1000, 1);
  bot.on_read(k1000 + 50 * kMs, -1);  // still entering
  bot.before_message(k1000 + 200 * kMs, b);
  bot.on_read(k1000 + 60 * kSecond, -1);  // holding
  bot.before_message(k1000 + 130 * kSecond, b);
  ASSERT_EQ(bot.trades().size(), 1u);
  EXPECT_EQ(bot.trades()[0].direction, 1);
  bot.on_read(k1000 + 131 * kSecond, -1);  // flat again: taken
  bot.before_message(k1000 + 132 * kSecond, b);
  bot.finish(k1000 + 133 * kSecond, b);
  EXPECT_EQ(bot.trades().size(), 2u);
}

// Only reads inside [from, to): 10:30:00 itself is out (09:30-10:30 by default).
TEST(TakerBotTest, OnlyInsideTheWindow) {
  TakerBot bot;
  const OrderBook b = book();
  bot.on_read((10 * 3600 + 30 * 60) * kSecond, 1);
  bot.on_read(9 * 3600 * kSecond, 1);
  bot.on_read(k1000, 0);  // no direction
  bot.before_message(11 * 3600 * kSecond, b);
  bot.finish(12 * 3600 * kSecond, b);
  EXPECT_TRUE(bot.trades().empty());
}

// The data ends while holding: closed against the book as it is then. An entry that never
// reached the book is dropped.
TEST(TakerBotTest, FinishClosesWhatIsOpen) {
  TakerBot holding;
  const OrderBook b = book();
  holding.on_read(k1000, 1);
  holding.before_message(k1000 + 200 * kMs, b);
  holding.finish(k1000 + 10 * kSecond, b);
  ASSERT_EQ(holding.trades().size(), 1u);
  EXPECT_EQ(holding.trades()[0].exit_ns, k1000 + 10 * kSecond);
  EXPECT_DOUBLE_EQ(holding.trades()[0].exit_price, 2'763'000);  // sold at the bid

  TakerBot entering;
  entering.on_read(k1000, 1);
  entering.finish(k1000 + 50 * kMs, b);
  EXPECT_TRUE(entering.trades().empty());
}

TEST(TakerCsvTest, OneRowPerTrade) {
  const TakerTrade t{.direction = -1, .signal_ns = 5, .entry_ns = 6, .entry_price = 2'763'000,
                     .exit_ns = 7, .exit_price = 2'763'140.5, .quantity = 100};
  std::ostringstream out;
  write_taker_csv(out, {t});
  EXPECT_EQ(out.str(),
            "signal_ns,direction,entry_ns,entry_price,exit_ns,exit_price,quantity\n"
            "5,-1,6,2763000.00,7,2763140.50,100\n");
}

}  // namespace
}  // namespace ob::sim
