#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

// Chart indicators, streaming: feed one closed bar's value at a time with update(); value()
// is nullopt until the indicator has seen enough bars (its warm-up). Values are in the
// same unit as what goes in (ticks for prices, 0-100 for RSI). D39.
//
// Definitions follow TradingView's, so the numbers can be checked against a chart:
// EMA seeded with the SMA of its first n values, population standard deviation in
// Bollinger, Wilder's smoothing in RSI.

namespace ob::sim {

// Simple moving average of the last n values. Any series, not only prices: Stochastic
// RSI smooths its %K and %D with it.
class Sma {
 public:
  explicit Sma(std::size_t n);
  void update(double x);
  [[nodiscard]] std::optional<double> value() const;

 private:
  std::size_t n_;
  std::deque<double> window_;
};

// Exponential moving average, alpha = 2 / (n + 1). The first value is the SMA of the first
// n inputs; from then on each input moves it by alpha of the gap.
class Ema {
 public:
  explicit Ema(std::size_t n);
  void update(double x);
  [[nodiscard]] std::optional<double> value() const;

 private:
  double alpha_;
  Sma seed_;
  std::optional<double> ema_;
};

struct BollingerValue {
  double middle = 0.0;     // SMA of the last n closes
  double upper = 0.0;      // middle + k standard deviations
  double lower = 0.0;      // middle - k standard deviations
  double percent_b = 0.0;  // where the last close is: 0 at the lower band, 1 at the upper
};

class Bollinger {
 public:
  Bollinger(std::size_t n, double k);
  void update(double close);
  [[nodiscard]] std::optional<BollingerValue> value() const;

 private:
  std::size_t n_;
  double k_;
  std::deque<double> window_;
};

// Relative strength index with Wilder's smoothing, 0-100.
class Rsi {
 public:
  explicit Rsi(std::size_t n);
  void update(double close);
  [[nodiscard]] std::optional<double> value() const;

 private:
  std::size_t n_;
  std::optional<double> previous_;  // last close, to take the change from
  std::size_t changes_ = 0;         // changes seen, up to n (warm-up)
  double avg_gain_ = 0.0;
  double avg_loss_ = 0.0;
};

struct StochRsiValue {
  double k = 0.0;  // 0-100
  double d = 0.0;  // 0-100, the slower line
};

// Stochastic RSI (TradingView's): where the RSI sits between its min and max of the last
// stoch_n values, smoothed into %K (SMA k_n) and %D (SMA d_n of %K).
class StochRsi {
 public:
  StochRsi(std::size_t rsi_n, std::size_t stoch_n, std::size_t k_n, std::size_t d_n);
  void update(double close);
  [[nodiscard]] std::optional<StochRsiValue> value() const;

 private:
  Rsi rsi_;
  std::size_t stoch_n_;
  std::deque<double> rsi_window_;
  Sma k_;
  Sma d_;
};

// Volume-weighted average price of the trades from start_ns on (09:30 for the regular
// session; earlier to include pre-market). Updated on every trade, not per bar.
class Vwap {
 public:
  explicit Vwap(std::uint64_t start_ns);
  void on_trade(std::uint64_t t_ns, Price price, std::uint64_t shares);
  [[nodiscard]] std::optional<double> value() const;  // ticks

 private:
  std::uint64_t start_ns_;
  std::int64_t notional_ = 0;  // sum of price x shares, exact in integers
  std::int64_t volume_ = 0;
};

}  // namespace ob::sim
