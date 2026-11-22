#include "orderbook/sim/indicators.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>

namespace ob::sim {
namespace {

// --- SMA / EMA -------------------------------------------------------------------------

TEST(SmaTest, AverageOfTheLastN) {
  Sma sma(3);
  sma.update(10);
  sma.update(11);
  EXPECT_FALSE(sma.value());  // two values: not warm yet
  sma.update(12);
  EXPECT_DOUBLE_EQ(*sma.value(), 11.0);
  sma.update(13);  // 10 drops out
  EXPECT_DOUBLE_EQ(*sma.value(), 12.0);
}

// A running sum drifts: 0.1 + 0.1 + 0.1 - 0.1 - 0.1 - 0.1 is 2.8e-17, not 0, in doubles.
// After three zeros the average must be exactly 0 (it printed "-0.00" for Stoch RSI).
TEST(SmaTest, NoDriftAfterTheWindowMovesOn) {
  Sma sma(3);
  for (double x : {0.1, 0.1, 0.1, 0.0, 0.0, 0.0}) {
    sma.update(x);
  }
  EXPECT_EQ(*sma.value(), 0.0);
}

// EMA(3): alpha = 0.5. First value = SMA of 10, 11, 12 = 11; then 13 moves it halfway: 12.
TEST(EmaTest, SeededWithTheSmaThenMovesByAlpha) {
  Ema ema(3);
  ema.update(10);
  ema.update(11);
  EXPECT_FALSE(ema.value());
  ema.update(12);
  EXPECT_DOUBLE_EQ(*ema.value(), 11.0);
  ema.update(13);
  EXPECT_DOUBLE_EQ(*ema.value(), 12.0);
  ema.update(13);
  EXPECT_DOUBLE_EQ(*ema.value(), 12.5);
}

// --- Bollinger -------------------------------------------------------------------------

// Closes 1, 2, 3: mean 2, population variance 2/3.
TEST(BollingerTest, BandsAreKStandardDeviationsAroundTheSma) {
  Bollinger bb(3, 2.0);
  bb.update(1);
  bb.update(2);
  EXPECT_FALSE(bb.value());
  bb.update(3);
  const BollingerValue v = *bb.value();
  const double sd = std::sqrt(2.0 / 3.0);
  EXPECT_DOUBLE_EQ(v.middle, 2.0);
  EXPECT_DOUBLE_EQ(v.upper, 2.0 + 2 * sd);
  EXPECT_DOUBLE_EQ(v.lower, 2.0 - 2 * sd);
  EXPECT_DOUBLE_EQ(v.percent_b, (3.0 - v.lower) / (v.upper - v.lower));
}

// AAPL at $2,900 in ticks, moving 1 tick: the mean is 29,000,000.5 and the standard
// deviation exactly 0.5. Sum-of-squares shortcuts lose this (catastrophic cancellation:
// two numbers near 1.7e16 subtracted to find 5).
TEST(BollingerTest, StaysExactAtRealPriceLevels) {
  Bollinger bb(20, 2.0);
  for (int i = 0; i < 20; ++i) {
    bb.update(i % 2 == 0 ? 29'000'000.0 : 29'000'001.0);
  }
  const BollingerValue v = *bb.value();
  EXPECT_DOUBLE_EQ(v.middle, 29'000'000.5);
  EXPECT_DOUBLE_EQ(v.upper, 29'000'001.5);
  EXPECT_DOUBLE_EQ(v.lower, 28'999'999.5);
}

// A flat price has no band; the close is in the middle of it.
TEST(BollingerTest, FlatPriceIsInTheMiddle) {
  Bollinger bb(3, 2.0);
  for (int i = 0; i < 3; ++i) {
    bb.update(100);
  }
  const BollingerValue v = *bb.value();
  EXPECT_DOUBLE_EQ(v.upper, 100.0);
  EXPECT_DOUBLE_EQ(v.lower, 100.0);
  EXPECT_DOUBLE_EQ(v.percent_b, 0.5);
}

// --- RSI -------------------------------------------------------------------------------

// RSI(2) on 10, 11, 10: changes +1, -1, so average gain = average loss = 0.5: RSI 50.
// Then 12 (+2), Wilder: gain (0.5 + 2) / 2 = 1.25, loss (0.5 + 0) / 2 = 0.25.
TEST(RsiTest, WildersSmoothing) {
  Rsi rsi(2);
  rsi.update(10);
  rsi.update(11);
  EXPECT_FALSE(rsi.value());  // one change: not warm yet
  rsi.update(10);
  EXPECT_DOUBLE_EQ(*rsi.value(), 50.0);
  rsi.update(12);
  EXPECT_DOUBLE_EQ(*rsi.value(), 100.0 * 1.25 / (1.25 + 0.25));
}

TEST(RsiTest, OnlyGainsIs100) {
  Rsi rsi(2);
  for (double c : {1.0, 2.0, 3.0}) {
    rsi.update(c);
  }
  EXPECT_DOUBLE_EQ(*rsi.value(), 100.0);
}

// No move at all is 0 / 0: called neutral, 50. Common on a 1-second chart.
TEST(RsiTest, NoMoveIsFifty) {
  Rsi rsi(3);
  for (int i = 0; i < 4; ++i) {
    rsi.update(5);
  }
  EXPECT_DOUBLE_EQ(*rsi.value(), 50.0);
}

// --- Stochastic RSI --------------------------------------------------------------------

// RSI(2) runs 50 (after 10, 11, 10), 83.3 (12), 50 (11). Stoch over 2 RSI values:
// at 12 the RSI is the top of [50, 83.3]: 100; at 11 it's the bottom: 0.
// %K = SMA 1 (raw), %D = SMA 2 of %K: (100 + 0) / 2 = 50.
TEST(StochRsiTest, WhereTheRsiSitsInItsRange) {
  StochRsi srsi(2, 2, 1, 2);
  for (double c : {10.0, 11.0, 10.0, 12.0}) {
    srsi.update(c);
  }
  EXPECT_FALSE(srsi.value());  // one %K so far, %D needs two
  srsi.update(11);
  const StochRsiValue v = *srsi.value();
  EXPECT_DOUBLE_EQ(v.k, 0.0);
  EXPECT_DOUBLE_EQ(v.d, 50.0);
}

// A flat RSI has no range: called neutral, 50.
TEST(StochRsiTest, FlatRsiIsFifty) {
  StochRsi srsi(2, 2, 1, 1);
  for (int i = 0; i < 6; ++i) {
    srsi.update(5);
  }
  const StochRsiValue v = *srsi.value();
  EXPECT_DOUBLE_EQ(v.k, 50.0);
  EXPECT_DOUBLE_EQ(v.d, 50.0);
}

// The usual (14, 14, 3, 3): RSI ready at close 15, first stoch at 28, %K at 30, %D at 32.
// On 1-minute bars that's 32 minutes of warm-up.
TEST(StochRsiTest, StandardSettingsNeed32Closes) {
  StochRsi srsi(14, 14, 3, 3);
  for (int i = 1; i <= 31; ++i) {
    srsi.update(100.0 + (i % 3));
  }
  EXPECT_FALSE(srsi.value());
  srsi.update(100.0);
  EXPECT_TRUE(srsi.value());
}

// --- VWAP ------------------------------------------------------------------------------

TEST(VwapTest, VolumeWeightedFromTheStart) {
  Vwap vwap(1'000);
  EXPECT_FALSE(vwap.value());
  vwap.on_trade(999, 50'000, 1'000'000);  // before the start: ignored
  vwap.on_trade(1'000, 10'000, 100);
  vwap.on_trade(2'000, 10'010, 300);
  EXPECT_DOUBLE_EQ(*vwap.value(), (10'000.0 * 100 + 10'010.0 * 300) / 400);
}

}  // namespace
}  // namespace ob::sim
