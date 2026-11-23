#include "orderbook/sim/structure.hpp"

namespace ob::sim {

namespace {

int sign(Price x) { return (x > 0) - (x < 0); }  // the comparisons are 1 or 0

}  // namespace

std::optional<Swing> ZigZag::on_bar(const Bar& bar, double delta_x2) {
  if (!started_) {
    // The first bar only sets where the extremes start.
    started_ = true;
    high_x2_ = bar.high_x2;
    low_x2_ = bar.low_x2;
    high_time_ = low_time_ = bar.start_ns;
    return std::nullopt;
  }
  const bool can_confirm = delta_x2 > 0.0;  // no ATR yet: watch, but don't decide

  switch (leg_) {
    case Leg::None: {
      // No swing yet: follow both extremes. The first one the other has moved delta away
      // from is the first swing, and the earlier of the two must be it.
      if (bar.high_x2 > high_x2_) {
        high_x2_ = bar.high_x2;
        high_time_ = bar.start_ns;
      }
      if (bar.low_x2 < low_x2_) {
        low_x2_ = bar.low_x2;
        low_time_ = bar.start_ns;
      }
      // One bar that made both extremes doesn't say which came first: wait for more.
      if (!can_confirm || high_time_ == low_time_ ||
          static_cast<double>(high_x2_ - low_x2_) < delta_x2) {
        return std::nullopt;
      }
      if (low_time_ < high_time_) {  // low, then a rise of delta: the low is a swing
        leg_ = Leg::Up;              // now following the high, which is already set
        return confirm(SwingKind::Low, low_x2_, low_time_, bar.end_ns);
      }
      leg_ = Leg::Down;
      return confirm(SwingKind::High, high_x2_, high_time_, bar.end_ns);
    }
    case Leg::Up:
      // A new high moves the candidate, and that bar can't also confirm it: inside one
      // bar we can't tell whether the low came before or after the high.
      if (bar.high_x2 > high_x2_) {
        high_x2_ = bar.high_x2;
        high_time_ = bar.start_ns;
        return std::nullopt;
      }
      if (!can_confirm || static_cast<double>(high_x2_ - bar.low_x2) < delta_x2) {
        return std::nullopt;  // a pullback smaller than delta is noise
      }
      leg_ = Leg::Down;  // the high is a swing; now follow the low from this bar
      low_x2_ = bar.low_x2;
      low_time_ = bar.start_ns;
      return confirm(SwingKind::High, high_x2_, high_time_, bar.end_ns);
    case Leg::Down:
      if (bar.low_x2 < low_x2_) {
        low_x2_ = bar.low_x2;
        low_time_ = bar.start_ns;
        return std::nullopt;
      }
      if (!can_confirm || static_cast<double>(bar.high_x2 - low_x2_) < delta_x2) {
        return std::nullopt;
      }
      leg_ = Leg::Up;
      high_x2_ = bar.high_x2;
      high_time_ = bar.start_ns;
      return confirm(SwingKind::Low, low_x2_, low_time_, bar.end_ns);
  }
  return std::nullopt;  // not reached: every Leg is handled above
}

Swing ZigZag::confirm(SwingKind kind, Price price_x2, std::uint64_t time_ns,
                      std::uint64_t confirmed_ns) {
  LastTwo& last = kind == SwingKind::High ? highs_ : lows_;
  Swing s{.kind = kind, .price_x2 = price_x2, .time_ns = time_ns, .confirmed_ns = confirmed_ns,
          .change = std::nullopt};
  if (last.newer) {
    s.change = sign(price_x2 - *last.newer);
  }
  last.older = last.newer;
  last.newer = price_x2;
  swings_.push_back(s);
  // Two of each kind: the score exists from now on, and changes only here, at a swing.
  if (highs_.older && lows_.older) {
    score_ = sign(*highs_.newer - *highs_.older) + sign(*lows_.newer - *lows_.older);
  }
  return s;
}

MarketStructure::MarketStructure(StructureSettings settings)
    : settings_(settings), atr_(settings.atr_n) {}

std::optional<Swing> MarketStructure::on_bar(const Bar& bar) {
  // ATR first, so this bar's own range counts in the delta it is judged by. Bars keep the
  // doubled mid; ATR works in ticks, like every other indicator.
  atr_.update(static_cast<double>(bar.high_x2) / 2.0, static_cast<double>(bar.low_x2) / 2.0,
              static_cast<double>(bar.close_x2) / 2.0);
  const std::optional<double> atr = atr_.value();
  const double delta_x2 = atr ? 2.0 * settings_.c * *atr : 0.0;
  return zigzag_.on_bar(bar, delta_x2);
}

}  // namespace ob::sim
