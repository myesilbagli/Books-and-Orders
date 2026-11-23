#pragma once

#include "orderbook/order.hpp"
#include "orderbook/sim/chart_feed.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Markouts: how good was a fill, judged by where the price went afterwards.
//   buy:  mid(t + h) - fill price        sell: fill price - mid(t + h)
// Positive means we traded on the right side of where the price went. At h = 0 a fill at
// the touch shows about half the spread; as h grows, adverse selection pulls it down.

namespace ob::sim {

struct Fill {
  std::uint64_t timestamp_ns;
  Side side;  // Buy: our bid was hit
  Price price;  // our quote's price
  Quantity quantity;
  // The book on our side at the moment of the fill, before the execution is applied (D33):
  double touch_share = 0.0;       // our side's size at the touch / both sides' sizes there
  std::uint64_t shares_left = 0;  // real shares left at our price after this execution
  Price mid_x2_before = 0;        // twice the mid just before the fill: the spread we earned
  // The charts as the strategy could see them at the fill (D45, D49): the last closed bar
  // of each timeframe with its indicators and structure, and the VWAP of the trades
  // before this one. Empty outside the chart window or while nothing has closed yet.
  std::optional<ChartPoint> chart_1s;
  std::optional<ChartPoint> chart_15s;
  std::optional<ChartPoint> chart_1m;
  std::optional<ChartPoint> chart_5m;
  std::optional<double> vwap;  // ticks
};

// One change of the mid. Stored as bid + ask (twice the mid) so it stays an integer when
// the spread is an odd number of ticks.
struct MidPoint {
  std::uint64_t timestamp_ns;
  Price mid_x2;
};

// The mid in force at time t: the last change at or before t. nullopt before the first.
// `mids` must be in time order.
[[nodiscard]] std::optional<Price> mid_x2_at(const std::vector<MidPoint>& mids, std::uint64_t t);

// Markout of one fill `horizon_ns` after it, in ticks per share; nullopt if no mid.
[[nodiscard]] std::optional<double> markout_ticks(const Fill& fill,
                                                  const std::vector<MidPoint>& mids,
                                                  std::uint64_t horizon_ns);

// Share-weighted average markout of many fills, in ticks per share: a 300-share fill
// counts three times a 100-share one. Fills with no mid are left out; nullopt if none.
[[nodiscard]] std::optional<double> average_markout_ticks(const std::vector<Fill>& fills,
                                                          const std::vector<MidPoint>& mids,
                                                          std::uint64_t horizon_ns);

}  // namespace ob::sim
