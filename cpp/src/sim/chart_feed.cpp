#include "orderbook/sim/chart_feed.hpp"

namespace ob::sim {

ChartFeed::ChartFeed(std::uint64_t interval_ns, ChartSettings settings)
    : bars_(interval_ns),
      ema_fast_(settings.ema_fast),
      ema_slow_(settings.ema_slow),
      sma_(settings.sma),
      bollinger_(settings.bollinger_n, settings.bollinger_k),
      rsi_(settings.rsi),
      stoch_rsi_(settings.rsi, settings.stoch, settings.stoch_k, settings.stoch_d),
      structure_(settings.structure),
      er_(settings.efficiency) {}

std::vector<ChartPoint> ChartFeed::on_mid(std::uint64_t t_ns, Price mid_x2) {
  return close(bars_.on_mid(t_ns, mid_x2));
}

std::vector<ChartPoint> ChartFeed::on_trade(std::uint64_t t_ns, std::uint64_t shares) {
  return close(bars_.on_trade(t_ns, shares));
}

std::vector<ChartPoint> ChartFeed::advance(std::uint64_t t_ns) {
  return close(bars_.advance(t_ns));
}

std::vector<ChartPoint> ChartFeed::close(const std::vector<Bar>& closed) {
  std::vector<ChartPoint> points;
  points.reserve(closed.size());
  for (const Bar& bar : closed) {
    // Indicators work in ticks; bars keep the doubled mid (MidPoint's convention).
    const double close = static_cast<double>(bar.close_x2) / 2.0;
    ema_fast_.update(close);
    ema_slow_.update(close);
    sma_.update(close);
    bollinger_.update(close);
    stoch_rsi_.update(close);  // has its own RSI inside
    rsi_.update(close);
    er_.update(close);
    structure_.on_bar(bar);  // ATR and swings use the bar's high and low, not just its close
    // Read every value right after this bar, before the next one: a quiet stretch closes
    // several bars at once, and each must show the indicators as they were then.
    points.push_back(ChartPoint{.bar = bar,
                                .ema_fast = ema_fast_.value(),
                                .ema_slow = ema_slow_.value(),
                                .sma = sma_.value(),
                                .bollinger = bollinger_.value(),
                                .rsi = rsi_.value(),
                                .stoch_rsi = stoch_rsi_.value(),
                                .atr = structure_.atr(),
                                .structure = structure_.score(),
                                .er = er_.value()});
  }
  if (!points.empty()) {
    latest_ = points.back();
  }
  return points;
}

}  // namespace ob::sim
