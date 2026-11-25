#include "orderbook/sim/turn_reads.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kSecond = 1'000'000'000ULL;
constexpr std::uint64_t k15s = 15 * kSecond;
constexpr std::uint64_t k1031 = (10 * 3600 + 31 * 60) * kSecond;  // 10:31:00

// The 15 s bar number i after 10:31:00, prices in ticks ($0.0001; bars keep them doubled).
Bar bar(std::uint64_t i, Price open, Price high, Price low, Price close) {
  return Bar{.start_ns = k1031 + i * k15s,
             .end_ns = k1031 + (i + 1) * k15s,
             .open_x2 = 2 * open,
             .high_x2 = 2 * high,
             .low_x2 = 2 * low,
             .close_x2 = 2 * close,
             .volume = 0};
}

// The plan's example: a swing high at $276.40, a 1 min ATR of 35 c (3,500 ticks), so a
// poke of at most 875 ticks (8.75 c).
const std::vector<Level> kHigh = {Level{LevelKind::SwingHigh, 2'764'000}};
constexpr double kAtr = 3'500;

// A flat bar j of the 8 before 10:31:00 (j = 0 to 7): high = low = open, so it can't poke
// anything, but it fills the 2 minutes of history a poke must break out of.
Bar flat_before(std::uint64_t j, Price price) {
  const std::uint64_t start = k1031 - (8 - j) * k15s;
  return Bar{.start_ns = start, .end_ns = start + k15s, .open_x2 = 2 * price,
             .high_x2 = 2 * price, .low_x2 = 2 * price, .close_x2 = 2 * price, .volume = 0};
}

// Feeds 8 flat bars at the first bar's open (unless warm is false), then the bars, the same
// levels each time; returns what each of the bars (not the flat ones) read.
std::vector<int> feed(FailedBreakout& b1, const std::vector<Bar>& bars,
                      const std::vector<Level>& levels = kHigh,
                      std::optional<double> atr = kAtr, bool warm = true) {
  for (std::uint64_t j = 0; warm && j < 8; ++j) {
    b1.on_bar(flat_before(j, bars.front().open_x2 / 2), levels, atr);
  }
  std::vector<int> reads;
  for (const Bar& b : bars) {
    reads.push_back(b1.on_bar(b, levels, atr));
  }
  return reads;
}

// The table from the plan: approach, poke 6 c above, close back below the next bar.
TEST(FailedBreakoutTest, ThePlansExample) {
  FailedBreakout b1;
  const auto reads = feed(b1, {bar(0, 2'763'000, 2'763'800, 2'762'800, 2'763'600),
                               bar(1, 2'763'600, 2'764'600, 2'763'500, 2'764'400),
                               bar(2, 2'764'400, 2'764'500, 2'763'100, 2'763'300)});
  EXPECT_EQ(reads, (std::vector<int>{0, 0, -1}));
}

// The read lasts 45 s from the end of the bar that completed it (10:31:45), then stops.
// Before that bar ended nothing was known.
TEST(FailedBreakoutTest, HeldFor45Seconds) {
  FailedBreakout b1;
  feed(b1, {bar(1, 2'763'600, 2'764'600, 2'763'500, 2'764'400),
            bar(2, 2'764'400, 2'764'500, 2'763'100, 2'763'300)});
  const std::uint64_t known = k1031 + 3 * k15s;
  EXPECT_EQ(b1.direction(known - 1), 0);
  EXPECT_EQ(b1.direction(known), -1);
  EXPECT_EQ(b1.direction(known + 45 * kSecond - 1), -1);
  EXPECT_EQ(b1.direction(known + 45 * kSecond), 0);
}

// One bar can do it all: poke above and close back below (a long upper tail at the level).
TEST(FailedBreakoutTest, PokeAndCloseBackInOneBar) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'600, 2'763'500, 2'763'700)}),
            (std::vector<int>{-1}));
}

// Mirror: a poke below a support that closes back above reads up.
TEST(FailedBreakoutTest, FailedBreakdownReadsUp) {
  FailedBreakout b1;
  const std::vector<Level> low = {Level{LevelKind::SwingLow, 2'764'000}};
  EXPECT_EQ(feed(b1, {bar(0, 2'764'400, 2'764'500, 2'763'400, 2'763'600),
                      bar(1, 2'763'600, 2'764'300, 2'763'500, 2'764'200)}, low),
            (std::vector<int>{0, 1}));
}

// A poke of 10 c is more than a quarter ATR: a real breakout, not a trap.
TEST(FailedBreakoutTest, TooFarIsARealBreakout) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'765'000, 2'763'500, 2'764'400),
                      bar(1, 2'764'400, 2'764'500, 2'763'100, 2'763'300)}),
            (std::vector<int>{0, 0}));
}

// The poke was small, but the next bar runs further than a quarter ATR past the level:
// the breakout worked, and a later close back below is something else.
TEST(FailedBreakoutTest, FollowThroughCancels) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'600, 2'763'500, 2'764'400),
                      bar(1, 2'764'400, 2'765'200, 2'764'300, 2'765'000),
                      bar(2, 2'765'000, 2'765'100, 2'763'000, 2'763'200)}),
            (std::vector<int>{0, 0, 0}));
}

// Back below on the third bar after the poke is too late: the trap is a quick turn.
TEST(FailedBreakoutTest, TooLateToBeATrap) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'600, 2'763'500, 2'764'400),
                      bar(1, 2'764'400, 2'764'500, 2'764'100, 2'764'300),
                      bar(2, 2'764'300, 2'764'500, 2'764'100, 2'764'200),
                      bar(3, 2'764'200, 2'764'300, 2'763'000, 2'763'200)}),
            (std::vector<int>{0, 0, 0, 0}));
}

// A bar that opens above the level was already past it: it can't poke it.
TEST(FailedBreakoutTest, MustComeFromTheOtherSide) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'764'100, 2'764'600, 2'763'500, 2'763'700)}),
            (std::vector<int>{0}));
}

// No ATR yet (warming up): no yardstick, no read.
TEST(FailedBreakoutTest, NothingWithoutAnAtr) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'600, 2'763'500, 2'763'700)}, kHigh, std::nullopt),
            (std::vector<int>{0}));
}

// A wide bar trapping both sides at once (an upper tail past one level, a lower tail past
// another, closing between them) says nothing: the reads cancel.
TEST(FailedBreakoutTest, TrapsBothWaysCancel) {
  FailedBreakout b1;
  const std::vector<Level> both = {Level{LevelKind::DayLow, 2'760'000},
                                   Level{LevelKind::SwingHigh, 2'764'000}};
  EXPECT_EQ(feed(b1, {bar(0, 2'762'000, 2'764'500, 2'759'500, 2'762'000)}, both),
            (std::vector<int>{0}));
}

// Calibrated (D63): only swing and day highs and lows are breakout levels. Round prices,
// VWAP, the EMA and the day's open sit where the price wanders back and forth all day;
// as B1 levels they made a "trap" of a quarter of all 15 s bars.
TEST(FailedBreakoutTest, OnlySwingAndDayLevels) {
  const Bar tail = bar(0, 2'763'600, 2'764'600, 2'763'500, 2'763'700);
  for (const LevelKind kind :
       {LevelKind::Round, LevelKind::Vwap, LevelKind::Ema, LevelKind::DayOpen}) {
    FailedBreakout b1;
    EXPECT_EQ(feed(b1, {tail}, {Level{kind, 2'764'000}}), (std::vector<int>{0}));
  }
  FailedBreakout day;
  EXPECT_EQ(feed(day, {tail}, {Level{LevelKind::DayHigh, 2'764'000}}), (std::vector<int>{-1}));
}

// A breakout attempt is the price trying a new high (low): the poke must go past every bar
// of the last 2 minutes. Here a bar 15 s earlier went higher (too far past the level to be
// a poke itself), so the later small poke is a lower high, not an attempt.
TEST(FailedBreakoutTest, MustBeANewTwoMinuteExtreme) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'900, 2'763'400, 2'763'500),
                      bar(1, 2'763'500, 2'764'600, 2'763'400, 2'763'700)}),
            (std::vector<int>{0, 0}));
}

// Fewer than 8 bars seen: no 2 minutes to break out of yet.
TEST(FailedBreakoutTest, NeedsTwoMinutesOfBars) {
  FailedBreakout b1;
  EXPECT_EQ(feed(b1, {bar(0, 2'763'600, 2'764'600, 2'763'500, 2'763'700)}, kHigh, kAtr, false),
            (std::vector<int>{0}));
}

// ---- B2, the pullback ends ----

// The plan's example: an uptrend, the 1 min EMA 20 at $276.20, the same 35 c ATR (a touch
// is within a fifth of it, 700 ticks). Three bars pull back; the fourth dips to 1 c above the EMA and
// closes up near its high.
const std::vector<Level> kEma = {Level{LevelKind::Ema, 2'762'000}};
std::vector<Bar> pullback(Price last_low = 2'762'100, Price last_close = 2'763'000) {
  return {bar(0, 2'764'500, 2'764'800, 2'764'000, 2'764'100),
          bar(1, 2'764'100, 2'764'200, 2'763'200, 2'763'300),
          bar(2, 2'763'300, 2'763'400, 2'762'600, 2'762'600),
          bar(3, 2'762'600, 2'763'200, last_low, last_close)};
}

std::vector<int> feed(PullbackEnd& b2, const std::vector<Bar>& bars, int trend = 1,
                      const std::vector<Level>& levels = kEma,
                      std::optional<double> atr = kAtr) {
  std::vector<int> reads;
  for (const Bar& b : bars) {
    reads.push_back(b2.on_bar(b, trend, levels, atr));
  }
  return reads;
}

TEST(PullbackEndTest, ThePlansExample) {
  PullbackEnd b2;
  EXPECT_EQ(feed(b2, pullback()), (std::vector<int>{0, 0, 0, 1}));
}

TEST(PullbackEndTest, HeldFor45Seconds) {
  PullbackEnd b2;
  feed(b2, pullback());
  const std::uint64_t known = k1031 + 4 * k15s;  // bar 3 ends at 10:32:00
  EXPECT_EQ(b2.direction(known - 1), 0);
  EXPECT_EQ(b2.direction(known), 1);
  EXPECT_EQ(b2.direction(known + 45 * kSecond), 0);
}

// No trend on the long chart (EMAs mixed): a dip to the EMA is just a range bar.
TEST(PullbackEndTest, NeedsATrend) {
  PullbackEnd b2;
  EXPECT_EQ(feed(b2, pullback(), 0), (std::vector<int>{0, 0, 0, 0}));
}

// In a downtrend the same bull bar is a counter-trend bounce, not B2.
TEST(PullbackEndTest, OnlyWithTheTrend) {
  PullbackEnd b2;
  EXPECT_EQ(feed(b2, pullback(), -1), (std::vector<int>{0, 0, 0, 0}));
}

// The dip must be the lowest of the last 4 bars: a higher low isn't where a pullback ends.
TEST(PullbackEndTest, MustBeTheLowestOfTheLastFour) {
  PullbackEnd b2;
  auto bars = pullback();
  bars[1].low_x2 = 2 * 2'762'000;  // bar 1 went lower than bar 3
  EXPECT_EQ(feed(b2, bars), (std::vector<int>{0, 0, 0, 0}));
}

// Fewer than 4 bars seen: not enough to call it the lowest.
TEST(PullbackEndTest, NeedsFourBars) {
  PullbackEnd b2;
  const auto bars = pullback();
  EXPECT_EQ(feed(b2, {bars[2], bars[3]}), (std::vector<int>{0, 0}));
}

// 8 c above the EMA: more than a fifth of an ATR (7 c) away, it didn't reach it. 6 c does
// (calibrated, D63: a tenth, 3.5 c, let through about 9 reads a day).
TEST(PullbackEndTest, MustReachTheLevel) {
  PullbackEnd far;
  EXPECT_EQ(feed(far, pullback(), 1, {Level{LevelKind::Ema, 2'761'300}}),
            (std::vector<int>{0, 0, 0, 0}));
  PullbackEnd near;
  EXPECT_EQ(feed(near, pullback(), 1, {Level{LevelKind::Ema, 2'761'500}}).back(), 1);
}

// Closing in the middle of its range is no turn.
TEST(PullbackEndTest, MustCloseStrong) {
  PullbackEnd b2;
  EXPECT_EQ(feed(b2, pullback(2'762'100, 2'762'700)), (std::vector<int>{0, 0, 0, 0}));
}

// A small bull bar that touched the level from below and closed under it: the level held
// against it, the pullback didn't end there.
TEST(PullbackEndTest, MustCloseAboveTheLevel) {
  PullbackEnd b2;
  auto bars = pullback();
  bars[3] = bar(3, 2'762'150, 2'762'350, 2'762'100, 2'762'300);
  EXPECT_EQ(feed(b2, bars, 1, {Level{LevelKind::Ema, 2'762'400}}),
            (std::vector<int>{0, 0, 0, 0}));
}

// VWAP is the other level; a round price or a swing at the same place doesn't count for B2.
TEST(PullbackEndTest, VwapCountsOtherLevelsDont) {
  PullbackEnd vwap;
  EXPECT_EQ(feed(vwap, pullback(), 1, {Level{LevelKind::Vwap, 2'762'000}}).back(), 1);
  PullbackEnd round;
  EXPECT_EQ(feed(round, pullback(), 1, {Level{LevelKind::Round, 2'762'000},
                                        Level{LevelKind::SwingLow, 2'762'000}}).back(), 0);
}

// Mirror: a downtrend, a rally up to VWAP, a bear bar closing near its low below it.
TEST(PullbackEndTest, DowntrendReadsDown) {
  PullbackEnd b2;
  const std::vector<Bar> bars = {bar(0, 2'759'500, 2'760'000, 2'759'200, 2'759'900),
                                 bar(1, 2'759'900, 2'760'800, 2'759'800, 2'760'700),
                                 bar(2, 2'760'700, 2'761'400, 2'760'600, 2'761'400),
                                 bar(3, 2'761'400, 2'761'900, 2'760'800, 2'761'000)};
  EXPECT_EQ(feed(b2, bars, -1, {Level{LevelKind::Vwap, 2'762'000}}),
            (std::vector<int>{0, 0, 0, -1}));
}

TEST(PullbackEndTest, NothingWithoutAnAtr) {
  PullbackEnd b2;
  EXPECT_EQ(feed(b2, pullback(), 1, kEma, std::nullopt), (std::vector<int>{0, 0, 0, 0}));
}

// The long chart's trend from the EMA stack 9 / 20 / 50.
TEST(EmaStackTest, StackedMixedOrWarmingUp) {
  EXPECT_EQ(ema_stack(ChartPoint{.ema_fast = 3, .ema_mid = 2, .ema_long = 1}), 1);
  EXPECT_EQ(ema_stack(ChartPoint{.ema_fast = 1, .ema_mid = 2, .ema_long = 3}), -1);
  EXPECT_EQ(ema_stack(ChartPoint{.ema_fast = 3, .ema_mid = 1, .ema_long = 2}), 0);
  EXPECT_EQ(ema_stack(ChartPoint{.ema_fast = 3, .ema_mid = 2}), 0);  // EMA 50 warming up
}

}  // namespace
}  // namespace ob::sim
