#include "orderbook/sim/fills_csv.hpp"

#include <algorithm>

namespace ob::sim {

void write_fills_csv(std::ostream& out, const std::vector<Fill>& fills,
                     const std::vector<MidPoint>& mids, std::uint64_t end_ns) {
  constexpr std::uint64_t kSecond = 1'000'000'000;
  constexpr std::uint64_t kHorizons[] = {kSecond, 10 * kSecond, 60 * kSecond};

  out << "timestamp_ns,side,price,quantity,mid_x2_before,mid_x2_1s,mid_x2_10s,mid_x2_60s,"
         "touch_share,shares_left\n";
  for (const Fill& f : fills) {
    out << f.timestamp_ns << ',' << (f.side == Side::Buy ? 'B' : 'S') << ',' << f.price << ','
        << f.quantity << ',' << f.mid_x2_before;
    for (const std::uint64_t h : kHorizons) {
      // Same rule as the P&L breakdown (D35): never look past the end of quoting.
      const std::uint64_t later = std::min(f.timestamp_ns + h, end_ns);
      out << ',' << mid_x2_at(mids, later).value_or(f.mid_x2_before);
    }
    out << ',' << f.touch_share << ',' << f.shares_left << '\n';
  }
}

}  // namespace ob::sim
