#include "orderbook/sim/structure.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMinute = 60'000'000'000ULL;

// A 1 minute bar number i with its high and low in ticks (bars keep the doubled mid).
Bar bar(std::uint64_t i, Price high, Price low) {
  return Bar{.start_ns = i * kMinute,
             .end_ns = (i + 1) * kMinute,
             .open_x2 = 2 * low,
             .high_x2 = 2 * high,
             .low_x2 = 2 * low,
             .close_x2 = 2 * high,
             .volume = 0};
}

constexpr double kDelta = 2 * 10.0;  // a 10-tick pullback confirms a swing (doubled, like prices)

// Feeds bars of (high, low), numbered from 0, and returns the swings each one confirmed.
std::vector<Swing> feed(ZigZag& z, const std::vector<std::pair<Price, Price>>& bars,
                        double delta = kDelta) {
  std::vector<Swing> confirmed;
  for (std::size_t i = 0; i < bars.size(); ++i) {
    if (const auto s = z.on_bar(bar(i, bars[i].first, bars[i].second), delta)) {
      confirmed.push_back(*s);
    }
  }
  return confirmed;
}

// The example from the round 2 plan: low 100, high 130, low 118 (higher), high 145 (higher).
const std::vector<std::pair<Price, Price>> kUptrend = {
    {100, 100}, {130, 120}, {128, 118}, {145, 125}, {140, 132}};

TEST(ZigZagTest, ConfirmsEachSwingOnceThePriceHasPulledBackDelta) {
  ZigZag z;
  const std::vector<Swing> s = feed(z, kUptrend);
  ASSERT_EQ(s.size(), 4u);
  // The low at bar 0 is confirmed by bar 1, which rose 30 from it.
  EXPECT_EQ(s[0].kind, SwingKind::Low);
  EXPECT_EQ(s[0].price_x2, 200);
  EXPECT_EQ(s[0].time_ns, 0u);              // the bar that made the low
  EXPECT_EQ(s[0].confirmed_ns, 2 * kMinute);  // the end of the bar that confirmed it
  // The high at bar 1 (130) is confirmed by bar 2's low, 12 below it.
  EXPECT_EQ(s[1].kind, SwingKind::High);
  EXPECT_EQ(s[1].price_x2, 260);
  EXPECT_EQ(s[1].time_ns, kMinute);
  EXPECT_EQ(s[1].confirmed_ns, 3 * kMinute);
  EXPECT_FALSE(s[1].change);  // the first high has nothing to compare with
  // Then a higher low (118 > 100) and a higher high (145 > 130).
  EXPECT_EQ(s[2].price_x2, 236);
  EXPECT_EQ(s[2].change, 1);
  EXPECT_EQ(s[3].price_x2, 290);
  EXPECT_EQ(s[3].change, 1);
}

// Higher highs and higher lows: +2. Not before two of each exist.
TEST(ZigZagTest, HigherHighsAndHigherLowsScorePlusTwo) {
  ZigZag z;
  feed(z, {{100, 100}, {130, 120}, {128, 118}, {145, 125}});  // the second high isn't confirmed yet
  EXPECT_FALSE(z.score());
  z.on_bar(bar(4, 140, 132), kDelta);
  EXPECT_EQ(z.score(), 2);
}

// The mirror image: lower highs and lower lows score -2.
TEST(ZigZagTest, LowerHighsAndLowerLowsScoreMinusTwo) {
  ZigZag z;
  feed(z, {{200, 200}, {180, 170}, {182, 172}, {160, 155}, {168, 158}});
  EXPECT_EQ(z.score(), -2);
}

// The founder's rule: the uptrend ends at the first lower high (range, 0), and only a
// lower low after it makes it a downtrend (-2). Breaking the last low alone doesn't.
TEST(ZigZagTest, ALowerHighEndsTheUptrendAndALowerLowStartsTheDowntrend) {
  ZigZag z;
  feed(z, kUptrend);  // ... high 145 confirmed by bar 4; now heading down from 132
  ASSERT_EQ(z.score(), 2);
  z.on_bar(bar(5, 138, 110), kDelta);  // falls below the last low (118): no swing yet
  EXPECT_EQ(z.score(), 2);
  z.on_bar(bar(6, 125, 112), kDelta);  // up 15 from 110: low 110 confirmed, a lower low
  EXPECT_EQ(z.score(), 0);             // higher high, lower low: mixed
  z.on_bar(bar(7, 130, 120), kDelta);  // a high of 130, below 145...
  z.on_bar(bar(8, 126, 105), kDelta);  // ...confirmed by a 25 drop: a lower high
  EXPECT_EQ(z.score(), -2);
}

// A pullback smaller than delta is noise: no swing, the leg goes on.
TEST(ZigZagTest, ASmallPullbackIsNotASwing) {
  ZigZag z;
  feed(z, {{100, 100}, {130, 120}, {129, 121}});  // 9 below the high
  ASSERT_EQ(z.swings().size(), 1u);  // only the first low
}

// A higher high moves the candidate: the pullback is measured from the new high, and the
// swing is dated at the bar that made it.
TEST(ZigZagTest, ANewExtremeMovesTheCandidate) {
  ZigZag z;
  feed(z, {{100, 100}, {130, 120}, {135, 128}, {134, 124}});  // 11 below 135
  ASSERT_EQ(z.swings().size(), 2u);
  EXPECT_EQ(z.swings()[1].price_x2, 270);
  EXPECT_EQ(z.swings()[1].time_ns, 2 * kMinute);
}

// Without a delta (no ATR yet) the extremes are tracked but nothing is confirmed.
TEST(ZigZagTest, NoDeltaNoSwings) {
  ZigZag z;
  feed(z, {{100, 100}, {130, 120}, {128, 100}}, 0.0);
  EXPECT_TRUE(z.swings().empty());
}

// MarketStructure: ATR(2), delta = 1.5 x ATR. Two 10-tick bars make ATR 10 (delta 15);
// during the warm-up nothing is confirmed, however far the price moves.
TEST(MarketStructureTest, DeltaFollowsTheAtr) {
  MarketStructure m(StructureSettings{.atr_n = 2, .c = 1.5});
  m.on_bar(bar(0, 110, 100));
  EXPECT_FALSE(m.atr());
  m.on_bar(bar(1, 110, 100));
  ASSERT_TRUE(m.atr());
  EXPECT_DOUBLE_EQ(*m.atr(), 10.0);
  EXPECT_TRUE(m.swings().empty());  // 10 up and down, less than 15
  // Bar 2 jumps to 120-130: true range 20 from the last close (110), so ATR is
  // (10 + 20) / 2 = 15 and delta 22.5. The rise from the low of 100 is 30: a swing low.
  m.on_bar(bar(2, 130, 120));
  ASSERT_EQ(m.swings().size(), 1u);
  EXPECT_EQ(m.swings()[0].kind, SwingKind::Low);
  EXPECT_EQ(m.swings()[0].price_x2, 200);
}

TEST(MarketStructureTest, NothingIsConfirmedWhileTheAtrWarmsUp) {
  MarketStructure m(StructureSettings{.atr_n = 3, .c = 1.0});
  m.on_bar(bar(0, 100, 100));
  m.on_bar(bar(1, 200, 190));  // a huge move, but no ATR yet
  EXPECT_TRUE(m.swings().empty());
}

}  // namespace
}  // namespace ob::sim
