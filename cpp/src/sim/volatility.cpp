#include "orderbook/sim/volatility.hpp"

#include <algorithm>

namespace ob::sim {

VolatilityEstimator::VolatilityEstimator(std::uint64_t interval_ns, std::size_t window)
    // A zero interval would loop forever in on_mid; a zero window would never be "full".
    : interval_ns_(std::max<std::uint64_t>(interval_ns, 1)),
      window_(std::max<std::size_t>(window, 1)) {}

void VolatilityEstimator::on_mid(std::uint64_t t_ns, Price mid_x2) {
  if (!current_) {
    // First mid: the first grid point is the first whole interval at or after it.
    next_sample_ns_ = (t_ns + interval_ns_ - 1) / interval_ns_ * interval_ns_;
  } else {
    // Every grid point strictly before t has passed with `current_` in force. A change
    // exactly on a grid point lands below, before that point is sampled, so it counts
    // as in force there (the same "at or before" rule as mid_x2_at).
    while (next_sample_ns_ < t_ns) {
      sample(*current_);
      next_sample_ns_ += interval_ns_;
    }
  }
  current_ = mid_x2;
}

void VolatilityEstimator::sample(Price mid_x2) {
  if (last_sample_) {
    const std::int64_t change = mid_x2 - *last_sample_;
    squares_.push_back(change * change);
    sum_ += change * change;
    if (squares_.size() > window_) {  // keep only the last `window` intervals
      sum_ -= squares_.front();
      squares_.pop_front();
    }
  }
  last_sample_ = mid_x2;
}

std::optional<double> VolatilityEstimator::variance_per_second() const {
  if (squares_.size() < window_) {
    return std::nullopt;
  }
  // Average squared change, assuming the mean change is zero: over a few seconds the
  // drift is tiny next to the noise, and estimating it would only add error.
  const double per_interval_x2 = static_cast<double>(sum_) / static_cast<double>(window_);
  // mid_x2 is twice the mid, so its squared changes are 4x those of the mid.
  const double per_interval = per_interval_x2 / 4.0;
  const double seconds = static_cast<double>(interval_ns_) / 1e9;
  return per_interval / seconds;  // variance grows linearly with time (random walk)
}

}  // namespace ob::sim
