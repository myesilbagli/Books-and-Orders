#include "orderbook/sim/markout.hpp"

#include <cstddef>

namespace ob::sim {

std::optional<Price> mid_x2_at(const std::vector<MidPoint>& mids, std::uint64_t t) {
  // Binary search: halve the range each step, so ~20 steps for a million mids instead of
  // walking them all. Invariant: everything before `lo` is at or before t, everything
  // from `hi` on is after t. When they meet, lo is the first point after t.
  std::size_t lo = 0;
  std::size_t hi = mids.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;  // not (lo + hi) / 2, which can overflow
    if (mids[mid].timestamp_ns <= t) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0) {
    return std::nullopt;  // every point is after t
  }
  return mids[lo - 1].mid_x2;  // the last change at or before t
}

std::optional<double> markout_ticks(const Fill& fill, const std::vector<MidPoint>& mids,
                                    std::uint64_t horizon_ns) {
  const auto mid_x2 = mid_x2_at(mids, fill.timestamp_ns + horizon_ns);
  if (!mid_x2) {
    return std::nullopt;
  }
  const double mid = static_cast<double>(*mid_x2) / 2.0;
  const auto price = static_cast<double>(fill.price);
  return fill.side == Side::Buy ? mid - price : price - mid;
}

std::optional<double> average_markout_ticks(const std::vector<Fill>& fills,
                                            const std::vector<MidPoint>& mids,
                                            std::uint64_t horizon_ns) {
  double total = 0.0;
  std::uint64_t shares = 0;
  for (const Fill& f : fills) {
    if (const auto mk = markout_ticks(f, mids, horizon_ns)) {
      total += *mk * static_cast<double>(f.quantity);
      shares += f.quantity;
    }
  }
  if (shares == 0) {
    return std::nullopt;
  }
  return total / static_cast<double>(shares);
}

}  // namespace ob::sim
