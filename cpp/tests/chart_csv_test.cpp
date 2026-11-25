#include "orderbook/sim/chart_csv.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace ob::sim {
namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;

// The 09:30:05 one-second bar: mid $290.1200 -> $290.1350 high, $290.1000 low,
// closes $290.1250. Prices in the bar are doubled ticks of $0.0001.
Bar bar_0930_05() {
  const std::uint64_t start = (9 * 3600 + 30 * 60 + 5) * kSecond;
  return Bar{.start_ns = start,
             .end_ns = start + kSecond,
             .open_x2 = 2 * 2'901'200,
             .high_x2 = 2 * 2'901'350,
             .low_x2 = 2 * 2'901'000,
             .close_x2 = 2 * 2'901'250,
             .volume = 1'200};
}

TEST(ChartCsvTest, Header) {
  std::ostringstream out;
  write_chart_header(out);
  EXPECT_EQ(out.str(),
            "timeframe,time_et,open,high,low,close,volume,vwap,ema_fast,ema_slow,sma,"
            "bb_upper,bb_lower,bb_pct,rsi,stoch_k,stoch_d,atr,structure,er,ema_mid,ema_long,b1,b2\n");
}

// Warming up: no indicator has a value yet, so those fields are empty, not 0.
TEST(ChartCsvTest, WarmingUpLeavesIndicatorsEmpty) {
  std::ostringstream out;
  write_chart_row(out, "1s", ChartPoint{.bar = bar_0930_05()}, std::nullopt);
  EXPECT_EQ(out.str(), "1s,09:30:05,290.1200,290.1350,290.1000,290.1250,1200,,,,,,,,,,,,,,,,,\n");
}

// Everything in chart units: dollars to 4 places, %B to 3, RSI and stoch to 2.
TEST(ChartCsvTest, ValuesInChartUnits) {
  const ChartPoint p{.bar = bar_0930_05(),
                     .ema_fast = 2'901'100.0,
                     .ema_slow = 2'900'950.6,
                     .sma = 2'901'000.0,
                     .bollinger = BollingerValue{.middle = 2'901'000.0,
                                                 .upper = 2'901'400.0,
                                                 .lower = 2'900'600.0,
                                                 .percent_b = 0.8126},
                     .rsi = 61.234,
                     .stoch_rsi = StochRsiValue{.k = 80.5, .d = 75.25},
                     .atr = 1'500.0,
                     .structure = -2,
                     .er = 0.12345,
                     .ema_mid = 2'901'050.0,
                     .ema_long = 2'900'500.0,
                     .b1 = -1,
                     .b2 = 0};
  std::ostringstream out;
  write_chart_row(out, "1m", p, 2'899'876.4);
  EXPECT_EQ(out.str(),
            "1m,09:30:05,290.1200,290.1350,290.1000,290.1250,1200,289.9876,290.1100,290.0951,"
            "290.1000,290.1400,290.0600,0.813,61.23,80.50,75.25,0.1500,-2,0.123,290.1050,"
            "290.0500,-1,0\n");
}

TEST(ChartCsvTest, AfternoonTimeIsTwentyFourHour) {
  Bar bar = bar_0930_05();
  bar.start_ns = (15 * 3600 + 59 * 60) * kSecond;
  std::ostringstream out;
  write_chart_row(out, "1m", ChartPoint{.bar = bar}, std::nullopt);
  EXPECT_EQ(out.str().substr(0, 12), "1m,15:59:00,");
}

// The swings, one row each, for drawing the structure on the hub (D49).
TEST(ChartCsvTest, SwingRows) {
  std::ostringstream out;
  write_swings_header(out);
  const std::uint64_t made = (9 * 3600 + 45 * 60) * kSecond;
  write_swing_row(out, "1m", Swing{.kind = SwingKind::High, .price_x2 = 2 * 2'901'350,
                                   .time_ns = made, .confirmed_ns = made + 180 * kSecond,
                                   .change = 1});
  write_swing_row(out, "1m", Swing{.kind = SwingKind::Low, .price_x2 = 2 * 2'900'000,
                                   .time_ns = made, .confirmed_ns = made + 60 * kSecond,
                                   .change = std::nullopt});
  EXPECT_EQ(out.str(),
            "timeframe,kind,price,time_et,confirmed_et,change\n"
            "1m,H,290.1350,09:45:00,09:48:00,1\n"
            "1m,L,290.0000,09:45:00,09:46:00,\n");
}

}  // namespace
}  // namespace ob::sim
