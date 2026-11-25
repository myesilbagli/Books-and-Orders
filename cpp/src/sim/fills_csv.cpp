#include "orderbook/sim/fills_csv.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ob::sim {

namespace {

// ",value" to `places` decimals, or just "," when there is none. Formatted in its own
// stream: setting std::fixed on `out` would change how the older columns (touch_share)
// print, and runs before this change must stay byte-identical.
void write_field(std::ostream& out, std::optional<double> v, int places) {
  out << ',';
  if (v) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(places) << *v;
    out << s.str();
  }
}

// One timeframe's indicators: prices in ticks to 2 places, %B to 3, RSI and stoch to 2.
void write_chart(std::ostream& out, const std::optional<ChartPoint>& point) {
  const ChartPoint none;  // all nullopt: the same empty fields when there is no point
  const ChartPoint& p = point ? *point : none;
  write_field(out, p.ema_fast, 2);
  write_field(out, p.ema_slow, 2);
  write_field(out, p.sma, 2);
  write_field(out, p.bollinger ? std::optional<double>(p.bollinger->percent_b) : std::nullopt, 3);
  write_field(out, p.rsi, 2);
  write_field(out, p.stoch_rsi ? std::optional<double>(p.stoch_rsi->k) : std::nullopt, 2);
  write_field(out, p.stoch_rsi ? std::optional<double>(p.stoch_rsi->d) : std::nullopt, 2);
}

// The structure score (D49): an integer from -2 to +2, or empty before there is one.
void write_structure(std::ostream& out, const std::optional<ChartPoint>& point) {
  out << ',';
  if (point && point->structure) {
    out << *point->structure;
  }
}

// The efficiency ratio (D57), 0 to 1, to 3 places; empty while warming up.
void write_er(std::ostream& out, const std::optional<ChartPoint>& point) {
  write_field(out, point ? point->er : std::nullopt, 3);
}

}  // namespace

void write_fills_csv(std::ostream& out, const std::vector<Fill>& fills,
                     const std::vector<MidPoint>& mids, std::uint64_t end_ns) {
  constexpr std::uint64_t kSecond = 1'000'000'000;
  constexpr std::uint64_t kHorizons[] = {kSecond, 10 * kSecond, 60 * kSecond};

  out << "timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
         "touch_share,shares_left,"
         "ema_fast_1s,ema_slow_1s,sma_1s,bb_pct_1s,rsi_1s,stoch_k_1s,stoch_d_1s,"
         "ema_fast_1m,ema_slow_1m,sma_1m,bb_pct_1m,rsi_1m,stoch_k_1m,stoch_d_1m,vwap,"
         "structure_1s,structure_15s,structure_1m,structure_5m,er_1s,er_15s,er_1m,er_5m,b1,b2\n";
  for (const Fill& f : fills) {
    out << f.timestamp_ns << ',' << (f.side == Side::Buy ? 'B' : 'S') << ',' << f.price << ','
        << f.quantity << ',' << f.mid_x2_before;
    for (const std::uint64_t h : kHorizons) {
      // Same rule as the P&L breakdown (D35): never look past the end of quoting.
      const std::uint64_t later = std::min(f.timestamp_ns + h, end_ns);
      out << ',' << mid_x2_at(mids, later).value_or(f.mid_x2_before);
    }
    out << ',' << f.touch_share << ',' << f.shares_left;
    write_chart(out, f.chart_1s);
    write_chart(out, f.chart_1m);
    write_field(out, f.vwap, 2);
    write_structure(out, f.chart_1s);
    write_structure(out, f.chart_15s);
    write_structure(out, f.chart_1m);
    write_structure(out, f.chart_5m);
    // New columns go last, so every earlier column stays byte-identical.
    write_er(out, f.chart_1s);
    write_er(out, f.chart_15s);
    write_er(out, f.chart_1m);
    write_er(out, f.chart_5m);
    out << ',' << f.b1 << ',' << f.b2;  // the turn reads at the fill (D63)
    out << '\n';
  }
}

}  // namespace ob::sim
