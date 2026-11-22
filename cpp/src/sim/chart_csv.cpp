#include "orderbook/sim/chart_csv.hpp"

#include <cstdint>
#include <iomanip>

namespace ob::sim {

namespace {

constexpr double kTicksPerDollar = 10'000.0;

// HH:MM:SS from nanoseconds since midnight (ITCH time is already Eastern).
void write_time(std::ostream& out, std::uint64_t t_ns) {
  const std::uint64_t s = t_ns / 1'000'000'000;
  const std::uint64_t parts[] = {s / 3600, s / 60 % 60, s % 60};
  for (int i = 0; i < 3; ++i) {
    out << (i > 0 ? ":" : "") << (parts[i] < 10 ? "0" : "") << parts[i];
  }
}

// ",value" in the given unit, or just "," while the indicator is warming up.
void write_field(std::ostream& out, std::optional<double> v, double divide_by, int places) {
  out << ',';
  if (v) {
    out << std::fixed << std::setprecision(places) << *v / divide_by;
  }
}

void write_price(std::ostream& out, std::optional<double> ticks) {
  write_field(out, ticks, kTicksPerDollar, 4);
}

}  // namespace

void write_chart_header(std::ostream& out) {
  out << "timeframe,time_et,open,high,low,close,volume,vwap,ema_fast,ema_slow,sma,"
         "bb_upper,bb_lower,bb_pct,rsi,stoch_k,stoch_d\n";
}

void write_chart_row(std::ostream& out, const std::string& timeframe, const ChartPoint& point,
                     std::optional<double> vwap_ticks) {
  const Bar& b = point.bar;
  out << timeframe << ',';
  write_time(out, b.start_ns);
  // Bars hold the doubled mid; halve it back to ticks.
  for (const Price x2 : {b.open_x2, b.high_x2, b.low_x2, b.close_x2}) {
    write_price(out, static_cast<double>(x2) / 2.0);
  }
  out << ',' << b.volume;
  write_price(out, vwap_ticks);
  write_price(out, point.ema_fast);
  write_price(out, point.ema_slow);
  write_price(out, point.sma);
  const std::optional<BollingerValue>& bb = point.bollinger;
  write_price(out, bb ? std::optional<double>(bb->upper) : std::nullopt);
  write_price(out, bb ? std::optional<double>(bb->lower) : std::nullopt);
  write_field(out, bb ? std::optional<double>(bb->percent_b) : std::nullopt, 1.0, 3);
  write_field(out, point.rsi, 1.0, 2);
  const std::optional<StochRsiValue>& st = point.stoch_rsi;
  write_field(out, st ? std::optional<double>(st->k) : std::nullopt, 1.0, 2);
  write_field(out, st ? std::optional<double>(st->d) : std::nullopt, 1.0, 2);
  out << '\n';
}

}  // namespace ob::sim
