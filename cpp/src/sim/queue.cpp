#include "orderbook/sim/queue.hpp"

#include <algorithm>

namespace ob::sim {

VirtualOrder place(const OrderBook& book, Side side, Price price, Quantity qty) {
  // The book's next seq, peeked at, not used up: every order resting now has a smaller
  // seq (ahead of us); the next order to arrive gets this SAME seq, and later ones bigger
  // (behind us). So the boundary is: seq < ours ahead, seq >= ours behind.
  return VirtualOrder{.side = side, .price = price, .remaining = qty, .seq = book.next_sequence()};
}

std::uint64_t shares_ahead(const OrderBook& book, const VirtualOrder& us) {
  return book.volume_ahead(us.side, us.price, us.seq);
}

Quantity fill_from_execution(VirtualOrder& us, const Order& executed, Quantity shares) {
  if (us.remaining == 0 || executed.side != us.side) {
    return 0;
  }
  // "Worse" from the aggressor's side: a seller walks bids from high to low, a buyer
  // walks asks from low to high. Trading at a worse price means they passed ours.
  const bool through =
      us.side == Side::Buy ? executed.price < us.price : executed.price > us.price;
  // >=, not >: the first order after us got our peeked seq (see place()).
  const bool behind_us_at_our_price = executed.price == us.price && executed.seq >= us.seq;
  if (!through && !behind_us_at_our_price) {
    return 0;  // ahead of us, or at a better price: the aggressor stopped before us
  }
  const Quantity filled = std::min(us.remaining, shares);
  us.remaining -= filled;  // safe: filled <= remaining
  return filled;
}

}  // namespace ob::sim
