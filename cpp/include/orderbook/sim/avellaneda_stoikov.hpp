#pragma once

#include "orderbook/sim/strategy.hpp"
#include "orderbook/sim/volatility.hpp"

#include <cstddef>
#include <cstdint>

// Avellaneda-Stoikov (2008) quoting, with a fixed holding time instead of "time left
// until the close" (D31). All prices in ticks ($0.0001).
//
//   risk        = gamma * sigma^2 * H              ticks: what holding one lot costs us
//   reservation = mid - q * risk                   our own fair price, leaning against q
//   half spread = base + risk / 2                  wider when holding is riskier
//   bid, ask    = reservation -/+ half spread, rounded to the nearest price tick
//
// q is inventory in lots (one lot = one quote), sigma^2 the mid's variance per second.

namespace ob::sim {

struct AsConfig {
  double gamma;                          // risk aversion, per tick
  double holding_s = 10.0;               // H: how long we expect to hold a position
  double base_half_spread_ticks = 50.0;  // half a cent: flat, we join a 1-cent market
  std::int64_t lot = 100;                // shares per unit of q (our quote size), > 0
  Price tick_size = 100;                 // the price grid: $0.01 for stocks over $1
  std::uint64_t vol_interval_ns = 1'000'000'000;  // volatility grid: 1 s (D29)
  std::size_t vol_window = 300;                   // over the last 5 minutes
};

// The formula on its own, so it can be tested without feeding a volatility history.
[[nodiscard]] Quotes as_quotes(Price mid_x2, double q_lots, double variance_per_s,
                               const AsConfig& config);

class AvellanedaStoikov final : public Strategy {
 public:
  explicit AvellanedaStoikov(const AsConfig& config);

  // Updates the volatility estimate, then quotes; no quotes until the estimate is ready.
  Quotes quote(const MarketView& view) override;

  [[nodiscard]] const VolatilityEstimator& volatility() const { return vol_; }

 private:
  AsConfig config_;
  VolatilityEstimator vol_;
};

}  // namespace ob::sim
