#pragma once

#include "orderbook/sim/strategy.hpp"

// A strategy that wraps another one (the "decorator" pattern): it asks the inner
// strategy where to quote, then takes away the side whose queue at the touch is thin.
//
// Why: a fill at the back of a thin queue usually means the whole level is being eaten,
// and the price moves against us right after (D33). Thin = our side's share of the
// shares at the touch, bid_size / (bid_size + ask_size) for the bid, below min_share.
//
// Because it only filters, it fits on any strategy: naive, Avellaneda-Stoikov, or later
// ones, without changing them.

namespace ob::sim {

class PullWhenThin final : public Strategy {
 public:
  // `inner` is used by reference and must outlive this object. min_share in [0, 1);
  // 0 never pulls.
  PullWhenThin(Strategy& inner, double min_share) : inner_(inner), min_share_(min_share) {}

  Quotes quote(const MarketView& view) override {
    // Ask the inner strategy first, always: it may keep state (a volatility window)
    // that needs every message, pulled or not.
    Quotes q = inner_.quote(view);
    const std::uint64_t total = view.bid_size + view.ask_size;
    if (total == 0) {
      return q;  // no sizes to judge by (can't happen on a usable book); leave it alone
    }
    // Each share divided out on its own: 1.0 - bid_share would put 0.8 -> 0.19999...,
    // and pull an ask sitting exactly at the threshold.
    const double bid_share = static_cast<double>(view.bid_size) / static_cast<double>(total);
    const double ask_share = static_cast<double>(view.ask_size) / static_cast<double>(total);
    if (bid_share < min_share_) {
      q.bid.reset();
    }
    if (ask_share < min_share_) {
      q.ask.reset();
    }
    return q;
  }

 private:
  Strategy& inner_;
  double min_share_;
};

}  // namespace ob::sim
