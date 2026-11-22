#pragma once

#include "orderbook/order.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Markouts: how good was a fill, judged by where the price went afterwards.
//   buy:  mid(t + h) - fill price        sell: fill price - mid(t + h)
// Positive means we traded on the right side of where the price went. At h = 0 a fill at
// the touch shows about half the spread; as h grows, adverse selection pulls it down.

namespace ob::sim {

struct Fill {
  std::uint64_t timestamp_ns;
  Side side;  // Buy: our bid was hit
  Price price;  // our quote's price
  Quantity quantity;
};

// One change of the mid. Stored as bid + ask (twice the mid) so it stays an integer when
// the spread is an odd number of ticks.
struct MidPoint {
  std::uint64_t timestamp_ns;
  Price mid_x2;
};

// The mid in force at time t: the last change at or before t. nullopt before the first.
// `mids` must be in time order.
[[nodiscard]] std::optional<Price> mid_x2_at(const std::vector<MidPoint>& mids, std::uint64_t t);

// Markout of one fill `horizon_ns` after it, in ticks per share; nullopt if no mid.
[[nodiscard]] std::optional<double> markout_ticks(const Fill& fill,
                                                  const std::vector<MidPoint>& mids,
                                                  std::uint64_t horizon_ns);

}  // namespace ob::sim
