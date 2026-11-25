#include "orderbook/sim/levels.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

constexpr std::uint64_t kMinute = 60'000'000'000ULL;
constexpr std::uint64_t k0930 = 570 * kMinute;  // 09:30 in ns since midnight

// A 1 minute bar starting at `start`, prices in ticks (bars keep them doubled).
Bar bar(std::uint64_t start, Price open, Price high, Price low, Price close) {
  return Bar{.start_ns = start,
             .end_ns = start + kMinute,
             .open_x2 = 2 * open,
             .high_x2 = 2 * high,
             .low_x2 = 2 * low,
             .close_x2 = 2 * close,
             .volume = 0};
}

Swing swing(SwingKind kind, Price ticks) {
  return Swing{.kind = kind, .price_x2 = 2 * ticks, .time_ns = 0, .confirmed_ns = 0,
               .change = std::nullopt};
}

// The warm-up bars before 09:30 don't count; the first bar of the session gives the open.
TEST(DayRangeTest, CountsFromTheSessionStart) {
  DayRange day(k0930);
  day.on_bar(bar(k0930 - kMinute, 500, 900, 100, 500));  // 09:29: pre-market, ignored
  EXPECT_FALSE(day.open());
  day.on_bar(bar(k0930, 400, 420, 390, 410));
  day.on_bar(bar(k0930 + kMinute, 410, 450, 400, 440));
  day.on_bar(bar(k0930 + 2 * kMinute, 440, 445, 380, 385));
  EXPECT_DOUBLE_EQ(*day.open(), 400.0);
  EXPECT_DOUBLE_EQ(*day.high(), 450.0);
  EXPECT_DOUBLE_EQ(*day.low(), 380.0);
}

std::vector<double> prices(const std::vector<Level>& levels, LevelKind kind) {
  std::vector<double> out;
  for (const Level& l : levels) {
    if (l.kind == kind) {
      out.push_back(l.price);
    }
  }
  return out;
}

// Only the last 3 swing highs and the last 3 lows: older ones are history.
TEST(LevelsTest, KeepsTheLastSwingsOfEachKind) {
  const std::vector<Swing> swings = {
      swing(SwingKind::High, 1'000), swing(SwingKind::Low, 900),
      swing(SwingKind::High, 1'100), swing(SwingKind::Low, 950),
      swing(SwingKind::High, 1'200), swing(SwingKind::Low, 1'050),
      swing(SwingKind::High, 1'300), swing(SwingKind::Low, 1'150)};
  const auto levels = levels_at(swings, DayRange(k0930), std::nullopt, std::nullopt, 1'200);
  EXPECT_EQ(prices(levels, LevelKind::SwingHigh), (std::vector<double>{1'100, 1'200, 1'300}));
  EXPECT_EQ(prices(levels, LevelKind::SwingLow), (std::vector<double>{950, 1'050, 1'150}));
}

// AAPL at $276.43: the round levels around it are $276.00 and $276.50. Nothing else is
// known yet (no swings, no bar of the day, VWAP and EMA warming up), so nothing else shows.
TEST(LevelsTest, RoundLevelsAroundThePrice) {
  const auto levels = levels_at({}, DayRange(k0930), std::nullopt, std::nullopt, 2'764'300);
  ASSERT_EQ(levels.size(), 2u);
  EXPECT_EQ(prices(levels, LevelKind::Round), (std::vector<double>{2'760'000, 2'765'000}));
}

// On a round price, that price is a level and so is the next one up.
TEST(LevelsTest, OnARoundPrice) {
  const auto levels = levels_at({}, DayRange(k0930), std::nullopt, std::nullopt, 2'765'000);
  EXPECT_EQ(prices(levels, LevelKind::Round), (std::vector<double>{2'765'000, 2'770'000}));
}

// Everything together, lowest price first.
TEST(LevelsTest, AllKindsSortedByPrice) {
  DayRange day(k0930);
  day.on_bar(bar(k0930, 2'761'000, 2'768'000, 2'759'000, 2'763'000));
  const std::vector<Swing> swings = {swing(SwingKind::High, 2'766'000),
                                     swing(SwingKind::Low, 2'760'500)};
  const auto levels = levels_at(swings, day, 2'762'500.0, 2'763'200.0, 2'764'300);
  const std::vector<LevelKind> kinds = {LevelKind::DayLow,    LevelKind::Round,
                                        LevelKind::SwingLow,  LevelKind::DayOpen,
                                        LevelKind::Vwap,      LevelKind::Ema,
                                        LevelKind::Round,     LevelKind::SwingHigh,
                                        LevelKind::DayHigh};
  ASSERT_EQ(levels.size(), kinds.size());
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    EXPECT_EQ(levels[i].kind, kinds[i]) << "level " << i;
  }
  EXPECT_DOUBLE_EQ(levels.front().price, 2'759'000);
  EXPECT_DOUBLE_EQ(levels.back().price, 2'768'000);
}

}  // namespace
}  // namespace ob::sim
