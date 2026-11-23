#pragma once

#include "orderbook/order.hpp"
#include "orderbook/sim/bars.hpp"
#include "orderbook/sim/indicators.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Market structure (D48): swings found by a ZigZag on closed bars, and a score from the
// last two swing highs and lows:
//
//   score = sign(high2 - high1) + sign(low2 - low1)
//   +2 higher highs and higher lows (uptrend), -2 lower highs and lower lows (downtrend),
//   anything else mixed (range).
//
// A swing is "a meaningful pullback": a high is confirmed once a later bar's low is at
// least delta below it (a low, once a later high is delta above it). Only confirmed swings
// count, so the score changes only when a new swing is confirmed: an uptrend ends at the
// first lower high, not when price dips under the last low (the founder's rule).

namespace ob::sim {

enum class SwingKind : std::uint8_t { High, Low };

struct Swing {
  SwingKind kind;
  Price price_x2;             // the bar's high (or low), doubled like the bars' prices
  std::uint64_t time_ns;      // start of the bar that made it
  std::uint64_t confirmed_ns;  // end of the bar that confirmed it: when anyone could know
  std::optional<int> change;  // vs the previous swing of the same kind: +1 higher, -1 lower,
                              // 0 equal; nullopt for the first
};

class ZigZag {
 public:
  // Closed bars in time order. delta_x2: the pullback that confirms a swing, doubled like
  // prices; <= 0 (no ATR yet) tracks the extremes but confirms nothing. Returns the swing
  // this bar confirmed, if any (at most one per bar).
  std::optional<Swing> on_bar(const Bar& bar, double delta_x2);

  [[nodiscard]] const std::vector<Swing>& swings() const { return swings_; }
  // nullopt until two highs and two lows are confirmed.
  [[nodiscard]] std::optional<int> score() const { return score_; }

 private:
  // Which extreme we are following: Up follows the highest high since the last swing low,
  // Down the lowest low since the last swing high. None until the first swing.
  enum class Leg : std::uint8_t { None, Up, Down };

  Leg leg_ = Leg::None;
  bool started_ = false;
  Price high_x2_ = 0;  // the candidate extremes and the bars that made them
  Price low_x2_ = 0;
  std::uint64_t high_time_ = 0;
  std::uint64_t low_time_ = 0;
  // The last two confirmed swings of one kind: all the score needs.
  struct LastTwo {
    std::optional<Price> older;
    std::optional<Price> newer;
  };

  std::vector<Swing> swings_;
  LastTwo highs_;
  LastTwo lows_;
  std::optional<int> score_;

  Swing confirm(SwingKind kind, Price price_x2, std::uint64_t time_ns, std::uint64_t confirmed_ns);
};

struct StructureSettings {
  std::size_t atr_n = 14;
  double c = 2.0;  // delta = c x ATR: a swing is a pullback of c average bars
};

// A ZigZag whose delta follows the market: c x ATR of the same bars, so a quiet market has
// smaller swings and a wild one bigger. The same rule on 15 s, 1 min and 5 min bars gives
// each chart's own structure (the fractal hypothesis).
class MarketStructure {
 public:
  explicit MarketStructure(StructureSettings settings = {});
  std::optional<Swing> on_bar(const Bar& bar);

  [[nodiscard]] const std::vector<Swing>& swings() const { return zigzag_.swings(); }
  [[nodiscard]] std::optional<int> score() const { return zigzag_.score(); }
  [[nodiscard]] std::optional<double> atr() const { return atr_.value(); }  // ticks
  [[nodiscard]] double c() const { return settings_.c; }

 private:
  StructureSettings settings_;
  Atr atr_;
  ZigZag zigzag_;
};

}  // namespace ob::sim
