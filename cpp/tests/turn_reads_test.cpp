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

// Feeds bars in order, the same levels each time; returns what each one read.
std::vector<int> feed(FailedBreakout& b1, const std::vector<Bar>& bars,
                      const std::vector<Level>& levels = kHigh,
                      std::optional<double> atr = kAtr) {
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
  const std::vector<Level> low = {Level{LevelKind::Vwap, 2'764'000}};
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
  const std::vector<Level> both = {Level{LevelKind::Round, 2'760'000},
                                   Level{LevelKind::SwingHigh, 2'764'000}};
  EXPECT_EQ(feed(b1, {bar(0, 2'762'000, 2'764'500, 2'759'500, 2'762'000)}, both),
            (std::vector<int>{0}));
}

}  // namespace
}  // namespace ob::sim
