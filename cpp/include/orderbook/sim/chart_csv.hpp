#pragma once

#include "orderbook/sim/chart_feed.hpp"

#include <optional>
#include <ostream>
#include <string>

// Chart bars and indicators as CSV, in chart units, to compare with a trading chart (D41):
//
//   timeframe, time_et (bar start, HH:MM:SS), open, high, low, close (dollars, the mid),
//   volume (shares), vwap, ema_fast, ema_slow, sma, bb_upper, bb_lower (dollars),
//   bb_pct (0 = lower band, 1 = upper), rsi, stoch_k, stoch_d (0-100)
//
// An indicator still warming up is an empty field, not a zero.

namespace ob::sim {

void write_chart_header(std::ostream& out);

// vwap_ticks: VWAP when the bar closed (nullopt before the first trade it counts).
void write_chart_row(std::ostream& out, const std::string& timeframe, const ChartPoint& point,
                     std::optional<double> vwap_ticks);

}  // namespace ob::sim
