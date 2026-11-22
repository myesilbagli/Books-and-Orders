#include "orderbook/sim/simulator.hpp"

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/sim/trades.hpp"

#include <algorithm>

namespace ob::sim {

void Simulator::on_message(const std::uint8_t* p, std::size_t len) {
  if (len < 11) {
    replay_.apply(p, len);  // too short to have a header; the replay counts it as bad
    return;
  }
  // Every message has the same header, so time and stock can be read without decoding.
  const std::uint64_t now = itch::read_u48(p + 5);
  const std::uint16_t locate = itch::read_u16(p + 1);
  const bool ours = locate == config_.locate;

  // 1. LOOK. Anything we sent that has reached the exchange by now is live first: it
  // was there before this event happened.
  activate(bid_, now);
  activate(ask_, now);

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
        check_fill(bid_, *resting, shares, now);
        check_fill(ask_, *resting, shares, now);
      }
    }
  }

  // The charts, before the book changes: first close the bars that are over (nothing at
  // `now` may get into them), then count the trade, whose price for an E is the resting
  // order's and so must be read before the book removes it (D40, D44).
  if (ours) {
    market_.advance(now);
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

void Simulator::check_fill(QuoteSide& q, const Order& executed, Quantity shares,
                           std::uint64_t now_ns) {
  if (!q.live) {
    return;
  }
  const Quantity filled = fill_from_execution(*q.live, executed, shares);
  if (filled == 0) {
    return;
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
}

void Simulator::requote(std::uint64_t now_ns) {
  const OrderBook& book = replay_.book(config_.locate);
  const auto best_bid = book.best_bid();
  const auto best_ask = book.best_ask();

  std::optional<Price> bid_target;
  std::optional<Price> ask_target;

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
    // Don't send a quote whose fill could take the position past the limit.
    const auto size = static_cast<std::int64_t>(config_.quote_size);
    const bool may_buy = inventory_ + size <= config_.max_inventory;
    const bool may_sell = inventory_ - size >= -config_.max_inventory;

    // Never better than the touch: a price inside the spread is one nobody in the real
    // data saw or reacted to, so the replay couldn't tell what would have happened (D30).
    // This also keeps us passive: a bid can never reach the ask and trade.
    if (open && may_buy && wanted.bid && *wanted.bid > 0) {
      bid_target = std::min(*wanted.bid, *best_bid);
    }
    if (open && may_sell && wanted.ask && *wanted.ask > 0) {
      ask_target = std::max(*wanted.ask, *best_ask);
    }
  }

  decide(bid_, bid_target, now_ns);
  decide(ask_, ask_target, now_ns);
}

void Simulator::decide(QuoteSide& q, std::optional<Price> target, std::uint64_t now_ns) {
  // Where we'll be once everything already sent has arrived. If that's the target,
  // do nothing: resending would throw away our place in the queue.
  const std::optional<Price> heading_to =
      q.pending ? q.pending_price : (q.live ? std::optional<Price>(q.live->price) : std::nullopt);
  if (heading_to == target) {
    return;
  }
  ++quote_messages_;
  q.pending = true;
  q.pending_price = target;
  q.pending_at_ns = now_ns + config_.latency_ns;
  activate(q, now_ns);  // with zero latency it takes effect right away
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
