#pragma once

#include <cstdint>

namespace ob {

// Price in integer ticks, never float: 0.1 + 0.2 != 0.3 in binary floating point,
// so two orders at "the same" price could fail to match. Signed 64-bit leaves room
// for any instrument and for price differences (which can be negative).
using Price = std::int64_t;

// Shares. Unsigned because a negative quantity has no meaning; 32 bits covers any
// single order (ITCH uses 32-bit share counts too). The cost: subtraction can wrap,
// so every decrease goes through fill(), which checks first.
using Quantity = std::uint32_t;

using OrderId = std::uint64_t;

// Arrival counter assigned when the order reaches the book. A counter rather than a
// clock: two orders can share a timestamp and clocks can jump, a counter is always strict.
using Sequence = std::uint64_t;

// enum class: no silent conversion to int, so a Side can't be mixed up with a number.
// uint8_t underlying type keeps it to 1 byte inside Order.
enum class Side : std::uint8_t { Buy, Sell };

// A plain aggregate (no constructor) so tests and callers can use designated
// initializers: Order{.id = 1, .side = Side::Buy, ...}.
struct Order {
  OrderId id;
  Side side;
  Price price;
  Quantity remaining;  // shares still open; drops on every fill
  Sequence seq;

  // Returns false and changes nothing if qty is more than what's left. The matcher
  // should never ask for that, but refusing beats wrapping to ~4 billion shares.
  [[nodiscard]] bool fill(Quantity qty) {
    if (qty > remaining) {
      return false;
    }
    remaining -= qty;  // safe: checked qty <= remaining above
    return true;
  }

  [[nodiscard]] bool is_filled() const { return remaining == 0; }
};

// Time priority within one price level: who got there first.
// Strict (<, not <=) so an order never comes "before" itself; sorted containers need that.
[[nodiscard]] inline bool arrived_before(const Order& a, const Order& b) { return a.seq < b.seq; }

}  // namespace ob
