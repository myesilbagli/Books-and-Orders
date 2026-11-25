#pragma once

#include "orderbook/sim/bars.hpp"
#include "orderbook/sim/levels.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Price-action reads that see a turn as it happens (round 7d-2, D63). Each one watches the
// short chart's bars (15 s) against the levels and the long chart's ATR (1 min), and when
// its pattern completes it says which way the price should go: +1 up, -1 down. It keeps
// saying so for a while (45 s), then says nothing (0) until the next pattern.

namespace ob::sim {

struct FailedBreakoutSettings {
  double max_poke_atr = 0.25;   // a poke past the level is at most a quarter of an ATR...
  std::size_t back_within = 2;  // ...and a bar closes back within the next 2 bars
  std::uint64_t hold_ns = 45'000'000'000ULL;  // the read lasts 45 s after its bar ends
};

// B1, the failed breakout (a trap). A bar opens on one side of a level and pokes past it by
// a little; that bar or one of the next two closes back on the first side. The breakout
// traders are trapped, and the price should run the other way: against the poke.
//
// Example, a level at $276.40 and a 1 min ATR of 35 c (a poke of at most 8.75 c):
//   bar 1  open 276.36 high 276.46 close 276.44   pokes 6 c above: watch it
//   bar 2  open 276.44 high 276.45 close 276.33   closes back below: trap, read -1
// A poke of 10 c, or a bar going further than 8.75 c past before the close back, is a
// real breakout and reads nothing.
class FailedBreakout {
 public:
  explicit FailedBreakout(FailedBreakoutSettings settings = {});

  // A bar has just closed. `levels`: as they were when it opened (so a level the bar
  // itself makes, like a new day high, isn't one it can poke). `atr`: the long chart's, in
  // ticks; nothing is read while it warms up. Returns the read this bar completes: +1, -1,
  // or 0 (none, or a trap each way in the same bar).
  int on_bar(const Bar& bar, const std::vector<Level>& levels, std::optional<double> atr);

  // The read in force at t: the last trap's direction until hold_ns after its bar ended.
  [[nodiscard]] int direction(std::uint64_t t_ns) const;

 private:
  // A level poked and not yet closed back across: the trap may still complete.
  struct Poke {
    double level;           // ticks
    int side;               // +1 poked above, -1 below
    std::size_t bars_left;  // bars after the poke that may still close back
  };

  FailedBreakoutSettings settings_;
  std::vector<Poke> pokes_;
  int last_ = 0;                  // the last trap's read
  std::uint64_t last_end_ns_ = 0;  // when its bar ended
};

}  // namespace ob::sim
