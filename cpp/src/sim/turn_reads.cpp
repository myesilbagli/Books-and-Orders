#include "orderbook/sim/turn_reads.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ob::sim {

void HeldRead::set(int read, std::uint64_t bar_end_ns) {
  if (read != 0) {
    last_ = read;  // a newer read replaces the one in force
    last_end_ns_ = bar_end_ns;
  }
}

int HeldRead::direction(std::uint64_t t_ns) const {
  if (last_ != 0 && t_ns >= last_end_ns_ && t_ns < last_end_ns_ + hold_ns_) {
    return last_;
  }
  return 0;
}

FailedBreakout::FailedBreakout(FailedBreakoutSettings settings)
    : settings_(settings), held_(settings.hold_ns) {}

int FailedBreakout::on_bar(const Bar& bar, const std::vector<Level>& levels,
                           std::optional<double> atr) {
  const int read = read_bar(bar, levels, atr);
  recent_.push_back(bar);  // history counts every bar, read or not
  while (recent_.size() > settings_.new_extreme_bars) {
    recent_.pop_front();
  }
  held_.set(read, bar.end_ns);  // known only once the bar has closed
  return read;
}

int FailedBreakout::read_bar(const Bar& bar, const std::vector<Level>& levels,
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

  // A breakout attempt goes past every bar of the last 2 minutes; with fewer bars seen there
  // is nothing to break out of yet.
  const bool full = recent_.size() >= settings_.new_extreme_bars;
  bool new_high = full;
  bool new_low = full;
  for (const Bar& b : recent_) {
    new_high = new_high && bar.high_x2 > b.high_x2;
    new_low = new_low && bar.low_x2 < b.low_x2;
  }

  // New pokes: the bar opened on one side (at the level counts) and went a little past.
  for (const Level& level : levels) {
    if (std::find(settings_.kinds.begin(), settings_.kinds.end(), level.kind) ==
        settings_.kinds.end()) {
      continue;  // not a breakout level
    }
    const double l = level.price;
    if (new_high && open <= l && high > l && high - l <= max_poke) {
      if (close < l) {
        reads_down = true;  // back below in the same bar: a long upper tail at the level
      } else if (settings_.back_within > 0) {
        waiting.push_back(Poke{l, +1, settings_.back_within});
      }
    }
    if (new_low && open >= l && low < l && l - low <= max_poke) {
      if (close > l) {
        reads_up = true;
      } else if (settings_.back_within > 0) {
        waiting.push_back(Poke{l, -1, settings_.back_within});
      }
    }
  }
  pokes_ = std::move(waiting);  // move: hand the vector over instead of copying it

  // Traps both ways in one bar say opposite things: no read.
  return reads_up == reads_down ? 0 : (reads_up ? 1 : -1);
}

int ema_stack(const ChartPoint& point) {
  if (!point.ema_fast || !point.ema_mid || !point.ema_long) {
    return 0;
  }
  const double fast = *point.ema_fast;
  const double mid = *point.ema_mid;
  const double slow = *point.ema_long;
  if (fast > mid && mid > slow) {
    return 1;
  }
  if (fast < mid && mid < slow) {
    return -1;
  }
  return 0;
}

PullbackEnd::PullbackEnd(PullbackEndSettings settings)
    : settings_(settings), held_(settings.hold_ns) {}

int PullbackEnd::on_bar(const Bar& bar, int trend, const std::vector<Level>& levels,
                        std::optional<double> atr) {
  int read = 0;
  // Only a full window can say "the lowest of the last n"; and only a trend has pullbacks.
  if (atr && *atr > 0.0 && trend != 0 && settings_.lookback > 0 &&
      recent_.size() + 1 >= settings_.lookback) {
    const bool up = trend > 0;
    // Work in the trend's direction: in an uptrend the pullback's extreme is the low.
    const Price extreme_x2 = up ? bar.low_x2 : bar.high_x2;
    bool deepest = true;
    for (const Bar& b : recent_) {
      deepest = deepest && (up ? extreme_x2 <= b.low_x2 : extreme_x2 >= b.high_x2);
    }
    const double extreme = static_cast<double>(extreme_x2) / 2.0;
    const double open = static_cast<double>(bar.open_x2) / 2.0;
    const double close = static_cast<double>(bar.close_x2) / 2.0;
    const double high = static_cast<double>(bar.high_x2) / 2.0;
    const double low = static_cast<double>(bar.low_x2) / 2.0;
    const double reach = settings_.close_in_end * (high - low);
    // A reversal bar with the trend: closes the trend's way, in the end of its range
    // away from the pullback (top third in an uptrend).
    const bool turns = up ? (close > open && close >= high - reach)
                          : (close < open && close <= low + reach);
    bool at_level = false;
    for (const Level& l : levels) {
      if (l.kind != LevelKind::Ema && l.kind != LevelKind::Vwap) {
        continue;  // B2 is about the moving averages a trend pulls back to
      }
      const bool near = std::abs(extreme - l.price) <= settings_.touch_atr * *atr;
      const bool held = up ? close > l.price : close < l.price;  // closed back on its side
      at_level = at_level || (near && held);
    }
    if (deepest && turns && at_level) {
      read = trend > 0 ? 1 : -1;
    }
  }
  recent_.push_back(bar);
  while (recent_.size() >= settings_.lookback && !recent_.empty()) {
    recent_.pop_front();  // keep the lookback - 1 bars before the next one
  }
  held_.set(read, bar.end_ns);
  return read;
}

}  // namespace ob::sim
