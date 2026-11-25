#pragma once

#include "orderbook/sim/bars.hpp"
#include "orderbook/sim/chart_feed.hpp"
#include "orderbook/sim/levels.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

// Price-action reads that see a turn as it happens (round 7d-2, D63). Each one watches the
// short chart's bars (15 s) against the levels and the long chart's ATR (1 min), and when
// its pattern completes it says which way the price should go: +1 up, -1 down. It keeps
// saying so for a while (45 s), then says nothing (0) until the next pattern.

namespace ob::sim {

constexpr std::uint64_t kReadHoldNs = 45'000'000'000ULL;  // 45 s

// A read that, once a pattern completes, holds its direction for hold_ns from the end of
// the bar that completed it (nothing about a bar is known before it ends), then 0.
class HeldRead {
 public:
  explicit HeldRead(std::uint64_t hold_ns) : hold_ns_(hold_ns) {}
  void set(int read, std::uint64_t bar_end_ns);  // read 0 changes nothing
  [[nodiscard]] int direction(std::uint64_t t_ns) const;

 private:
  std::uint64_t hold_ns_;
  int last_ = 0;
  std::uint64_t last_end_ns_ = 0;
};

struct FailedBreakoutSettings {
  double max_poke_atr = 0.25;   // a poke past the level is at most a quarter of an ATR...
  std::size_t back_within = 2;  // ...and a bar closes back within the next 2 bars
  std::uint64_t hold_ns = kReadHoldNs;  // the read lasts 45 s after its bar ends
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
  [[nodiscard]] int direction(std::uint64_t t_ns) const { return held_.direction(t_ns); }

 private:
  // A level poked and not yet closed back across: the trap may still complete.
  struct Poke {
    double level;           // ticks
    int side;               // +1 poked above, -1 below
    std::size_t bars_left;  // bars after the poke that may still close back
  };

  FailedBreakoutSettings settings_;
  std::vector<Poke> pokes_;
  HeldRead held_;
};

// The long chart's trend from its EMA stack: +1 when 9 > 20 > 50, -1 when 9 < 20 < 50,
// 0 when mixed or still warming up.
int ema_stack(const ChartPoint& point);

struct PullbackEndSettings {
  std::size_t lookback = 4;      // the bar's low is the lowest of the last 4 (a 1 min dip)
  double touch_atr = 0.1;        // and within a tenth of an ATR of the EMA 20 or VWAP
  double close_in_end = 1.0 / 3;  // it closes in its top third (bottom, in a downtrend)
  std::uint64_t hold_ns = kReadHoldNs;
};

// B2, the pullback ends (Brooks' H1/H2 buy, L1/L2 sell). The long chart trends (its EMAs
// are stacked); a short-chart pullback reaches the EMA 20 or VWAP and a bar turns back
// with the trend there. Read: with the trend.
//
// Example, an uptrend on the 1 min chart, its EMA 20 at $276.20, a 1 min ATR of 35 c
// (a touch is within 3.5 c):
//   bars 0-2   lows 276.40, 276.32, 276.26            the pullback
//   bar 3      open 276.26 high 276.32 low 276.21 close 276.30
//              lowest of the 4, 1 c from the EMA, closes up, in its top third and above
//              the EMA: the pullback ended, read +1
class PullbackEnd {
 public:
  explicit PullbackEnd(PullbackEndSettings settings = {});

  // A bar has just closed. trend: the long chart's (ema_stack) as of this bar's open.
  // levels and atr as in FailedBreakout; only the EMA and VWAP levels are used.
  int on_bar(const Bar& bar, int trend, const std::vector<Level>& levels,
             std::optional<double> atr);

  [[nodiscard]] int direction(std::uint64_t t_ns) const { return held_.direction(t_ns); }

 private:
  PullbackEndSettings settings_;
  std::deque<Bar> recent_;  // the last lookback - 1 bars before this one
  HeldRead held_;
};

}  // namespace ob::sim
