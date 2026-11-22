#pragma once

#include "orderbook/sim/markout.hpp"

#include <cstdint>
#include <vector>

// Where the P&L came from (D35). For one fill of q shares at price p, with s = +1 for a
// buy and -1 for a sell, its mark-to-market at the end splits exactly into three:
//
//   s q (mid_end - p) =  s q (mid_before - p)          spread: how far from the mid we got
//                      + s q (mid_after_h - mid_before)  adverse selection: the move in h
//                      + s q (mid_end - mid_after_h)     inventory: holding it after that
//
// mid_before is the mid just before the fill, mid_after_h the mid h later (capped at the
// end of quoting, D28), mid_end the mid when quoting ended. Summed over all fills, the
// three parts add up to the total P&L. The rebate the exchange pays makers is separate,
// on top: rebate per share x shares traded.
//
// Everything is in ticks x shares (1 = $0.0001).

namespace ob::sim {

struct PnlBreakdown {
  double spread = 0.0;
  double adverse_selection = 0.0;
  double inventory = 0.0;
  double total = 0.0;   // mark-to-market before the rebate: cash + position x mid_end
  double rebate = 0.0;  // on top of total
  std::uint64_t shares = 0;
};

[[nodiscard]] PnlBreakdown pnl_breakdown(const std::vector<Fill>& fills,
                                         const std::vector<MidPoint>& mids,
                                         std::uint64_t horizon_ns, std::uint64_t end_ns,
                                         double rebate_ticks_per_share);

}  // namespace ob::sim
