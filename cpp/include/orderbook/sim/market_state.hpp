#pragma once

#include "orderbook/order.hpp"
#include "orderbook/sim/chart_feed.hpp"
#include "orderbook/sim/indicators.hpp"
#include "orderbook/sim/trades.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// One stock's charts: 1 s, 15 s, 1 min and 5 min bars with their indicators and market
// structure, and the VWAP (D44, D49). The simulator keeps one and shows it to the strategy read-only; chart_dump uses
// the same class, so what a strategy sees is what was checked against Databento (D42).
//
// Feed it, per message of the stock, in this order:
//   advance(t)   before anything at t: closes the bars that are over
//   on_trade()   if the message is a trade (read before the book applies it)
//   on_mid()     after the book applied it, if the mid changed

namespace ob::sim {

// The swing size c of each timeframe's structure (D48). Picked by eye on the hub with the
// fills hidden (D51): 1.5 for 1 min, 2 for 15 s. The others keep 2 (not tested).
struct SwingSizes {
  double one_second = 2.0;
  double fifteen_seconds = 2.0;
  double one_minute = 1.5;
  double five_minutes = 2.0;
};

struct MarketStateConfig {
  std::uint64_t from_ns = 9ULL * 3600 * 1'000'000'000;  // bars start (pre-market warm-up)
  std::uint64_t to_ns = 16ULL * 3600 * 1'000'000'000;   // and stop
  std::uint64_t vwap_from_ns = (9ULL * 3600 + 30 * 60) * 1'000'000'000;  // VWAP counts from
  ChartSettings settings;  // the founder's chart by default (its structure.c is replaced:)
  SwingSizes swing_c;      // c per timeframe
};

// The bars closed by one advance(), per timeframe, oldest first.
struct ClosedBars {
  std::vector<ChartPoint> one_second;
  std::vector<ChartPoint> fifteen_seconds;
  std::vector<ChartPoint> one_minute;
  std::vector<ChartPoint> five_minutes;
};

class MarketState {
 public:
  explicit MarketState(const MarketStateConfig& config = {});

  // Events outside [from_ns, to_ns) are ignored; advance() past to_ns closes the bars up
  // to to_ns, once.
  ClosedBars advance(std::uint64_t t_ns);
  void on_trade(const Trade& trade);
  void on_mid(std::uint64_t t_ns, Price mid_x2);

  [[nodiscard]] const ChartFeed& one_second() const { return one_second_; }
  [[nodiscard]] const ChartFeed& fifteen_seconds() const { return fifteen_seconds_; }
  [[nodiscard]] const ChartFeed& one_minute() const { return one_minute_; }
  [[nodiscard]] const ChartFeed& five_minutes() const { return five_minutes_; }
  [[nodiscard]] const Vwap& vwap() const { return vwap_; }

 private:
  MarketStateConfig config_;
  ChartFeed one_second_;
  ChartFeed fifteen_seconds_;
  ChartFeed one_minute_;
  ChartFeed five_minutes_;
  Vwap vwap_;
  ClosedBars pending_;  // closed since the last advance(), waiting to be handed out
  std::optional<Price> carried_mid_x2_;  // the last mid before from_ns
  bool started_ = false;                 // the window has opened

  // At the first event at or after from_ns: open the bars at from_ns with the mid in
  // force then, which usually changed before the window (else the first bar would wait
  // for the next change).
  void start_if_due(std::uint64_t t_ns);

  [[nodiscard]] bool inside(std::uint64_t t_ns) const {
    return t_ns >= config_.from_ns && t_ns < config_.to_ns;
  }
};

}  // namespace ob::sim
