#include "orderbook/sim/chart_feed.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMs = 1'000'000;
constexpr std::uint64_t kSecond = 1'000 * kMs;

// Short lengths, so a few bars warm everything up: EMA 2/3, SMA 2, Bollinger (2, 2),
// RSI 2, Stoch RSI (2, 2, 1, 1).
ChartSettings small() {
  return ChartSettings{.ema_fast = 2,
                       .ema_slow = 3,
                       .sma = 2,
                       .bollinger_n = 2,
                       .bollinger_k = 2.0,
                       .rsi = 2,
                       .stoch = 2,
                       .stoch_k = 1,
                       .stoch_d = 1};
}

// Indicators see a bar only once it has closed. The first second closes at 1.0000 and
// the second at 1.0002, so SMA(2) of the closes (10,000 and 10,002 ticks) is 10,001.
TEST(ChartFeedTest, IndicatorsUpdateWhenABarCloses) {
  ChartFeed chart(kSecond, small());
  chart.on_mid(0, 20'000);
  chart.on_mid(kSecond, 20'002);
  ASSERT_TRUE(chart.latest());
  EXPECT_FALSE(chart.latest()->sma);  // one closed bar: SMA(2) not warm yet
  chart.on_mid(1'500 * kMs, 20'004);  // inside the second bar: nothing new closes
  EXPECT_EQ(chart.latest()->bar.start_ns, 0u);
  chart.advance(2 * kSecond);
  ASSERT_TRUE(chart.latest()->sma);
  EXPECT_DOUBLE_EQ(*chart.latest()->sma, (10'000.0 + 10'002.0) / 2);  // closes in ticks
  EXPECT_DOUBLE_EQ(*chart.latest()->ema_fast, (10'000.0 + 10'002.0) / 2);  // EMA seed
}

TEST(ChartFeedTest, NothingBeforeTheFirstBarCloses) {
  ChartFeed chart(kSecond, small());
  chart.on_mid(100 * kMs, 20'000);
  EXPECT_FALSE(chart.latest());
  EXPECT_TRUE(chart.advance(999 * kMs).empty());
  EXPECT_FALSE(chart.latest());
}

// A quiet stretch closes several bars at once; each point keeps its own values.
// Closes: 10,000 (bar 0), 10,005 (bar 1), then flat 10,005 (bars 2 and 3).
TEST(ChartFeedTest, EveryClosedBarGetsItsOwnPoint) {
  ChartFeed chart(kSecond, small());
  chart.on_mid(500 * kMs, 20'000);
  std::vector<ChartPoint> points = chart.on_mid(1'500 * kMs, 20'010);  // closes bar 0
  ASSERT_EQ(points.size(), 1u);
  for (const ChartPoint& p : chart.advance(4 * kSecond)) {  // bars 1, 2, 3 at once
    points.push_back(p);
  }
  ASSERT_EQ(points.size(), 4u);
  EXPECT_FALSE(points[0].sma);
  EXPECT_DOUBLE_EQ(*points[1].sma, 10'002.5);
  EXPECT_DOUBLE_EQ(*points[2].sma, 10'005.0);
  EXPECT_DOUBLE_EQ(*points[3].sma, 10'005.0);
  EXPECT_EQ(points[3].bar.start_ns, 3 * kSecond);
  EXPECT_EQ(chart.latest()->bar.start_ns, 3 * kSecond);  // latest = the last one
}

TEST(ChartFeedTest, TradesGoIntoTheBarsVolume) {
  ChartFeed chart(kSecond, small());
  chart.on_mid(0, 20'000);
  chart.on_trade(300 * kMs, 100);
  chart.on_trade(800 * kMs, 200);
  const std::vector<ChartPoint> points = chart.advance(kSecond);
  ASSERT_EQ(points.size(), 1u);
  EXPECT_EQ(points[0].bar.volume, 300u);
}

// All indicators run off the same closes: after enough bars every one has a value.
TEST(ChartFeedTest, EveryIndicatorWarmsUp) {
  ChartFeed chart(kSecond, small());
  const Price mids[] = {20'000, 20'002, 20'000, 20'004, 20'002, 20'006};
  for (std::size_t i = 0; i < 6; ++i) {
    chart.on_mid(i * kSecond, mids[i]);
  }
  chart.advance(6 * kSecond);
  const ChartPoint& p = *chart.latest();
  EXPECT_TRUE(p.ema_fast);
  EXPECT_TRUE(p.ema_slow);
  EXPECT_TRUE(p.sma);
  EXPECT_TRUE(p.bollinger);
  EXPECT_TRUE(p.rsi);
  EXPECT_TRUE(p.stoch_rsi);
}

// The defaults are the founder's chart: EMA 9/21, SMA 20, Bollinger (20, 2),
// Stoch RSI (14, 14, 3, 3).
TEST(ChartFeedTest, DefaultsAreTheChartSettings) {
  const ChartSettings s;
  EXPECT_EQ(s.ema_fast, 9u);
  EXPECT_EQ(s.ema_slow, 21u);
  EXPECT_EQ(s.sma, 20u);
  EXPECT_EQ(s.bollinger_n, 20u);
  EXPECT_DOUBLE_EQ(s.bollinger_k, 2.0);
  EXPECT_EQ(s.rsi, 14u);
  EXPECT_EQ(s.stoch, 14u);
  EXPECT_EQ(s.stoch_k, 3u);
  EXPECT_EQ(s.stoch_d, 3u);
  EXPECT_EQ(s.efficiency, 10u);  // Kaufman's, on every timeframe (D57)
}

// Every timeframe has its own ATR and market structure (D49). One mid per second makes
// flat bars at 100, 110, 100 ticks. ATR(2): true ranges 0, 10 -> 5; then 10 -> 7.5. With
// c = 1 the low of 100 is a swing once 110 is 5 above it, and 110 once 100 is 7.5 below.
TEST(ChartFeedTest, EachTimeframeHasItsOwnStructure) {
  ChartSettings s = small();
  s.structure = StructureSettings{.atr_n = 2, .c = 1.0};
  ChartFeed chart(kSecond, s);
  chart.on_mid(0, 200);
  chart.on_mid(kSecond, 220);
  chart.on_mid(2 * kSecond, 200);
  chart.advance(3 * kSecond);
  ASSERT_TRUE(chart.latest()->atr);
  EXPECT_DOUBLE_EQ(*chart.latest()->atr, 7.5);
  ASSERT_EQ(chart.structure().swings().size(), 2u);  // the low 100, then the high 110
  EXPECT_FALSE(chart.latest()->structure);           // two of each needed for a score
}

// Each closed bar carries its efficiency ratio. ER(2) on closes 100, 110, 100: net 0 over a
// path of 20, so 0; nothing before the third close.
TEST(ChartFeedTest, EachPointHasItsEfficiencyRatio) {
  ChartSettings s = small();
  s.efficiency = 2;
  ChartFeed chart(kSecond, s);
  chart.on_mid(0, 200);
  chart.on_mid(kSecond, 220);
  chart.advance(2 * kSecond);
  EXPECT_FALSE(chart.latest()->er);
  chart.on_mid(2 * kSecond, 200);
  chart.advance(3 * kSecond);
  ASSERT_TRUE(chart.latest()->er);
  EXPECT_DOUBLE_EQ(*chart.latest()->er, 0.0);
}

}  // namespace
}  // namespace ob::sim
