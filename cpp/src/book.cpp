#include "orderbook/book.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace ob {

bool OrderBook::add(const Order& order) {
  // Check everything before touching the book, so a rejected add leaves no trace.
  if (order.remaining == 0) {
    return false;
  }
  if (index_.contains(order.id)) {
    return false;
  }

  // operator[] finds the level at this price, or creates an empty one if none exists.
  // Both maps hold the same Level type, so one reference works for either side.
  Level& level = order.side == Side::Buy ? bids_[order.price] : asks_[order.price];

  // Back of the queue = time priority. The book stamps its own seq, so arrival
  // order never depends on what the caller put in the field.
  level.push_back(order);
  level.back().seq = next_seq_++;

  // std::prev(level.end()) is an iterator to the element we just pushed: our ticket.
  index_.emplace(order.id, Locator{order.side, order.price, std::prev(level.end())});
  return true;
}

bool OrderBook::cancel(OrderId id) {
  const auto found = index_.find(id);
  if (found == index_.end()) {
    return false;
  }
  const Locator& where = found->second;

  // Two branches because bids_ and asks_ are different map types (different sort order).
  // The price lookup is O(log levels); the removal inside the level is O(1) via the ticket.
  if (where.side == Side::Buy) {
    const auto level = bids_.find(where.price);
    level->second.erase(where.position);
    if (level->second.empty()) {
      bids_.erase(level);  // an empty level would make best_bid() lie
    }
  } else {
    const auto level = asks_.find(where.price);
    level->second.erase(where.position);
    if (level->second.empty()) {
      asks_.erase(level);
    }
  }

  // Erase the index entry last: `where` refers into it and is used above.
  index_.erase(found);
  return true;
}

bool OrderBook::reduce(OrderId id, Quantity qty) {
  if (qty == 0) {
    return false;
  }
  const auto found = index_.find(id);
  if (found == index_.end()) {
    return false;
  }

  // Change the order where it stands, through its ticket: no unlinking, so it keeps
  // its place in the queue. fill() refuses an overfill and leaves the order unchanged.
  Order& order = *found->second.position;
  if (!order.fill(qty)) {
    return false;
  }
  if (order.is_filled()) {
    [[maybe_unused]] const bool removed = cancel(id);  // reuses the empty-level cleanup
    assert(removed);                                   // can't fail: id was just found
  }
  return true;
}

bool OrderBook::replace(OrderId old_id, OrderId new_id, Quantity qty, Price price) {
  // All checks first, so a rejected replace leaves the old order untouched.
  const auto found = index_.find(old_id);
  if (found == index_.end() || qty == 0 || index_.contains(new_id)) {
    return false;
  }
  const Side side = found->second.side;  // ITCH U doesn't repeat the side

  // cancel + add: the new order goes to the back of its level, so it loses time
  // priority even at the same price. That's Nasdaq's rule, not a shortcut.
  [[maybe_unused]] const bool cancelled = cancel(old_id);
  [[maybe_unused]] const bool added =
      add(Order{.id = new_id, .side = side, .price = price, .remaining = qty, .seq = 0});
  assert(cancelled && added);  // can't fail: everything was checked above
  return true;
}

SubmitResult OrderBook::submit(const Order& order) {
  SubmitResult result{.accepted = false, .trades = {}};

  // Same checks as add(), done BEFORE matching: a rejected order must not trade at all.
  if (order.remaining == 0 || index_.contains(order.id)) {
    return result;
  }
  result.accepted = true;

  Order incoming = order;  // our own copy; its `remaining` shrinks as it fills

  // Walk the opposite side from its best price. Stop when the order is filled, the
  // side is empty, or the best opposite price is worse than our limit (no cross).
  if (incoming.side == Side::Buy) {
    while (incoming.remaining > 0 && !asks_.empty() && asks_.begin()->first <= incoming.price) {
      const auto best = asks_.begin();
      fill_against(best->second, best->first, incoming, result.trades);
      if (best->second.empty()) {
        asks_.erase(best);
      }
    }
  } else {
    while (incoming.remaining > 0 && !bids_.empty() && bids_.begin()->first >= incoming.price) {
      const auto best = bids_.begin();
      fill_against(best->second, best->first, incoming, result.trades);
      if (best->second.empty()) {
        bids_.erase(best);
      }
    }
  }

  // Whatever didn't fill becomes a resting order at its limit price. It can't cross
  // any more: the loop above stopped exactly because the opposite best is worse.
  if (incoming.remaining > 0) {
    [[maybe_unused]] const bool rested = add(incoming);
    assert(rested);  // can't fail: id and quantity were checked above
  }
  return result;
}

void OrderBook::fill_against(Level& level, Price level_price, Order& incoming,
                             std::vector<Trade>& trades) {
  while (incoming.remaining > 0 && !level.empty()) {
    Order& resting = level.front();  // oldest at this price: time priority

    // Trade the smaller of the two, so neither fill can overfill.
    const Quantity qty = std::min(incoming.remaining, resting.remaining);
    [[maybe_unused]] const bool ok = resting.fill(qty) && incoming.fill(qty);
    assert(ok);

    trades.push_back(Trade{.aggressor_id = incoming.id,
                           .resting_id = resting.id,
                           .price = level_price,
                           .quantity = qty});

    if (resting.is_filled()) {
      index_.erase(resting.id);  // drop its ticket first; pop_front destroys `resting`
      level.pop_front();
    }
  }
}

std::optional<Price> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.begin()->first;  // highest buy, because bids_ sorts with std::greater
}

std::optional<Price> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.begin()->first;  // lowest sell, default ascending order
}

const OrderBook::Level* OrderBook::find_level(Side side, Price price) const {
  if (side == Side::Buy) {
    const auto it = bids_.find(price);
    return it == bids_.end() ? nullptr : &it->second;
  }
  const auto it = asks_.find(price);
  return it == asks_.end() ? nullptr : &it->second;
}

std::vector<OrderId> OrderBook::queue_at(Side side, Price price) const {
  std::vector<OrderId> ids;
  if (const Level* level = find_level(side, price)) {
    ids.reserve(level->size());
    for (const Order& o : *level) {
      ids.push_back(o.id);
    }
  }
  return ids;
}

std::uint64_t OrderBook::volume_at(Side side, Price price) const {
  std::uint64_t total = 0;
  if (const Level* level = find_level(side, price)) {
    for (const Order& o : *level) {
      total += o.remaining;  // widened to 64-bit before adding, so the sum can't wrap
    }
  }
  return total;
}

}  // namespace ob
