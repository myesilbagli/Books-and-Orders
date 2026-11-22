#pragma once

#include "orderbook/replay.hpp"
#include "orderbook/sim/markout.hpp"
#include "orderbook/sim/queue.hpp"
#include "orderbook/sim/strategy.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// A market-making simulation for one stock on top of an ITCH replay.
//
// For every message, in this order:
//   1. LOOK:   our quotes and pulls that have reached the exchange by now go live; if the
//              message is an execution in our stock, check (before the book changes)
//              whether it would have filled one of our virtual quotes (D24).
//   2. APPLY:  hand the message to the replay, so the real books move.
//   3. DECIDE: if it was our stock, the strategy picks where to quote. Changes reach the
//              exchange `latency_ns` later; until then the old quote stays live and can
//              still be filled.
//
// Where to quote is the Strategy's call (strategy.hpp); without one, NaiveStrategy joins
// the best bid and ask. The simulator keeps the rules no strategy may break: quoting
// hours, a two-sided market, the inventory limit, and never improving on the best price.

namespace ob::sim {

struct SimConfig {
  std::uint16_t locate;           // the stock we make a market in
  Quantity quote_size;            // shares per quote
  std::uint64_t latency_ns;       // decision -> exchange, for new quotes and pulls alike
  std::int64_t max_inventory;     // shares; no quote that could push |position| past it
  std::uint64_t start_ns;         // quote only in [start_ns, end_ns), ns since midnight
  std::uint64_t end_ns;
};

class Simulator {
 public:
  // With the naive strategy (the milestone 4 baseline).
  explicit Simulator(const SimConfig& config) : config_(config), strategy_(&naive_) {}
  // With any strategy. It's used by reference, not copied: it must outlive the simulator.
  Simulator(const SimConfig& config, Strategy& strategy)
      : config_(config), strategy_(&strategy) {}

  // A copy's strategy_ could point at the original's naive_, which may be gone.
  Simulator(const Simulator&) = delete;
  Simulator& operator=(const Simulator&) = delete;

  // One message, as handed out by ItchReader. Every message of every stock goes through
  // here, so all books stay correct and time keeps moving.
  void on_message(const std::uint8_t* p, std::size_t len);

  [[nodiscard]] const Replayer& replay() const { return replay_; }
  [[nodiscard]] const std::vector<Fill>& fills() const { return fills_; }
  // Our stock's mid every time it changed, in time order: what markouts are measured on.
  [[nodiscard]] const std::vector<MidPoint>& mids() const { return mids_; }
  [[nodiscard]] const std::optional<VirtualOrder>& bid() const { return bid_.live; }
  [[nodiscard]] const std::optional<VirtualOrder>& ask() const { return ask_.live; }

  [[nodiscard]] std::int64_t inventory() const { return inventory_; }  // shares, + is long
  // Every new quote, price change or pull we sent: what an exchange counts against us
  // (message rate, order-to-trade ratio), and each one gives up our place in a queue.
  [[nodiscard]] std::uint64_t quote_messages() const { return quote_messages_; }
  // Money in ticks x shares ($0.0001 per share): sells add, buys subtract.
  [[nodiscard]] std::int64_t cash_ticks() const { return cash_; }
  // Mark-to-market: cash plus inventory valued at the mid when quoting ended, in dollars.
  [[nodiscard]] double pnl_dollars() const;

 private:
  // One side of our quoting: what's live at the exchange, and what we've sent that
  // hasn't arrived yet (a new price, or a pull when pending_price is empty).
  struct QuoteSide {
    Side side;
    std::optional<VirtualOrder> live;
    bool pending = false;
    std::optional<Price> pending_price;
    std::uint64_t pending_at_ns = 0;
  };

  SimConfig config_;
  Replayer replay_;
  QuoteSide bid_{.side = Side::Buy};
  QuoteSide ask_{.side = Side::Sell};
  std::vector<Fill> fills_;
  std::vector<MidPoint> mids_;
  std::int64_t inventory_ = 0;
  std::int64_t cash_ = 0;
  std::uint64_t quote_messages_ = 0;
  std::optional<Price> last_mid_x2_;  // bid + ask: twice the mid, so it stays an integer
  NaiveStrategy naive_;               // used when no strategy is given
  Strategy* strategy_;                // a pointer, not a reference, so it can point at naive_

  void activate(QuoteSide& q, std::uint64_t now_ns);
  void check_fill(QuoteSide& q, const Order& executed, Quantity shares, std::uint64_t now_ns);
  void requote(std::uint64_t now_ns);
  void decide(QuoteSide& q, std::optional<Price> target, std::uint64_t now_ns);
};

}  // namespace ob::sim
