#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

// How much the mid moves: the sigma^2 in Avellaneda-Stoikov.
//
// The mid is sampled on a fixed clock grid (every `interval_ns`, aligned to whole
// intervals since midnight), and the estimate is the average squared change between
// grid points over the last `window` intervals, scaled to one second.
//
// Why a grid instead of every tick: the estimate depends on the time scale it's measured
// at (D29). A fixed grid makes that scale an explicit choice, ignores flicker shorter
// than one interval, and makes every interval count once however busy it was.

namespace ob::sim {

class VolatilityEstimator {
 public:
  // interval_ns and window must be > 0 (they're clamped to 1).
  VolatilityEstimator(std::uint64_t interval_ns, std::size_t window);

  // The mid at time t (twice the mid, as in MidPoint). Times must not go backwards.
  // Calling it again with an unchanged mid is fine: it moves the clock, so a quiet
  // market counts as zero changes instead of being skipped.
  void on_mid(std::uint64_t t_ns, Price mid_x2);

  // Variance of the mid in ticks^2 per second. nullopt until `window` changes are in.
  [[nodiscard]] std::optional<double> variance_per_second() const;

  [[nodiscard]] std::size_t samples() const { return squares_.size(); }

 private:
  std::uint64_t interval_ns_;
  std::size_t window_;
  std::optional<Price> current_;      // mid in force right now
  std::optional<Price> last_sample_;  // mid at the previous grid point
  std::uint64_t next_sample_ns_ = 0;  // the next grid point to sample
  std::deque<std::int64_t> squares_;  // squared mid_x2 change of each of the last intervals
  std::int64_t sum_ = 0;              // running sum of squares_, so the average is O(1)

  void sample(Price mid_x2);
};

}  // namespace ob::sim
