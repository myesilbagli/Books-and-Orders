#include "orderbook/sim/market_state.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMs = 1'000'000;
constexpr std::uint64_t kSecond = 1'000 * kMs;
constexpr std::uint64_t kMinute = 60 * kSecond;

// Charts from 1 s to 2 minutes, VWAP from 3 s: small numbers, same rules as the real day.
MarketStateConfig config() {
  return MarketStateConfig{.from_ns = kSecond, .to_ns = 2 * kMinute, .vwap_from_ns = 3 * kSecond};
}

Trade trade(std::uint64_t t, Price price, Quantity shares) {
  return Trade{.timestamp_ns = t, .locate = 13, .price = price, .shares = shares};
}

// Both timeframes close their bars on advance(), and hand them back.
TEST(MarketStateTest, AdvanceClosesBothTimeframes) {
  MarketState m(config());
  m.on_mid(1'200 * kMs, 20'000);
  const ClosedBars seconds = m.advance(3 * kSecond);
  EXPECT_EQ(seconds.one_second.size(), 2u);  // [1 s, 2 s) and [2 s, 3 s)
  EXPECT_TRUE(seconds.one_minute.empty());
  const ClosedBars minute = m.advance(kMinute);
  EXPECT_EQ(minute.one_minute.size(), 1u);  // [0, 1 min), started by the mid at 1.2 s
  ASSERT_TRUE(m.one_second().latest());
  ASSERT_TRUE(m.one_minute().latest());
}

// Before from_ns, trades don't count and no bar exists. But the mid in force when the
// window opens does: the first bar opens at it, at from_ns, not at the next change.
TEST(MarketStateTest, OpensAtTheMidInForceAtTheStart) {
  MarketState m(config());
  m.on_mid(500 * kMs, 30'000);                // before the window, still in force at 1 s
  m.on_trade(trade(600 * kMs, 15'000, 100));  // before the window: not counted
  m.on_mid(1'500 * kMs, 20'000);
  const ClosedBars closed = m.advance(2 * kSecond);
  ASSERT_EQ(closed.one_second.size(), 1u);
  EXPECT_EQ(closed.one_second[0].bar.start_ns, kSecond);
  EXPECT_EQ(closed.one_second[0].bar.open_x2, 30'000);
  EXPECT_EQ(closed.one_second[0].bar.close_x2, 20'000);
  EXPECT_EQ(closed.one_second[0].bar.volume, 0u);
}

// Same when nothing moves after the start: the carried mid makes flat bars from from_ns.
TEST(MarketStateTest, AQuietStartStillHasBars) {
  MarketState m(config());
  m.on_mid(500 * kMs, 30'000);
  const ClosedBars closed = m.advance(3 * kSecond);
  ASSERT_EQ(closed.one_second.size(), 2u);  // [1 s, 2 s) and [2 s, 3 s)
  EXPECT_EQ(closed.one_second[0].bar.start_ns, kSecond);
  EXPECT_EQ(closed.one_second[1].bar.close_x2, 30'000);
}

// VWAP counts from its own start; bar volume counts every trade in the window.
TEST(MarketStateTest, VwapStartsAtItsOwnTime) {
  MarketState m(config());
  m.on_mid(1'000 * kMs, 20'000);
  m.on_trade(trade(2'000 * kMs, 9'000, 100));  // before VWAP's start: volume only
  m.on_trade(trade(3'000 * kMs, 10'000, 100));
  m.on_trade(trade(3'500 * kMs, 10'010, 300));
  EXPECT_DOUBLE_EQ(*m.vwap().value(), (10'000.0 * 100 + 10'010.0 * 300) / 400);
  const ClosedBars closed = m.advance(4 * kSecond);
  ASSERT_EQ(closed.one_second.size(), 3u);
  EXPECT_EQ(closed.one_second[1].bar.volume, 100u);  // [2 s, 3 s)
  EXPECT_EQ(closed.one_second[2].bar.volume, 400u);  // [3 s, 4 s)
}

// At to_ns the charts stop: later events are ignored and the last bars close at to_ns.
TEST(MarketStateTest, StopsAtTheEnd) {
  MarketState m(config());
  m.on_mid(kMinute + 59 * kSecond + 500 * kMs, 20'000);  // in the last second
  m.on_mid(2 * kMinute + kSecond, 30'000);                // after the end: ignored
  const ClosedBars closed = m.advance(3 * kMinute);
  ASSERT_EQ(closed.one_second.size(), 1u);
  EXPECT_EQ(closed.one_second[0].bar.close_x2, 20'000);
  EXPECT_EQ(closed.one_minute.size(), 1u);  // [1 min, 2 min)
  EXPECT_TRUE(m.advance(4 * kMinute).one_second.empty());  // closed once, not again
}

}  // namespace
}  // namespace ob::sim
