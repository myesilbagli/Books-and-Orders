#pragma once

#include "orderbook/sim/chart_feed.hpp"
#include "orderbook/sim/structure.hpp"

#include <optional>
#include <ostream>
#include <string>

// Chart bars and indicators as CSV, in chart units, to compare with a trading chart (D41):
//
//   timeframe, time_et (bar start, HH:MM:SS), open, high, low, close (dollars, the mid),
//   volume (shares), vwap, ema_fast, ema_slow, sma, bb_upper, bb_lower (dollars),
//   bb_pct (0 = lower band, 1 = upper), rsi, stoch_k, stoch_d (0-100),
//   atr (dollars), structure (-2 down, +2 up, else range; D49), er (D57),
//   ema_mid, ema_long (dollars), b1, b2 (the turn reads a 15 s bar completed, +1 / -1 / 0;
//   empty on the other timeframes; D63)
//
// An indicator still warming up is an empty field, not a zero.

namespace ob::sim {

void write_chart_header(std::ostream& out);

// vwap_ticks: VWAP when the bar closed (nullopt before the first trade it counts).
void write_chart_row(std::ostream& out, const std::string& timeframe, const ChartPoint& point,
                     std::optional<double> vwap_ticks);

// The swings, for drawing the structure (D49):
//   timeframe, kind (H/L), price (dollars), time_et (the bar that made it),
//   confirmed_et (the end of the bar that confirmed it), change (+1 higher, -1 lower,
//   0 equal than the last of its kind; empty for the first)
void write_swings_header(std::ostream& out);
void write_swing_row(std::ostream& out, const std::string& timeframe, const Swing& swing);

}  // namespace ob::sim
