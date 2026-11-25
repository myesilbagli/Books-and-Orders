#include "orderbook/sim/levels.hpp"

#include <algorithm>
#include <cmath>

namespace ob::sim {

namespace {

double ticks(Price x2) { return static_cast<double>(x2) / 2.0; }

}  // namespace

DayRange::DayRange(std::uint64_t from_ns) : from_ns_(from_ns) {}

void DayRange::on_bar(const Bar& bar) {
  if (bar.start_ns < from_ns_) {
    return;  // warm-up bars: the session's range starts at its open
  }
  if (!range_) {
    range_ = bar;  // its open stays the day's open; high and low widen from here
    return;
  }
  range_->high_x2 = std::max(range_->high_x2, bar.high_x2);
  range_->low_x2 = std::min(range_->low_x2, bar.low_x2);
}

std::optional<double> DayRange::open() const {
  return range_ ? std::optional<double>(ticks(range_->open_x2)) : std::nullopt;
}
std::optional<double> DayRange::high() const {
  return range_ ? std::optional<double>(ticks(range_->high_x2)) : std::nullopt;
}
std::optional<double> DayRange::low() const {
  return range_ ? std::optional<double>(ticks(range_->low_x2)) : std::nullopt;
}

std::vector<Level> levels_at(std::span<const Swing> swings, const DayRange& day,
                             std::optional<double> vwap, std::optional<double> ema,
                             double price, const LevelSettings& settings) {
  std::vector<Level> out;
  const auto add = [&out](LevelKind kind, std::optional<double> p) {
    if (p) {
      out.push_back(Level{kind, *p});
    }
  };

  // Newest first, until we have enough of each kind: the swings vector only grows, so the
  // last ones are the most recent.
  std::size_t highs = 0;
  std::size_t lows = 0;
  for (auto it = swings.rbegin(); it != swings.rend(); ++it) {
    const bool high = it->kind == SwingKind::High;
    std::size_t& count = high ? highs : lows;
    if (count < settings.swings_each) {
      ++count;
      add(high ? LevelKind::SwingHigh : LevelKind::SwingLow, ticks(it->price_x2));
    }
    if (highs >= settings.swings_each && lows >= settings.swings_each) {
      break;
    }
  }

  add(LevelKind::DayHigh, day.high());
  add(LevelKind::DayLow, day.low());
  add(LevelKind::DayOpen, day.open());
  add(LevelKind::Vwap, vwap);
  add(LevelKind::Ema, ema);

  const double step = static_cast<double>(settings.round_step);
  const double below = std::floor(price / step) * step;  // at or below the price
  add(LevelKind::Round, below);
  add(LevelKind::Round, below + step);

  // Stable: levels at the same price keep a fixed order, so runs are reproducible.
  std::stable_sort(out.begin(), out.end(),
                   [](const Level& a, const Level& b) { return a.price < b.price; });
  return out;
}

}  // namespace ob::sim
