#pragma once

#include "orderbook/sim/markout.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

// Our fills as CSV, for the research side (Python) to analyse (D37). Raw prices only,
// no derived P&L: the analysis decides how to combine them.
//
//   timestamp_ns, side (B/S), price, quantity,
//   mid_x2_before,                    twice the mid just before the fill (D35)
//   mid_x2_1s, mid_x2_10s, mid_x2_60s twice the mid that long after, capped at end_ns
//   touch_share, shares_left          the book on our side at the fill (D33)
//   ema_fast, ema_slow, sma, bb_pct, rsi, stoch_k, stoch_d, for _1s then _1m, and vwap:
//                                     the charts at the fill (D45), empty when not there
//   structure_1s, _15s, _1m, _5m      the market structure score at the fill (D49)
//
// Prices are in ticks ($0.0001), mids doubled so they stay integers; the chart prices are
// ticks to 2 places (not doubled).

namespace ob::sim {

void write_fills_csv(std::ostream& out, const std::vector<Fill>& fills,
                     const std::vector<MidPoint>& mids, std::uint64_t end_ns);

}  // namespace ob::sim
