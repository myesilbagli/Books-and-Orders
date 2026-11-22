#pragma once

#include "orderbook/order.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Time bars (OHLCV) for the indicators: EMA, SMA, Bollinger, RSI work on closed bars (D38).
//
// Prices are the mid, not trade prices. At one second a trade-price bar is often empty
// (no trade that second) and jumps between bid and ask without the price moving; the
// mid is always defined and has no bounce. Volume still comes from the trades.
//
// Bars are aligned to whole intervals since midnight: with 1-minute bars, one bar is
// [10:00:00, 10:01:00). A bar is handed out only once an event at or after its end
// arrives, so nothing can use a bar before it has closed (no look-ahead).

namespace ob::sim {

struct Bar {
  std::uint64_t start_ns = 0;  // the bar covers [start_ns, end_ns)
  std::uint64_t end_ns = 0;
  Price open_x2 = 0;  // twice the mid, as in MidPoint: the mid in force at start_ns
  Price high_x2 = 0;
  Price low_x2 = 0;
  Price close_x2 = 0;  // the mid in force just before end_ns
  std::uint64_t volume = 0;  // shares traded in the bar
};

class BarBuilder {
 public:
  // interval_ns must be > 0 (it's clamped to 1).
  explicit BarBuilder(std::uint64_t interval_ns);

  // Feed events in time order. Each call first closes every bar that ended at or before
  // t_ns and returns them, oldest first (usually none, several after a quiet stretch).
  // A change exactly at a bar's end belongs to the next bar.

  // The mid changed. No bar exists before the first mid.
  std::vector<Bar> on_mid(std::uint64_t t_ns, Price mid_x2);

  // Shares traded. Ignored before the first mid (there's no bar to put them in yet).
  std::vector<Bar> on_trade(std::uint64_t t_ns, std::uint64_t shares);

  // Time passed with no event: close the bars that are over.
  std::vector<Bar> advance(std::uint64_t t_ns);

 private:
  std::uint64_t interval_ns_;
  std::optional<Bar> current_;  // the bar being built; empty until the first mid
};

}  // namespace ob::sim
