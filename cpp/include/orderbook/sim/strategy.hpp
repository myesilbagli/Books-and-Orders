#pragma once

#include "orderbook/order.hpp"

#include <cstdint>
#include <optional>

// The quoting decision, separated from the simulator's mechanics (latency, queues,
// fills, accounting). The simulator is the car, the strategy is the driver: swapping
// drivers on the same car is how strategies are compared fairly.

namespace ob::sim {

// What the strategy is shown, on every message of our stock while the book is two-sided
// and not crossed (so best_bid < best_ask always holds here).
struct MarketView {
  std::uint64_t now_ns;
  Price best_bid;
  Price best_ask;
  std::uint64_t bid_size;  // shares resting at best_bid, other traders only (not us)
  std::uint64_t ask_size;
  std::int64_t inventory;  // our position in shares, + is long
};

// Where to quote; an empty side means "no quote there".
struct Quotes {
  std::optional<Price> bid;
  std::optional<Price> ask;
};

// The contract every strategy follows. `virtual` lets the simulator call whichever
// strategy it was given, picked at run time, without knowing which one it is.
// (A template would pick it at compile time and save the indirect call; kept for the
// optimization milestone, D30.)
class Strategy {
 public:
  // Virtual so that deleting a strategy through a Strategy* also runs the subclass's
  // destructor. Any class meant to be inherited from needs this.
  virtual ~Strategy() = default;

  // Called on every two-sided message of our stock, also outside quoting hours, so a
  // strategy can warm up (e.g. its volatility estimate). Outside hours, and wherever it
  // would break the simulator's rules, the simulator overrides the answer (D30).
  // Not const: a strategy may keep state between calls.
  virtual Quotes quote(const MarketView& view) = 0;  // "= 0": no default, must be written
};

// The milestone 4 baseline: join the best bid and the best ask.
class NaiveStrategy final : public Strategy {  // final: nothing inherits from this one
 public:
  Quotes quote(const MarketView& view) override {  // override: must match the contract
    return {.bid = view.best_bid, .ask = view.best_ask};
  }
};

}  // namespace ob::sim
