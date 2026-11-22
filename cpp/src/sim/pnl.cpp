#include "orderbook/sim/pnl.hpp"

#include <algorithm>

namespace ob::sim {

PnlBreakdown pnl_breakdown(const std::vector<Fill>& fills, const std::vector<MidPoint>& mids,
                           std::uint64_t horizon_ns, std::uint64_t end_ns,
                           double rebate_ticks_per_share) {
  PnlBreakdown b;
  // With no mid at the end we never quoted, so there are no fills and 0 is never used.
  const double mid_end = static_cast<double>(mid_x2_at(mids, end_ns).value_or(0)) / 2.0;

  double cash = 0.0;      // computed the plain way too, as a check on the three parts
  double position = 0.0;
  for (const Fill& f : fills) {
    const double s = f.side == Side::Buy ? 1.0 : -1.0;
    const auto q = static_cast<double>(f.quantity);
    const auto price = static_cast<double>(f.price);
    const double mid_before = static_cast<double>(f.mid_x2_before) / 2.0;
    // Capped at the end of quoting: after it the book thins out and its mid means
    // nothing (D28). A fill right before the end has a shorter horizon; that's all.
    const std::uint64_t later = std::min(f.timestamp_ns + horizon_ns, end_ns);
    const double mid_after =
        static_cast<double>(mid_x2_at(mids, later).value_or(f.mid_x2_before)) / 2.0;

    b.spread += s * q * (mid_before - price);
    b.adverse_selection += s * q * (mid_after - mid_before);
    b.inventory += s * q * (mid_end - mid_after);

    cash -= s * q * price;  // buying spends, selling receives
    position += s * q;
    b.shares += f.quantity;
  }
  // Mark-to-market. Equal to spread + adverse + inventory by construction of the three
  // terms (they telescope to mid_end - price); the tests check both stay in step.
  b.total = cash + position * mid_end;
  b.rebate = rebate_ticks_per_share * static_cast<double>(b.shares);
  return b;
}

}  // namespace ob::sim
