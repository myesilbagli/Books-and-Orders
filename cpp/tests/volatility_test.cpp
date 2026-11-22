#include "orderbook/sim/volatility.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kHalf = kSecond / 2;

// Mids at 0.5 s, 1.5 s, 2.5 s, ... so the grid points (1 s, 2 s, 3 s, ...) see:
//   1 s: 20'000   2 s: 20'002   3 s: 20'000   4 s: 20'004
// Changes of mid_x2: +2, -2, +4 -> squares 4, 4, 16 -> average 8.
// mid_x2 is twice the mid, so in mid ticks that's 8 / 4 = 2 ticks^2 per interval.
void feed_path(VolatilityEstimator& vol, std::uint64_t interval = kSecond) {
  const Price path[] = {20'000, 20'002, 20'000, 20'004, 20'004};
  for (std::uint64_t i = 0; i < 5; ++i) {
    vol.on_mid(i * interval + interval / 2, path[i]);
  }
}

TEST(VolatilityTest, NoEstimateUntilTheWindowIsFull) {
  VolatilityEstimator vol(kSecond, 3);
  EXPECT_FALSE(vol.variance_per_second().has_value());
  vol.on_mid(kHalf, 20'000);
  vol.on_mid(kSecond + kHalf, 20'002);      // grid 1 s sampled: the first point
  vol.on_mid(2 * kSecond + kHalf, 20'000);  // grid 2 s: change 1
  vol.on_mid(3 * kSecond + kHalf, 20'004);  // grid 3 s: change 2
  EXPECT_EQ(vol.samples(), 2u);
  EXPECT_FALSE(vol.variance_per_second().has_value());
  vol.on_mid(4 * kSecond + kHalf, 20'004);  // grid 4 s: change 3, window full
  EXPECT_TRUE(vol.variance_per_second().has_value());
}

TEST(VolatilityTest, AverageSquaredChangePerSecond) {
  VolatilityEstimator vol(kSecond, 3);
  feed_path(vol);
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 2.0);
}

// The same moves spread over 2 s intervals: half the variance per second.
TEST(VolatilityTest, ScalesToOneSecond) {
  VolatilityEstimator vol(2 * kSecond, 3);
  feed_path(vol, 2 * kSecond);
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 1.0);
}

TEST(VolatilityTest, ConstantMidHasZeroVariance) {
  VolatilityEstimator vol(kSecond, 3);
  for (std::uint64_t i = 0; i < 6; ++i) {
    vol.on_mid(i * kSecond + kHalf, 20'000);
  }
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 0.0);
}

// Bid-ask bounce: the mid jumps up and back inside one interval. The grid never sees it.
TEST(VolatilityTest, FlickerInsideAnIntervalIsIgnored) {
  VolatilityEstimator vol(kSecond, 1);
  vol.on_mid(kHalf, 20'000);
  vol.on_mid(kSecond + kHalf, 20'000);  // grid 1 s: 20'000
  vol.on_mid(kSecond + 600'000'000, 20'010);
  vol.on_mid(kSecond + 700'000'000, 20'000);
  vol.on_mid(2 * kSecond + kHalf, 20'000);  // grid 2 s: 20'000 again
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 0.0);
}

// A change exactly on a grid point is in force at that point (same rule as mid_x2_at).
TEST(VolatilityTest, ChangeOnAGridPointCounts) {
  VolatilityEstimator vol(kSecond, 1);
  vol.on_mid(kHalf, 20'000);
  vol.on_mid(kSecond + kHalf, 20'000);  // grid 1 s: 20'000
  vol.on_mid(2 * kSecond, 20'004);      // exactly at 2 s
  vol.on_mid(2 * kSecond + kHalf, 20'004);
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 4.0);  // 4^2 / 4
}

// A quiet stretch is several intervals of no change, not a gap to skip.
TEST(VolatilityTest, QuietIntervalsCountAsZeroChanges) {
  VolatilityEstimator vol(kSecond, 3);
  vol.on_mid(kHalf, 20'000);
  vol.on_mid(kSecond + kHalf, 20'002);      // grid 1 s: 20'000
  vol.on_mid(4 * kSecond + kHalf, 20'002);  // grids 2, 3, 4 s: 20'002 each
  // Changes 2, 0, 0 -> squares 4, 0, 0 -> average 4/3 -> /4 for mid ticks.
  EXPECT_NEAR(*vol.variance_per_second(), 1.0 / 3.0, 1e-12);
}

// Only the last `window` intervals count: an old big move drops out.
TEST(VolatilityTest, OldChangesLeaveTheWindow) {
  VolatilityEstimator vol(kSecond, 1);
  vol.on_mid(kHalf, 20'000);
  vol.on_mid(kSecond + kHalf, 20'010);      // grid 1 s: 20'000
  vol.on_mid(2 * kSecond + kHalf, 20'010);  // grid 2 s: 20'010, change 10
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 25.0);  // 10^2 / 4
  vol.on_mid(3 * kSecond + kHalf, 20'010);  // grid 3 s: no change
  EXPECT_DOUBLE_EQ(*vol.variance_per_second(), 0.0);
}

}  // namespace
}  // namespace ob::sim
