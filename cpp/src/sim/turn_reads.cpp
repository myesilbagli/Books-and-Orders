#include "orderbook/sim/turn_reads.hpp"

#include <utility>

namespace ob::sim {

FailedBreakout::FailedBreakout(FailedBreakoutSettings settings) : settings_(settings) {}

int FailedBreakout::on_bar(const Bar& bar, const std::vector<Level>& levels,
                           std::optional<double> atr) {
  if (!atr || *atr <= 0.0) {
    pokes_.clear();  // no yardstick: nothing can be called small, so nothing is watched
    return 0;
  }
  // Ticks, like the levels (bars keep the mid doubled).
  const double open = static_cast<double>(bar.open_x2) / 2.0;
  const double high = static_cast<double>(bar.high_x2) / 2.0;
  const double low = static_cast<double>(bar.low_x2) / 2.0;
  const double close = static_cast<double>(bar.close_x2) / 2.0;
  const double max_poke = settings_.max_poke_atr * *atr;
  bool reads_up = false;    // a trap below a level completed in this bar
  bool reads_down = false;  // a trap above one

  // Earlier pokes: this bar follows through (a real breakout), closes back (the trap), or
  // leaves them waiting one bar fewer.
  std::vector<Poke> waiting;
  for (Poke p : pokes_) {
    const double past = p.side > 0 ? high - p.level : p.level - low;
    if (past > max_poke) {
      continue;  // it went on: the breakout worked
    }
    if (p.side > 0 ? close < p.level : close > p.level) {
      (p.side > 0 ? reads_down : reads_up) = true;  // against the poke
      continue;
    }
    if (--p.bars_left > 0) {
      waiting.push_back(p);
    }
  }

  // New pokes: the bar opened on one side (at the level counts) and went a little past.
  for (const Level& level : levels) {
    const double l = level.price;
    if (open <= l && high > l && high - l <= max_poke) {
      if (close < l) {
        reads_down = true;  // back below in the same bar: a long upper tail at the level
      } else if (settings_.back_within > 0) {
        waiting.push_back(Poke{l, +1, settings_.back_within});
      }
    }
    if (open >= l && low < l && l - low <= max_poke) {
      if (close > l) {
        reads_up = true;
      } else if (settings_.back_within > 0) {
        waiting.push_back(Poke{l, -1, settings_.back_within});
      }
    }
  }
  pokes_ = std::move(waiting);  // move: hand the vector over instead of copying it

  // Traps both ways in one bar say opposite things: no read.
  const int read = reads_up == reads_down ? 0 : (reads_up ? 1 : -1);
  if (read != 0) {
    last_ = read;
    last_end_ns_ = bar.end_ns;  // known only once the bar has closed
  }
  return read;
}

int FailedBreakout::direction(std::uint64_t t_ns) const {
  if (last_ != 0 && t_ns >= last_end_ns_ && t_ns < last_end_ns_ + settings_.hold_ns) {
    return last_;
  }
  return 0;
}

}  // namespace ob::sim
