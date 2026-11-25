#pragma once

#include "orderbook/book.hpp"
#include "orderbook/order.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

// A taker on the replay (round 7f, D65): when a read fires, cross the spread with a market
// order, hold for a fixed time, cross back. Fills are what the real book offered when the
// order arrived: the best price first, then the next ticks, as a marketable order would
// take them. One position at a time.

namespace ob::sim {

// The average price (ticks) a marketable order for `qty` shares pays, taking the opposite
// side of the book from its best price, one tick at a time, at most max_ticks deep. nullopt
// when that side is empty or too thin. Read-only: our order never changes the replay.
std::optional<double> sweep_price(const OrderBook& book, Side aggressor, Quantity qty,
                                  Price tick, int max_ticks = 50);

struct TakerConfig {
  std::uint64_t from_ns = (9ULL * 3600 + 30 * 60) * 1'000'000'000;  // reads count from
  std::uint64_t to_ns = (10ULL * 3600 + 30 * 60) * 1'000'000'000;   // until (not included)
  std::uint64_t latency_ns = 100'000'000;  // read known -> order at the exchange: 100 ms
  std::uint64_t hold_ns = 120'000'000'000;  // from the entry to sending the exit: 2 min
  Quantity size = 100;
  Price tick = 100;  // $0.01 in ticks
};

struct TakerTrade {
  int direction;            // +1 bought first (long), -1 sold first (short)
  std::uint64_t signal_ns;  // when the read was known: its bar's end
  std::uint64_t entry_ns;   // when the entry order reached the book
  double entry_price;       // ticks, average over the shares
  std::uint64_t exit_ns;
  double exit_price;
  Quantity quantity;
  // Ticks a share, before fees: what the position made.
  [[nodiscard]] double pnl_ticks() const { return direction * (exit_price - entry_price); }
};

class TakerBot {
 public:
  explicit TakerBot(TakerConfig config = {});

  // A read was completed at t_ns (+1 / -1; 0 is ignored). Acted on only when flat, inside
  // the window: the entry order reaches the book latency_ns later.
  void on_read(std::uint64_t t_ns, int direction);

  // Call before the book applies a message at now_ns: the book is then exactly as it was
  // for any order that arrived since the last message. Fills what is due.
  void before_message(std::uint64_t now_ns, const OrderBook& book);

  // End of the data: an open position is closed against the book as it is; an entry not
  // yet filled is dropped.
  void finish(std::uint64_t end_ns, const OrderBook& book);

  [[nodiscard]] const std::vector<TakerTrade>& trades() const { return trades_; }

 private:
  enum class State : std::uint8_t { Flat, Entering, Holding };

  TakerConfig config_;
  State state_ = State::Flat;
  std::uint64_t due_ns_ = 0;  // when the next order (entry or exit) reaches the book
  TakerTrade open_{};         // the position being entered or held
  std::vector<TakerTrade> trades_;

  bool close(std::uint64_t now_ns, const OrderBook& book);
};

// The trades as CSV for the research side: signal_ns, direction (+1 / -1), entry_ns,
// entry_price, exit_ns, exit_price (ticks, averages to 2 places), quantity. Fees are left
// to the analysis.
void write_taker_csv(std::ostream& out, const std::vector<TakerTrade>& trades);

}  // namespace ob::sim
