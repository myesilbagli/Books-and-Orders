#include "orderbook/sim/markout.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

// Mid history (stored as bid + ask, twice the mid): 1.0005 from t=100, 1.0010 from t=200,
// 0.9995 from t=300.
const std::vector<MidPoint> kMids = {
    {.timestamp_ns = 100, .mid_x2 = 20'010},
    {.timestamp_ns = 200, .mid_x2 = 20'020},
    {.timestamp_ns = 300, .mid_x2 = 19'990},
};

// --- the mid at a given time -----------------------------------------------------------

TEST(MarkoutTest, NoMidBeforeTheFirstPoint) {
  EXPECT_FALSE(mid_x2_at(kMids, 99).has_value());
  EXPECT_FALSE(mid_x2_at({}, 1000).has_value());
}

TEST(MarkoutTest, MidAtAChangeIsTheNewMid) {
  EXPECT_EQ(mid_x2_at(kMids, 200), 20'020);
}

// Between two changes, the earlier one is still in force.
TEST(MarkoutTest, MidBetweenChangesIsTheEarlierOne) {
  EXPECT_EQ(mid_x2_at(kMids, 150), 20'010);
  EXPECT_EQ(mid_x2_at(kMids, 299), 20'020);
}

TEST(MarkoutTest, MidAfterTheLastChangeStaysTheLastOne) {
  EXPECT_EQ(mid_x2_at(kMids, 1'000'000), 19'990);
}

// --- markouts: positive = the fill was good for us ------------------------------------

// Bought at 1.0000; 100 ns later the mid is 1.0010: +10 ticks per share.
TEST(MarkoutTest, BuyThenPriceRisesIsPositive) {
  const Fill buy{.timestamp_ns = 100, .side = Side::Buy, .price = 10'000, .quantity = 100};
  EXPECT_EQ(markout_ticks(buy, kMids, 100), 10.0);
}

// Bought at 1.0000; later the mid is 0.9995: -5 ticks. Adverse selection.
TEST(MarkoutTest, BuyThenPriceFallsIsNegative) {
  const Fill buy{.timestamp_ns = 100, .side = Side::Buy, .price = 10'000, .quantity = 100};
  EXPECT_EQ(markout_ticks(buy, kMids, 200), -5.0);
}

// Sold at 1.0010; later the mid is 0.9995: +15 ticks (we sold high).
TEST(MarkoutTest, SellThenPriceFallsIsPositive) {
  const Fill sell{.timestamp_ns = 150, .side = Side::Sell, .price = 10'010, .quantity = 100};
  EXPECT_EQ(markout_ticks(sell, kMids, 150), 15.0);
}

// Horizon 0: the mid right when we were filled. For a fill at the touch this is about
// half the spread: what we "earned" on paper at the moment of the trade.
TEST(MarkoutTest, ZeroHorizonUsesTheMidAtTheFill) {
  const Fill buy{.timestamp_ns = 100, .side = Side::Buy, .price = 10'000, .quantity = 100};
  EXPECT_EQ(markout_ticks(buy, kMids, 0), 5.0);
}

TEST(MarkoutTest, NoMidMeansNoMarkout) {
  const Fill buy{.timestamp_ns = 50, .side = Side::Buy, .price = 10'000, .quantity = 100};
  EXPECT_FALSE(markout_ticks(buy, kMids, 0).has_value());
}

}  // namespace
}  // namespace ob::sim
