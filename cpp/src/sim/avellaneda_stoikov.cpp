#include "orderbook/sim/avellaneda_stoikov.hpp"

#include <cmath>

namespace ob::sim {

namespace {

// Nearest multiple of `tick`. Not always down for bids and up for asks: that would step
// back from the touch even when flat, whenever the extra width is a fraction of a tick.
Price round_to_tick(double price, Price tick) {
  return static_cast<Price>(std::llround(price / static_cast<double>(tick))) * tick;
}

}  // namespace

Quotes as_quotes(Price mid_x2, double q_lots, double variance_per_s, const AsConfig& config) {
  const double mid = static_cast<double>(mid_x2) / 2.0;
  // ticks: gamma (1/tick) x variance (ticks^2/s) x H (s). The price risk of one lot
  // held for H, weighted by how much we mind risk.
  const double risk = config.gamma * variance_per_s * config.holding_s;
  const double reservation = mid - q_lots * risk;
  // AS's spread term (2/gamma) ln(1 + gamma/k) is about 2/k for small gamma; the fixed
  // base stands in for it (D31). risk / 2 is the inventory-risk widening.
  const double half_spread = config.base_half_spread_ticks + risk / 2.0;
  return Quotes{.bid = round_to_tick(reservation - half_spread, config.tick_size),
                .ask = round_to_tick(reservation + half_spread, config.tick_size)};
}

AvellanedaStoikov::AvellanedaStoikov(const AsConfig& config)
    : config_(config), vol_(config.vol_interval_ns, config.vol_window) {}

Quotes AvellanedaStoikov::quote(const MarketView& view) {
  // Every call moves the volatility clock, also before quoting hours: that's the warm-up.
  vol_.on_mid(view.now_ns, view.best_bid + view.best_ask);
  const auto variance = vol_.variance_per_second();
  if (!variance) {
    return {};  // no estimate yet: don't guess, don't quote
  }
  const double q_lots = static_cast<double>(view.inventory) / static_cast<double>(config_.lot);
  return as_quotes(view.best_bid + view.best_ask, q_lots, *variance, config_);
}

}  // namespace ob::sim
