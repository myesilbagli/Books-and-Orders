#pragma once

#include "orderbook/book.hpp"

#include <cstdint>

// Our own simulated (virtual) order inside a replayed real book. It never enters the
// book: the real market didn't see it, and we assume it wouldn't have changed anyone's
// behaviour (small size, no market impact). What we track is where it WOULD stand in
// the queue, using the book's arrival counter (D4):
//   seq < ours  -> ahead of us,   seq >= ours  -> behind us.
// With order-by-order (L3) data that split is exact, not estimated.

namespace ob::sim {

struct VirtualOrder {
  Side side;
  Price price;
  Quantity remaining;
  Sequence seq;  // our place: orders with a smaller seq are ahead, the rest behind
};

// Join the back of the queue at `price` right now.
[[nodiscard]] VirtualOrder place(const OrderBook& book, Side side, Price price, Quantity qty);

// Displayed shares ahead of us at our price, right now.
[[nodiscard]] std::uint64_t shares_ahead(const OrderBook& book, const VirtualOrder& us);

// Call for each execution of a resting order, BEFORE the book applies it. Returns how
// many of our shares that execution would have filled first (and takes them off
// `us.remaining`). It fills us when the aggressor got past us:
//   - it traded an order on our side and price that is behind us, or
//   - it traded our side at a worse price (a seller below our bid, a buyer above our ask).
// Each execution fills at most its own size.
[[nodiscard]] Quantity fill_from_execution(VirtualOrder& us, const Order& executed, Quantity shares);

}  // namespace ob::sim
