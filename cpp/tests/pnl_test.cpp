#include "orderbook/sim/pnl.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kH = 1000;     // horizon for adverse selection
constexpr std::uint64_t kEnd = 10'000;  // quoting ends

// A fill with the mid just before it (twice the mid, as everywhere).
Fill fill(std::uint64_t t, Side side, Price price, Price mid_x2_before, Quantity q = 100) {
  return Fill{.timestamp_ns = t,
              .side = side,
              .price = price,
              .quantity = q,
              .mid_x2_before = mid_x2_before};
}

void expect_parts_add_up(const PnlBreakdown& b) {
  EXPECT_NEAR(b.spread + b.adverse_selection + b.inventory, b.total, 1e-9);
}

// Buy 100 at 1.0000 and sell 100 at 1.0010 around a mid that never moves (1.0005):
// 5 ticks of spread each side, nothing else. Total 1,000 ticks x shares = $0.10.
TEST(PnlTest, RoundTripInAQuietMarketIsAllSpread) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010}};
  const std::vector<Fill> fills = {fill(100, Side::Buy, 10'000, 20'010),
                                   fill(200, Side::Sell, 10'010, 20'010)};
  const PnlBreakdown b = pnl_breakdown(fills, mids, kH, kEnd, 0.0);
  EXPECT_DOUBLE_EQ(b.spread, 1'000.0);
  EXPECT_DOUBLE_EQ(b.adverse_selection, 0.0);
  EXPECT_DOUBLE_EQ(b.inventory, 0.0);
  EXPECT_DOUBLE_EQ(b.total, 1'000.0);
  EXPECT_EQ(b.shares, 200u);
}

// Bought at 1.0000 with the mid at 1.0005; within the horizon it drops to 0.9995 and
// stays. Spread +5, adverse selection -10 per share: total -5.
TEST(PnlTest, MidMovingAgainstUsIsAdverseSelection) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010},
                                      {.timestamp_ns = 500, .mid_x2 = 19'990}};
  const PnlBreakdown b =
      pnl_breakdown({fill(100, Side::Buy, 10'000, 20'010)}, mids, kH, kEnd, 0.0);
  EXPECT_DOUBLE_EQ(b.spread, 500.0);
  EXPECT_DOUBLE_EQ(b.adverse_selection, -1'000.0);
  EXPECT_DOUBLE_EQ(b.inventory, 0.0);
  EXPECT_DOUBLE_EQ(b.total, -500.0);
}

// Bought at 1.0000, mid 1.0005; quiet through the horizon, then up to 1.0020 by the end.
// Holding the position earned 15 ticks a share: inventory, not adverse selection.
TEST(PnlTest, MoveAfterTheHorizonIsInventory) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010},
                                      {.timestamp_ns = 5'000, .mid_x2 = 20'040}};
  const PnlBreakdown b =
      pnl_breakdown({fill(100, Side::Buy, 10'000, 20'010)}, mids, kH, kEnd, 0.0);
  EXPECT_DOUBLE_EQ(b.spread, 500.0);
  EXPECT_DOUBLE_EQ(b.adverse_selection, 0.0);
  EXPECT_DOUBLE_EQ(b.inventory, 1'500.0);
  EXPECT_DOUBLE_EQ(b.total, 2'000.0);
}

// A sell: the mid rising after it is against us.
TEST(PnlTest, SellSideSignsAreMirrored) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010},
                                      {.timestamp_ns = 500, .mid_x2 = 20'030}};
  const PnlBreakdown b =
      pnl_breakdown({fill(100, Side::Sell, 10'010, 20'010)}, mids, kH, kEnd, 0.0);
  EXPECT_DOUBLE_EQ(b.spread, 500.0);                // sold 5 ticks above the mid
  EXPECT_DOUBLE_EQ(b.adverse_selection, -1'000.0);  // the mid went up 10
  expect_parts_add_up(b);
}

// A fill near the end: its horizon would run past the end of quoting, into after-hours
// mids that mean nothing (D28). The horizon stops at the end.
TEST(PnlTest, HorizonStopsAtTheEndOfQuoting) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010},
                                      {.timestamp_ns = kEnd + 1, .mid_x2 = 30'000}};
  const PnlBreakdown b =
      pnl_breakdown({fill(kEnd - 10, Side::Buy, 10'000, 20'010)}, mids, kH, kEnd, 0.0);
  EXPECT_DOUBLE_EQ(b.adverse_selection, 0.0);
  EXPECT_DOUBLE_EQ(b.total, 500.0);  // marked at the mid at the end, not after it
}

// Many fills, both sides, the mid wandering: the three parts still add up exactly, and
// the total agrees with cash + position x end mid computed the plain way.
TEST(PnlTest, PartsAlwaysAddUpToTheTotal) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010},
                                      {.timestamp_ns = 300, .mid_x2 = 20'000},
                                      {.timestamp_ns = 1'700, .mid_x2 = 20'030},
                                      {.timestamp_ns = 4'000, .mid_x2 = 19'980}};
  const std::vector<Fill> fills = {fill(100, Side::Buy, 10'000, 20'010, 100),
                                   fill(400, Side::Buy, 9'995, 20'000, 300),
                                   fill(2'000, Side::Sell, 10'020, 20'030, 200),
                                   fill(5'000, Side::Sell, 9'995, 19'980, 100)};
  const PnlBreakdown b = pnl_breakdown(fills, mids, kH, kEnd, 0.0);
  expect_parts_add_up(b);
  // cash: -100x10'000 - 300x9'995 + 200x10'020 + 100x9'995; position +100; end mid 9'990.
  const double cash = -1'000'000.0 - 2'998'500.0 + 2'004'000.0 + 999'500.0;
  EXPECT_DOUBLE_EQ(b.total, cash + 100 * 9'990.0);
}

// The maker rebate: 0.2 cents = 20 ticks a share, on every share traded, on top.
TEST(PnlTest, RebateIsPaidOnEveryShare) {
  const std::vector<MidPoint> mids = {{.timestamp_ns = 0, .mid_x2 = 20'010}};
  const std::vector<Fill> fills = {fill(100, Side::Buy, 10'000, 20'010, 100),
                                   fill(200, Side::Sell, 10'010, 20'010, 200)};
  const PnlBreakdown b = pnl_breakdown(fills, mids, kH, kEnd, 20.0);
  EXPECT_DOUBLE_EQ(b.rebate, 6'000.0);
  expect_parts_add_up(b);  // the rebate is not one of the three parts
}

}  // namespace
}  // namespace ob::sim
