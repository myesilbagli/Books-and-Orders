#pragma once

#include "orderbook/sim/bars.hpp"
#include "orderbook/sim/indicators.hpp"
#include "orderbook/sim/structure.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// One timeframe's chart: bars plus the indicators on them, updated each time a bar
// closes (D40). Run one per timeframe (1 s and 1 min side by side). VWAP isn't here: it
// is per trade, not per bar, so all timeframes share one.

namespace ob::sim {

// The founder's chart settings by default; trials can change any of them.
struct ChartSettings {
  std::size_t ema_fast = 9;
  std::size_t ema_slow = 21;
  std::size_t sma = 20;
  std::size_t bollinger_n = 20;
  double bollinger_k = 2.0;
  std::size_t rsi = 14;
  std::size_t stoch = 14;
  std::size_t stoch_k = 3;
  std::size_t stoch_d = 3;
  StructureSettings structure;  // ATR(14), swings of 2 ATR (D48, D49)
  std::size_t efficiency = 10;  // efficiency ratio over 10 bars, Kaufman's (D57)
  // Brooks' EMA 20 and a slow 50, for the 9 > 20 > 50 trend stack and the pullback level
  // (D63). Separate from ema_slow (21): the older columns stay as they were.
  std::size_t ema_mid = 20;
  std::size_t ema_long = 50;
};

// A closed bar and every indicator's value as of its close (nullopt while warming up).
// Prices in ticks.
struct ChartPoint {
  Bar bar;
  std::optional<double> ema_fast;
  std::optional<double> ema_slow;
  std::optional<double> sma;
  std::optional<BollingerValue> bollinger;
  std::optional<double> rsi;
  std::optional<StochRsiValue> stoch_rsi;
  std::optional<double> atr;     // ticks
  std::optional<int> structure;  // -2 down, +2 up, else range; nullopt before 2 highs + 2 lows
  std::optional<double> er;      // efficiency ratio, 0 (going nowhere) to 1 (straight line)
  std::optional<double> ema_mid;   // EMA 20 by default
  std::optional<double> ema_long;  // EMA 50 by default
};

class ChartFeed {
 public:
  explicit ChartFeed(std::uint64_t interval_ns, ChartSettings settings = {});

  // Same events as BarBuilder. Each returns one point per bar that closed, oldest first,
  // with the indicators as they were at that bar's close (a quiet stretch closes several).
  std::vector<ChartPoint> on_mid(std::uint64_t t_ns, Price mid_x2);
  std::vector<ChartPoint> on_trade(std::uint64_t t_ns, std::uint64_t shares);
  std::vector<ChartPoint> advance(std::uint64_t t_ns);

  // The last closed bar's point: what a strategy may look at now. Never the bar that is
  // still open (no look-ahead).
  [[nodiscard]] const std::optional<ChartPoint>& latest() const { return latest_; }
  // Every swing so far, for drawing (a strategy needs only the score in latest()).
  [[nodiscard]] const MarketStructure& structure() const { return structure_; }

 private:
  BarBuilder bars_;
  Ema ema_fast_;
  Ema ema_slow_;
  Sma sma_;
  Bollinger bollinger_;
  Rsi rsi_;
  StochRsi stoch_rsi_;
  MarketStructure structure_;
  EfficiencyRatio er_;
  Ema ema_mid_;
  Ema ema_long_;
  std::optional<ChartPoint> latest_;

  // Feeds closed bars to the indicators, one at a time, and records each one's point.
  std::vector<ChartPoint> close(const std::vector<Bar>& closed);
};

}  // namespace ob::sim
