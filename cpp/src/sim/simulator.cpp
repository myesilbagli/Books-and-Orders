#include "orderbook/sim/simulator.hpp"

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/sim/trades.hpp"

#include <algorithm>

namespace ob::sim {

namespace {

// A better price for that side: a higher bid, a lower ask.
bool better(Side side, Price a, Price b) { return side == Side::Buy ? a > b : a < b; }

// How many orders of `size` fit in `room` shares, at most `levels`: every resting order
// counts as if it all filled, so the position can't pass the limit even if the whole
// ladder is hit at once. With one level: 1 exactly when position + size stays in the limit.
std::size_t orders_that_fit(std::int64_t room, std::int64_t size, std::size_t levels) {
  if (size <= 0 || room < size) {
    return 0;
  }
  return std::min(levels, static_cast<std::size_t>(room / size));
}

}  // namespace

Simulator::Simulator(const SimConfig& config, Strategy& strategy)
    : config_(config),
      market_(config.market),
      // One QuoteSide per level; std::clamp keeps the count within [1, kMaxLevels].
      bids_(std::clamp<std::size_t>(config.levels, 1, kMaxLevels), QuoteSide{.side = Side::Buy}),
      asks_(std::clamp<std::size_t>(config.levels, 1, kMaxLevels), QuoteSide{.side = Side::Sell}),
      strategy_(&strategy) {}

void Simulator::on_message(const std::uint8_t* p, std::size_t len) {
  if (len < 11) {
    replay_.apply(p, len);  // too short to have a header; the replay counts it as bad
    return;
  }
  // Every message has the same header, so time and stock can be read without decoding.
  const std::uint64_t now = itch::read_u48(p + 5);
  const std::uint16_t locate = itch::read_u16(p + 1);
  const bool ours = locate == config_.locate;

  // The charts first close the bars that are over: nothing at `now` may get into them,
  // and a fill at `now` must see them (D45). This only reads the past, so it can go first.
  if (ours) {
    market_.advance(now);
  }

  // 1. LOOK. Anything we sent that has reached the exchange by now is live first: it
  // was there before this event happened.
  for (QuoteSide& q : bids_) {
    activate(q, now);
  }
  for (QuoteSide& q : asks_) {
    activate(q, now);
  }

  // An execution in our stock: look at the resting order BEFORE the book applies it,
  // because the book may remove it.
  if (ours && (p[0] == 'E' || p[0] == 'C')) {
    std::optional<OrderId> ref;
    Quantity shares = 0;
    if (p[0] == 'E') {
      if (const auto m = itch::decode_order_executed(p, len)) {
        ref = m->ref;
        shares = m->executed_shares;
      }
    } else if (const auto m = itch::decode_order_executed_with_price(p, len)) {
      ref = m->ref;
      shares = m->executed_shares;
    }
    if (ref) {
      if (const Order* resting = replay_.book(locate).find(*ref)) {
        fill_side(bids_, *resting, shares, now);
        fill_side(asks_, *resting, shares, now);
      }
    }
  }

  // Then count the trade in the charts: after the fill check, so a fill's snapshot is
  // what was known before its own trade, and before the book changes, because an E's
  // price is the resting order's and the book may remove it (D40, D44).
  if (ours) {
    const std::optional<Trade> trade = trade_of(p, len, replay_);
    if (trade) {
      market_.on_trade(*trade);
    }
  }

  // 2. APPLY.
  replay_.apply(p, len);

  // 3. DECIDE. Only our stock's messages can change what we want to quote.
  if (ours) {
    requote(now);
  }
}

void Simulator::activate(QuoteSide& q, std::uint64_t now_ns) {
  if (!q.pending || q.pending_at_ns > now_ns) {
    return;  // nothing sent, or still on its way
  }
  // A new price replaces the old quote and joins the back of that queue as the book is
  // now; an empty pending_price is a pull.
  if (q.pending_price) {
    q.live = place(replay_.book(config_.locate), q.side, *q.pending_price, config_.quote_size);
  } else {
    q.live.reset();
  }
  q.pending = false;
}

void Simulator::fill_side(std::vector<QuoteSide>& side, const Order& executed,
                          Quantity shares, std::uint64_t now_ns) {
  // An aggressor reaches our better prices first, so they fill first, and each executed
  // share fills at most one of ours. A handful of levels: pick the best one left each time.
  std::array<bool, kMaxLevels> done{};
  Quantity left = shares;
  for (std::size_t round = 0; round < side.size() && left > 0; ++round) {
    std::size_t pick = side.size();  // "none yet"
    for (std::size_t i = 0; i < side.size(); ++i) {
      if (done[i] || !side[i].live) {
        continue;
      }
      if (pick == side.size() || better(side[i].side, side[i].live->price, side[pick].live->price)) {
        pick = i;
      }
    }
    if (pick == side.size()) {
      return;  // no live order left on this side
    }
    done[pick] = true;
    left -= check_fill(side[pick], executed, shares, left, now_ns);
  }
}

Quantity Simulator::check_fill(QuoteSide& q, const Order& executed, Quantity shares,
                               Quantity cap, std::uint64_t now_ns) {
  if (!q.live) {
    return 0;
  }
  // `cap`: what this execution has left after filling our better-priced orders.
  const Quantity filled = fill_from_execution(*q.live, executed, cap);
  if (filled == 0) {
    return 0;
  }
  const Price price = q.live->price;

  // What the book looked like on our side as we were filled. This runs before the replay
  // applies the execution, so the executed shares are still in the book: take them off
  // to get what's left at our price (when it traded at our price; a trade-through
  // happened below us). A thin touch, or nothing left behind us, means the level is
  // about to break and the price to move against us (D33).
  const OrderBook& book = replay_.book(config_.locate);
  const Side other = q.side == Side::Buy ? Side::Sell : Side::Buy;
  const auto our_best = q.side == Side::Buy ? book.best_bid() : book.best_ask();
  const auto other_best = q.side == Side::Buy ? book.best_ask() : book.best_bid();
  const std::uint64_t ours_at_touch = our_best ? book.volume_at(q.side, *our_best) : 0;
  const std::uint64_t other_at_touch = other_best ? book.volume_at(other, *other_best) : 0;
  const std::uint64_t at_touch = ours_at_touch + other_at_touch;
  const std::uint64_t at_our_price = book.volume_at(q.side, price);
  const std::uint64_t leaving = executed.price == price ? shares : 0;

  fills_.push_back(Fill{
      .timestamp_ns = now_ns,
      .side = q.side,
      .price = price,
      .quantity = filled,
      .touch_share = at_touch ? static_cast<double>(ours_at_touch) / static_cast<double>(at_touch)
                              : 0.0,
      .shares_left = at_our_price > leaving ? at_our_price - leaving : 0,  // unsigned: no < 0
      // The last two-sided mid, from before this message was applied: the price the
      // spread is measured from. Always set while we quote; the fallback is "no spread".
      .mid_x2_before = last_mid_x2_.value_or(2 * price),
      // Copies, not references: the charts keep moving after the fill (D45).
      .chart_1s = market_.one_second().latest(),
      .chart_15s = market_.fifteen_seconds().latest(),
      .chart_1m = market_.one_minute().latest(),
      .chart_5m = market_.five_minutes().latest(),
      .vwap = market_.vwap().value(),
      .b1 = market_.b1(now_ns),
      .b2 = market_.b2(now_ns),
  });

  // Buying adds shares and spends money; selling the reverse. Quantity is unsigned, so
  // convert before negating.
  const auto qty = static_cast<std::int64_t>(filled);
  if (q.side == Side::Buy) {
    inventory_ += qty;
    cash_ -= price * qty;
  } else {
    inventory_ -= qty;
    cash_ += price * qty;
  }
  if (q.live->remaining == 0) {
    q.live.reset();  // fully filled: the strategy requotes on its next decision
  }
  return filled;
}

void Simulator::requote(std::uint64_t now_ns) {
  const OrderBook& book = replay_.book(config_.locate);
  const auto best_bid = book.best_bid();
  const auto best_ask = book.best_ask();

  std::optional<Price> bid_target;  // the ladder's top: the strategy's price
  std::optional<Price> ask_target;
  std::size_t bid_count = 0;  // how many orders the ladder may hold on that side
  std::size_t ask_count = 0;

  // Only a normal two-sided market has a mid, and only then is the strategy asked.
  if (best_bid && best_ask && *best_bid < *best_ask) {
    const Price mid_x2 = *best_bid + *best_ask;
    if (mid_x2 != last_mid_x2_) {  // only changes: a quiet mid adds nothing to the series
      mids_.push_back(MidPoint{.timestamp_ns = now_ns, .mid_x2 = mid_x2});
      last_mid_x2_ = mid_x2;
      market_.on_mid(now_ns, mid_x2);  // the charts move with the same mid series
    }

    // Asked even outside our hours, so a strategy can warm up; the answer is used below
    // only inside them.
    const MarketView view{.now_ns = now_ns,
                          .best_bid = *best_bid,
                          .best_ask = *best_ask,
                          .bid_size = book.volume_at(Side::Buy, *best_bid),
                          .ask_size = book.volume_at(Side::Sell, *best_ask),
                          .inventory = inventory_,
                          .market = &market_};
    const Quotes wanted = strategy_->quote(view);

    const bool open = now_ns >= config_.start_ns && now_ns < config_.end_ns;
    // Don't hold orders whose fills could take the position past the limit.
    const auto size = static_cast<std::int64_t>(config_.quote_size);
    const std::size_t may_buy = orders_that_fit(config_.max_inventory - inventory_, size, bids_.size());
    const std::size_t may_sell = orders_that_fit(config_.max_inventory + inventory_, size, asks_.size());

    // Never better than the touch: a price inside the spread is one nobody in the real
    // data saw or reacted to, so the replay couldn't tell what would have happened (D30).
    // This also keeps us passive: a bid can never reach the ask and trade.
    if (open && may_buy > 0 && wanted.bid && *wanted.bid > 0) {
      bid_target = std::min(*wanted.bid, *best_bid);
      bid_count = may_buy;
    }
    if (open && may_sell > 0 && wanted.ask && *wanted.ask > 0) {
      ask_target = std::max(*wanted.ask, *best_ask);
      ask_count = may_sell;
    }
  }

  ladder(bids_, bid_target, bid_count, -config_.tick, now_ns);  // bids step down
  ladder(asks_, ask_target, ask_count, config_.tick, now_ns);   // asks step up
}

void Simulator::ladder(std::vector<QuoteSide>& side, std::optional<Price> top, std::size_t count,
                       Price step, std::uint64_t now_ns) {
  // The prices wanted: top, top + step, ... `count` of them, the touch first.
  // An order already at (or on its way to) one of them is left alone: moving it would
  // throw away its place in that queue, which is the point of the ladder (D60).
  std::array<bool, kMaxLevels> kept{};     // per order
  std::array<bool, kMaxLevels> covered{};  // per wanted price
  for (std::size_t i = 0; top && i < side.size(); ++i) {
    const std::optional<Price> at = heading_to(side[i]);
    for (std::size_t k = 0; at && k < count; ++k) {
      if (!covered[k] && *at == *top + step * static_cast<Price>(k)) {
        kept[i] = true;
        covered[k] = true;
        break;
      }
    }
  }
  // Every other order moves to the wanted price nearest the touch that no order covers,
  // or is pulled when none is left, so the deepest price is the first to go when the
  // limit binds. Orders that are live or on their way are moved first: moving one is a
  // single message, where pulling it and sending an idle one would be two.
  std::size_t k = 0;
  // Two passes over the orders: `{true, false}` is a two-value list to loop over.
  for (const bool busy : {true, false}) {
    for (std::size_t i = 0; i < side.size(); ++i) {
      if (kept[i] || heading_to(side[i]).has_value() != busy) {
        continue;
      }
      while (k < count && covered[k]) {
        ++k;
      }
      std::optional<Price> target;
      if (top && k < count) {
        const Price price = *top + step * static_cast<Price>(k);
        if (price > 0) {
          target = price;
        }
        covered[k] = true;
      }
      decide(side[i], target, now_ns);
    }
  }
}

void Simulator::decide(QuoteSide& q, std::optional<Price> target, std::uint64_t now_ns) {
  // If that's where we're already heading, do nothing: resending would throw away our
  // place in the queue.
  if (heading_to(q) == target) {
    return;
  }
  ++quote_messages_;
  q.pending = true;
  q.pending_price = target;
  q.pending_at_ns = now_ns + config_.latency_ns;
  activate(q, now_ns);  // with zero latency it takes effect right away
}

std::optional<Price> Simulator::heading_to(const QuoteSide& q) {
  // Where this order will be once everything already sent has arrived.
  return q.pending ? q.pending_price : (q.live ? std::optional<Price>(q.live->price) : std::nullopt);
}

const std::optional<VirtualOrder>& Simulator::best(const std::vector<QuoteSide>& side) {
  const QuoteSide* top = &side.front();  // if nothing is live, its empty slot says so
  for (const QuoteSide& q : side) {
    if (q.live && (!top->live || better(q.side, q.live->price, top->live->price))) {
      top = &q;
    }
  }
  return top->live;
}

std::vector<VirtualOrder> Simulator::live(const std::vector<QuoteSide>& side) {
  std::vector<VirtualOrder> out;
  for (const QuoteSide& q : side) {
    if (!q.live) {
      continue;
    }
    // Insert in price order, best first: a few orders, so a linear walk is enough.
    std::size_t at = 0;
    while (at < out.size() && !better(q.side, q.live->price, out[at].price)) {
      ++at;
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), *q.live);
  }
  return out;
}

double Simulator::pnl_dollars() const {
  // cash + inventory x mid, with mid = (bid + ask) / 2 kept as 2 x mid to stay exact.
  // Value inventory at the mid in force when quoting ended (or now, if it hasn't ended
  // yet). The last mid of the day is no good: after hours the book thins out and the mid
  // of a few scattered orders can be far from any real price.
  // No mid at all means we never quoted, so inventory is 0 and the 0 default doesn't matter.
  const std::int64_t mid_x2 = mid_x2_at(mids_, config_.end_ns).value_or(0);
  const double ticks = static_cast<double>(2 * cash_ + inventory_ * mid_x2) / 2.0;
  return ticks / 10'000.0;  // ticks are $0.0001
}

}  // namespace ob::sim
