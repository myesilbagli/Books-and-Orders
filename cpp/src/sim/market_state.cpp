#include "orderbook/sim/market_state.hpp"

#include <algorithm>

namespace ob::sim {

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;

void append(std::vector<ChartPoint>& to, const std::vector<ChartPoint>& from) {
  to.insert(to.end(), from.begin(), from.end());
}

// The shared chart settings with one timeframe's own swing size.
ChartSettings with_c(ChartSettings settings, double c) {
  settings.structure.c = c;  // settings is a copy: the caller's stays as it was
  return settings;
}

}  // namespace

MarketState::MarketState(const MarketStateConfig& config)
    : config_(config),
      one_second_(kSecond, with_c(config.settings, config.swing_c.one_second)),
      fifteen_seconds_(15 * kSecond, with_c(config.settings, config.swing_c.fifteen_seconds)),
      one_minute_(60 * kSecond, with_c(config.settings, config.swing_c.one_minute)),
      five_minutes_(300 * kSecond, with_c(config.settings, config.swing_c.five_minutes)),
      vwap_(config.vwap_from_ns) {}

void MarketState::start_if_due(std::uint64_t t_ns) {
  if (started_ || t_ns < config_.from_ns) {
    return;
  }
  started_ = true;
  if (carried_mid_x2_) {
    append(pending_.one_second, one_second_.on_mid(config_.from_ns, *carried_mid_x2_));
    append(pending_.fifteen_seconds, fifteen_seconds_.on_mid(config_.from_ns, *carried_mid_x2_));
    append(pending_.one_minute, one_minute_.on_mid(config_.from_ns, *carried_mid_x2_));
    append(pending_.five_minutes, five_minutes_.on_mid(config_.from_ns, *carried_mid_x2_));
  }
}

ClosedBars MarketState::advance(std::uint64_t t_ns) {
  start_if_due(t_ns);
  if (t_ns >= config_.from_ns) {
    // Past the end, close only up to the end: the bars after it don't exist.
    const std::uint64_t until = std::min(t_ns, config_.to_ns);
    append(pending_.one_second, one_second_.advance(until));
    append(pending_.fifteen_seconds, fifteen_seconds_.advance(until));
    append(pending_.one_minute, one_minute_.advance(until));
    append(pending_.five_minutes, five_minutes_.advance(until));
  }
  // Hand out everything closed since the last call, including bars an on_trade or on_mid
  // closed on the way (if advance wasn't called first), so none is ever lost.
  ClosedBars out = pending_;  // a copy: a handful of bars at most
  pending_ = {};
  return out;
}

void MarketState::on_trade(const Trade& trade) {
  start_if_due(trade.timestamp_ns);
  if (!inside(trade.timestamp_ns)) {
    return;
  }
  vwap_.on_trade(trade.timestamp_ns, trade.price, trade.shares);
  append(pending_.one_second, one_second_.on_trade(trade.timestamp_ns, trade.shares));
  append(pending_.fifteen_seconds, fifteen_seconds_.on_trade(trade.timestamp_ns, trade.shares));
  append(pending_.one_minute, one_minute_.on_trade(trade.timestamp_ns, trade.shares));
  append(pending_.five_minutes, five_minutes_.on_trade(trade.timestamp_ns, trade.shares));
}

void MarketState::on_mid(std::uint64_t t_ns, Price mid_x2) {
  if (t_ns < config_.from_ns) {
    carried_mid_x2_ = mid_x2;  // not charted yet, but it may still be in force at the start
    return;
  }
  start_if_due(t_ns);
  if (!inside(t_ns)) {
    return;
  }
  append(pending_.one_second, one_second_.on_mid(t_ns, mid_x2));
  append(pending_.fifteen_seconds, fifteen_seconds_.on_mid(t_ns, mid_x2));
  append(pending_.one_minute, one_minute_.on_mid(t_ns, mid_x2));
  append(pending_.five_minutes, five_minutes_.on_mid(t_ns, mid_x2));
}

}  // namespace ob::sim
