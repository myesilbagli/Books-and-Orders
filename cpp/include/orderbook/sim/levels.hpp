#pragma once

#include "orderbook/order.hpp"
#include "orderbook/sim/bars.hpp"
#include "orderbook/sim/structure.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The price levels a trader watches, as known at one moment (D63): where a breakout can
// fail (B1) and a pullback can end (B2). Brooks calls them magnets. Only what is known by
// then goes in: confirmed swings, the day so far, the current VWAP and EMA.

namespace ob::sim {

enum class LevelKind : std::uint8_t {
  SwingHigh,  // one of the last few confirmed swing highs
  SwingLow,
  DayHigh,    // the day so far
  DayLow,
  DayOpen,
  Vwap,
  Ema,        // the EMA 20 of the context chart
  Round,      // a round price (every 50 cents by default)
};

struct Level {
  LevelKind kind;
  double price;  // ticks, not doubled: the same unit as the indicators
};

// The day's open, high and low from the bars that start at or after from_ns (09:30 for the
// regular session; the charts start earlier to warm up). Feed closed bars only.
class DayRange {
 public:
  explicit DayRange(std::uint64_t from_ns);
  void on_bar(const Bar& bar);

  [[nodiscard]] std::optional<double> open() const;  // ticks; nullopt before the first bar
  [[nodiscard]] std::optional<double> high() const;
  [[nodiscard]] std::optional<double> low() const;

 private:
  std::uint64_t from_ns_;
  std::optional<Bar> range_;  // open of the first bar, highest high, lowest low so far
};

struct LevelSettings {
  std::size_t swings_each = 3;  // the last 3 swing highs and the last 3 lows
  Price round_step = 5'000;     // 50 cents in ticks: Brooks' AAPL scalp size
};

// Every level in force at `price` (ticks), lowest first. Missing inputs (no VWAP yet, EMA
// warming up, no bar of the day yet) are left out. Round levels: the one at or below the
// price and the one above it. `swings`: a view of the swings known by then, oldest first
// (a std::span: a pointer and a length, so the caller can pass the first n of a vector
// without copying them).
std::vector<Level> levels_at(std::span<const Swing> swings, const DayRange& day,
                             std::optional<double> vwap, std::optional<double> ema,
                             double price, const LevelSettings& settings = {});

}  // namespace ob::sim
