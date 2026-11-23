#include "orderbook/sim/fills_csv.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;

// Mid 1.0005, then 1.0000 from 0.5 s, 1.0010 from 5 s, 0.9990 from 30 s.
const std::vector<MidPoint> kMids = {
    {.timestamp_ns = 0, .mid_x2 = 20'010},
    {.timestamp_ns = kSecond / 2, .mid_x2 = 20'000},
    {.timestamp_ns = 5 * kSecond, .mid_x2 = 20'020},
    {.timestamp_ns = 30 * kSecond, .mid_x2 = 19'980},
};

const Fill kBuy{.timestamp_ns = 1000,
                .side = Side::Buy,
                .price = 10'000,
                .quantity = 100,
                .touch_share = 0.8,
                .shares_left = 340,
                .mid_x2_before = 20'010};

const char* const kHeader =
    "timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
    "touch_share,shares_left,"
    "ema_fast_1s,ema_slow_1s,sma_1s,bb_pct_1s,rsi_1s,stoch_k_1s,stoch_d_1s,"
    "ema_fast_1m,ema_slow_1m,sma_1m,bb_pct_1m,rsi_1m,stoch_k_1m,stoch_d_1m,vwap\n";

// No chart snapshot (outside the window): the 15 indicator fields are empty.
const char* const kNoCharts = ",,,,,,,,,,,,,,,";

TEST(FillsCsvTest, NoFillsIsJustTheHeader) {
  std::ostringstream out;
  write_fills_csv(out, {}, kMids, 100 * kSecond);
  EXPECT_EQ(out.str(), kHeader);
}

// 1 s later the mid is 1.0000, 10 s later 1.0010, 60 s later 0.9990.
TEST(FillsCsvTest, OneRowPerFillWithTheMidsLater) {
  std::ostringstream out;
  write_fills_csv(out, {kBuy}, kMids, 100 * kSecond);
  EXPECT_EQ(out.str(), std::string(kHeader) + "1000,B,10000,100,20010,20000,20020,19980,0.8,340" +
                           kNoCharts + "\n");
}

// Quoting ended at 2 s: the 10 s and 60 s mids are the mid at the end, not after it.
TEST(FillsCsvTest, LaterMidsStopAtTheEnd) {
  std::ostringstream out;
  write_fills_csv(out, {kBuy}, kMids, 2 * kSecond);
  EXPECT_EQ(out.str(), std::string(kHeader) + "1000,B,10000,100,20010,20000,20000,20000,0.8,340" +
                           kNoCharts + "\n");
}

TEST(FillsCsvTest, SellsAreMarkedS) {
  Fill sell = kBuy;
  sell.side = Side::Sell;
  std::ostringstream out;
  write_fills_csv(out, {sell}, kMids, 100 * kSecond);
  EXPECT_NE(out.str().find("\n1000,S,"), std::string::npos);
}

// With a snapshot: prices in ticks (2 places), %B to 3, RSI and stoch to 2. A 1 s chart
// still warming up its slow EMA leaves that one field empty; the 1 m chart isn't there.
TEST(FillsCsvTest, WritesTheChartsAtTheFill) {
  ChartPoint s1;
  s1.ema_fast = 10'004.123;
  s1.sma = 10'003.5;
  s1.bollinger = BollingerValue{.middle = 10'003.5, .upper = 10'010.0, .lower = 9'997.0,
                                .percent_b = 0.2504};
  s1.rsi = 41.237;
  s1.stoch_rsi = StochRsiValue{.k = 12.5, .d = 20.0};
  Fill f = kBuy;
  f.chart_1s = s1;
  f.vwap = 10'002.25;
  std::ostringstream out;
  write_fills_csv(out, {f}, kMids, 100 * kSecond);
  EXPECT_EQ(out.str(), std::string(kHeader) +
                           "1000,B,10000,100,20010,20000,20020,19980,0.8,340,"
                           "10004.12,,10003.50,0.250,41.24,12.50,20.00,"
                           ",,,,,,,10002.25\n");
}

}  // namespace
}  // namespace ob::sim
