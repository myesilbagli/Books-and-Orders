#pragma once

#include "orderbook/order.hpp"
#include "orderbook/replay.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

// Trades (prints) from ITCH, for volume and VWAP (D40). A trade on Nasdaq shows up as:
//   E  a displayed order executed at its own price (the price is the resting order's)
//   C  a displayed order executed at another price; skipped when not printable, since
//      Nasdaq has counted that volume elsewhere
//   P  a hidden order executed (the only trace hidden liquidity leaves)
//   Q  an auction's single print (the opening and closing crosses); the orders it filled
//      get non-printable C messages, which is why those are skipped

namespace ob::sim {

struct Trade {
  std::uint64_t timestamp_ns = 0;
  std::uint16_t locate = 0;
  Price price = 0;  // ticks
  Quantity shares = 0;
};

// The trade in this message, or nullopt if it isn't one. Call it BEFORE the replay applies
// the message: an E's price comes from the resting order, which the book may then remove.
[[nodiscard]] std::optional<Trade> trade_of(const std::uint8_t* p, std::size_t len,
                                            const Replayer& replay);

}  // namespace ob::sim
