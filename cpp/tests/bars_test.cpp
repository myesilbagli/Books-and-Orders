#include "orderbook/sim/bars.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMs = 1'000'000;
constexpr std::uint64_t kSecond = 1'000 * kMs;

// One bar's fields in one line, so a failure says which field is off.
void expect_bar(const Bar& bar, std::uint64_t start_ns, std::uint64_t interval_ns, Price open,
                Price high, Price low, Price close, std::uint64_t volume) {
  EXPECT_EQ(bar.start_ns, start_ns);
  EXPECT_EQ(bar.end_ns, start_ns + interval_ns);
  EXPECT_EQ(bar.open_x2, open);
  EXPECT_EQ(bar.high_x2, high);
  EXPECT_EQ(bar.low_x2, low);
  EXPECT_EQ(bar.close_x2, close);
  EXPECT_EQ(bar.volume, volume);
}

// Mid 1.0000, up to 1.0002, down to 0.9999, ends the second at 1.0001.
TEST(BarBuilderTest, OneSecondGivesOpenHighLowClose) {
  BarBuilder bars(kSecond);
  bars.on_mid(0, 20'000);
  bars.on_mid(300 * kMs, 20'004);
  bars.on_mid(600 * kMs, 19'998);
  bars.on_mid(900 * kMs, 20'002);
  const std::vector<Bar> closed = bars.advance(kSecond);
  ASSERT_EQ(closed.size(), 1u);
  expect_bar(closed[0], 0, kSecond, 20'000, 20'004, 19'998, 20'002, 0);
}

// No look-ahead: at 0.999 s the bar isn't over, so nobody gets it yet.
TEST(BarBuilderTest, ABarIsHandedOutOnlyWhenItIsOver) {
  BarBuilder bars(kSecond);
  bars.on_mid(200 * kMs, 20'000);
  EXPECT_TRUE(bars.advance(999 * kMs).empty());
  EXPECT_EQ(bars.advance(kSecond).size(), 1u);
}

// A change exactly at 1 s belongs to the second bar: the first closes at the old mid,
// the second opens at the new one.
TEST(BarBuilderTest, AChangeAtTheBoundaryBelongsToTheNextBar) {
  BarBuilder bars(kSecond);
  bars.on_mid(0, 20'000);
  const std::vector<Bar> first = bars.on_mid(kSecond, 20'010);
  ASSERT_EQ(first.size(), 1u);
  expect_bar(first[0], 0, kSecond, 20'000, 20'000, 20'000, 20'000, 0);
  const std::vector<Bar> second = bars.advance(2 * kSecond);
  ASSERT_EQ(second.size(), 1u);
  expect_bar(second[0], kSecond, kSecond, 20'010, 20'010, 20'010, 20'010, 0);
}

// Quiet seconds still make bars: flat at the last mid, no volume. The bar where the mid
// finally moves opens at the mid in force when it started.
TEST(BarBuilderTest, QuietSecondsAreFlatBars) {
  BarBuilder bars(kSecond);
  bars.on_mid(500 * kMs, 20'000);
  const std::vector<Bar> quiet = bars.on_mid(3'500 * kMs, 20'010);
  ASSERT_EQ(quiet.size(), 3u);
  expect_bar(quiet[0], 0, kSecond, 20'000, 20'000, 20'000, 20'000, 0);
  expect_bar(quiet[1], kSecond, kSecond, 20'000, 20'000, 20'000, 20'000, 0);
  expect_bar(quiet[2], 2 * kSecond, kSecond, 20'000, 20'000, 20'000, 20'000, 0);
  const std::vector<Bar> moved = bars.advance(4 * kSecond);
  ASSERT_EQ(moved.size(), 1u);
  expect_bar(moved[0], 3 * kSecond, kSecond, 20'000, 20'010, 20'000, 20'010, 0);
}

TEST(BarBuilderTest, VolumeAddsUpTheTradesInEachBar) {
  BarBuilder bars(kSecond);
  bars.on_mid(0, 20'000);
  bars.on_trade(200 * kMs, 100);
  bars.on_trade(700 * kMs, 300);
  const std::vector<Bar> first = bars.on_trade(1'100 * kMs, 50);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].volume, 400u);
  const std::vector<Bar> second = bars.advance(2 * kSecond);
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].volume, 50u);
}

// Before the first mid there's no price, so no bar to put a trade in.
TEST(BarBuilderTest, NothingBeforeTheFirstMid) {
  BarBuilder bars(kSecond);
  EXPECT_TRUE(bars.on_trade(100 * kMs, 100).empty());
  EXPECT_TRUE(bars.advance(5 * kSecond).empty());
  bars.on_mid(5'200 * kMs, 20'000);
  const std::vector<Bar> closed = bars.advance(6 * kSecond);
  ASSERT_EQ(closed.size(), 1u);
  expect_bar(closed[0], 5 * kSecond, kSecond, 20'000, 20'000, 20'000, 20'000, 0);
}

// 1-minute bars line up with the clock: a first mid at 09:30:30 is in the 09:30 bar.
TEST(BarBuilderTest, BarsAreAlignedToTheClock) {
  constexpr std::uint64_t kMinute = 60 * kSecond;
  constexpr std::uint64_t k0930 = (9 * 60 + 30) * kMinute;
  BarBuilder bars(kMinute);
  bars.on_mid(k0930 + 30 * kSecond, 20'000);
  const std::vector<Bar> closed = bars.advance(k0930 + kMinute);
  ASSERT_EQ(closed.size(), 1u);
  expect_bar(closed[0], k0930, kMinute, 20'000, 20'000, 20'000, 20'000, 0);
}

}  // namespace
}  // namespace ob::sim
