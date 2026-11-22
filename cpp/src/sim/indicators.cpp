#include "orderbook/sim/indicators.hpp"

#include <algorithm>
#include <cmath>

namespace ob::sim {

// Every length is clamped to 1: a zero-length window would never be "warm".

// --- SMA -------------------------------------------------------------------------------

Sma::Sma(std::size_t n) : n_(std::max<std::size_t>(n, 1)) {}

void Sma::update(double x) {
  window_.push_back(x);
  if (window_.size() > n_) {
    window_.pop_front();
  }
}

std::optional<double> Sma::value() const {
  if (window_.size() < n_) {
    return std::nullopt;
  }
  // Summed afresh each time, not kept as a running total: adding and later subtracting
  // the same doubles leaves rounding dust (0.1 + 0.1 + 0.1 - 0.1 - 0.1 - 0.1 != 0), which
  // showed up as "-0.00" in Stochastic RSI. n is small (3 to 21), so this is cheap.
  double sum = 0.0;
  for (double v : window_) {
    sum += v;
  }
  return sum / static_cast<double>(n_);
}

// --- EMA -------------------------------------------------------------------------------

Ema::Ema(std::size_t n)
    : alpha_(2.0 / (static_cast<double>(std::max<std::size_t>(n, 1)) + 1.0)), seed_(n) {}

void Ema::update(double x) {
  if (ema_) {
    *ema_ += alpha_ * (x - *ema_);
    return;
  }
  // Until n values are in, collect them; the first EMA is their plain average. Starting
  // from the first value instead would weight that one bar far too much for a while.
  seed_.update(x);
  ema_ = seed_.value();
}

std::optional<double> Ema::value() const { return ema_; }

// --- Bollinger -------------------------------------------------------------------------

Bollinger::Bollinger(std::size_t n, double k) : n_(std::max<std::size_t>(n, 1)), k_(k) {}

void Bollinger::update(double close) {
  window_.push_back(close);
  if (window_.size() > n_) {
    window_.pop_front();
  }
}

std::optional<BollingerValue> Bollinger::value() const {
  if (window_.size() < n_) {
    return std::nullopt;
  }
  // Two passes over the window (mean, then squared gaps from it), not a running sum of
  // squares: at ~29 M ticks, mean(x^2) - mean(x)^2 subtracts two numbers near 8e14 to find
  // a variance of a few ticks^2, and doubles can't hold that difference. n is about 20,
  // so recomputing is cheap.
  double sum = 0.0;
  for (double x : window_) {
    sum += x;
  }
  const double mean = sum / static_cast<double>(n_);
  double squares = 0.0;
  for (double x : window_) {
    squares += (x - mean) * (x - mean);
  }
  // Population standard deviation (divide by n, not n - 1), as TradingView's ta.stdev.
  const double sd = std::sqrt(squares / static_cast<double>(n_));
  BollingerValue v{.middle = mean, .upper = mean + k_ * sd, .lower = mean - k_ * sd};
  const double width = v.upper - v.lower;
  // A flat window has no band: the close is in the middle of it.
  v.percent_b = width > 0.0 ? (window_.back() - v.lower) / width : 0.5;
  return v;
}

// --- RSI -------------------------------------------------------------------------------

Rsi::Rsi(std::size_t n) : n_(std::max<std::size_t>(n, 1)) {}

void Rsi::update(double close) {
  if (!previous_) {
    previous_ = close;
    return;
  }
  const double change = close - *previous_;
  previous_ = close;
  const double gain = std::max(change, 0.0);
  const double loss = std::max(-change, 0.0);
  const double n = static_cast<double>(n_);
  if (changes_ < n_) {
    // Warm-up: the first averages are plain averages of the first n changes.
    avg_gain_ += gain / n;
    avg_loss_ += loss / n;
    ++changes_;
    return;
  }
  // Wilder's smoothing: an EMA with alpha = 1/n (slower than the usual 2/(n+1)).
  avg_gain_ = (avg_gain_ * (n - 1.0) + gain) / n;
  avg_loss_ = (avg_loss_ * (n - 1.0) + loss) / n;
}

std::optional<double> Rsi::value() const {
  if (changes_ < n_) {
    return std::nullopt;
  }
  // 100 - 100 / (1 + gain/loss) rewritten as 100 * gain / (gain + loss): the same number,
  // without dividing by a zero loss when the price only went up.
  const double total = avg_gain_ + avg_loss_;
  if (total == 0.0) {
    return 50.0;  // no move at all: neutral (D39)
  }
  return 100.0 * avg_gain_ / total;
}

// --- Stochastic RSI --------------------------------------------------------------------

StochRsi::StochRsi(std::size_t rsi_n, std::size_t stoch_n, std::size_t k_n, std::size_t d_n)
    : rsi_(rsi_n), stoch_n_(std::max<std::size_t>(stoch_n, 1)), k_(k_n), d_(d_n) {}

void StochRsi::update(double close) {
  rsi_.update(close);
  const std::optional<double> rsi = rsi_.value();
  if (!rsi) {
    return;
  }
  rsi_window_.push_back(*rsi);
  if (rsi_window_.size() > stoch_n_) {
    rsi_window_.pop_front();
  }
  if (rsi_window_.size() < stoch_n_) {
    return;
  }
  double low = rsi_window_.front();
  double high = rsi_window_.front();
  for (double r : rsi_window_) {
    low = std::min(low, r);
    high = std::max(high, r);
  }
  const double range = high - low;
  // A flat RSI has no range: neutral, as with RSI itself (D39).
  const double stoch = range > 0.0 ? 100.0 * (*rsi - low) / range : 50.0;
  k_.update(stoch);
  const std::optional<double> k = k_.value();
  if (k) {  // %D averages %K, so it starts only once %K exists
    d_.update(*k);
  }
}

std::optional<StochRsiValue> StochRsi::value() const {
  const std::optional<double> k = k_.value();
  const std::optional<double> d = d_.value();
  if (!k || !d) {
    return std::nullopt;
  }
  return StochRsiValue{.k = *k, .d = *d};
}

// --- VWAP ------------------------------------------------------------------------------

Vwap::Vwap(std::uint64_t start_ns) : start_ns_(start_ns) {}

void Vwap::on_trade(std::uint64_t t_ns, Price price, std::uint64_t shares) {
  if (t_ns < start_ns_) {
    return;
  }
  // Integers, so the sum is exact: a day of AAPL on Nasdaq is ~1e13 tick-shares, far
  // below the int64 limit (~9e18).
  const auto q = static_cast<std::int64_t>(shares);
  notional_ += price * q;
  volume_ += q;
}

std::optional<double> Vwap::value() const {
  if (volume_ == 0) {
    return std::nullopt;
  }
  return static_cast<double>(notional_) / static_cast<double>(volume_);
}

}  // namespace ob::sim
