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
    "touch_share,shares_left\n";

TEST(FillsCsvTest, NoFillsIsJustTheHeader) {
  std::ostringstream out;
  write_fills_csv(out, {}, kMids, 100 * kSecond);
  EXPECT_EQ(out.str(), kHeader);
}

// 1 s later the mid is 1.0000, 10 s later 1.0010, 60 s later 0.9990.
TEST(FillsCsvTest, OneRowPerFillWithTheMidsLater) {
  std::ostringstream out;
  write_fills_csv(out, {kBuy}, kMids, 100 * kSecond);
  EXPECT_EQ(out.str(), std::string(kHeader) + "1000,B,10000,100,20010,20000,20020,19980,0.8,340\n");
}

// Quoting ended at 2 s: the 10 s and 60 s mids are the mid at the end, not after it.
TEST(FillsCsvTest, LaterMidsStopAtTheEnd) {
  std::ostringstream out;
  write_fills_csv(out, {kBuy}, kMids, 2 * kSecond);
  EXPECT_EQ(out.str(), std::string(kHeader) + "1000,B,10000,100,20010,20000,20000,20000,0.8,340\n");
}

TEST(FillsCsvTest, SellsAreMarkedS) {
  Fill sell = kBuy;
  sell.side = Side::Sell;
  std::ostringstream out;
  write_fills_csv(out, {sell}, kMids, 100 * kSecond);
  EXPECT_NE(out.str().find("\n1000,S,"), std::string::npos);
}

}  // namespace
}  // namespace ob::sim
