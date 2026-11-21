#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

// One execution: the incoming (aggressor) order traded against one resting order.
struct Trade {
  OrderId aggressor_id;
  OrderId resting_id;
  Price price;  // always the resting order's price: it was there first and set it
  Quantity quantity;
};

struct SubmitResult {
  bool accepted;              // false: zero quantity or duplicate id, nothing happened
  std::vector<Trade> trades;  // in execution order; empty if the order didn't cross
};

// A Level 3 (market-by-order) book: every resting order is kept individually,
// in arrival order at its price.
class OrderBook {
 public:
  // Rest an order WITHOUT matching. For replaying ITCH, where Nasdaq already did the
  // matching and only tells us what now rests. false if the id is already in the book
  // or the quantity is 0; the book is unchanged.
  [[nodiscard]] bool add(const Order& order);

  // Match first with price-time priority (best price, then oldest at that price),
  // then rest whatever is left at the order's limit price.
  [[nodiscard]] SubmitResult submit(const Order& order);

  // false if the id is not in the book (never added, or already cancelled).
  [[nodiscard]] bool cancel(OrderId id);

  // Empty (std::nullopt) when that side has no orders, never a fake price like 0.
  [[nodiscard]] std::optional<Price> best_bid() const;
  [[nodiscard]] std::optional<Price> best_ask() const;

  // Ids at one price, oldest first; empty if there is no level there. This is the
  // queue a new order would join the back of, which later gives us queue position.
  [[nodiscard]] std::vector<OrderId> queue_at(Side side, Price price) const;

  // Total shares resting at one price. 64-bit: many 32-bit orders can sum past 4.29 billion.
  [[nodiscard]] std::uint64_t volume_at(Side side, Price price) const;

  [[nodiscard]] std::size_t order_count() const { return index_.size(); }
  [[nodiscard]] std::size_t level_count(Side side) const {
    return side == Side::Buy ? bids_.size() : asks_.size();
  }

 private:
  // One price level: a FIFO queue. std::list so removing from the middle is O(1)
  // and iterators to the other orders stay valid.
  using Level = std::list<Order>;

  // Bids sorted high to low, asks low to high, so the best price is always begin().
  std::map<Price, Level, std::greater<Price>> bids_;
  std::map<Price, Level> asks_;

  // Where an order sits: enough to find its level and remove it without searching.
  struct Locator {
    Side side;
    Price price;
    Level::iterator position;  // the "coat-check ticket" into its level's list
  };
  std::unordered_map<OrderId, Locator> index_;

  // Arrival counter stamped on each accepted order (decision D4: a counter, not a clock).
  Sequence next_seq_ = 1;

  // Read-only lookup of one level; nullptr if no orders rest at that price.
  [[nodiscard]] const Level* find_level(Side side, Price price) const;

  // Trade `incoming` against one level, front of the queue first, until either runs out.
  // Takes a Level (same type on both sides), so one function serves buys and sells.
  void fill_against(Level& level, Price level_price, Order& incoming, std::vector<Trade>& trades);
};

}  // namespace ob
