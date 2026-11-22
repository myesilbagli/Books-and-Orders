#include "orderbook/sim/imbalance.hpp"

#include <algorithm>

namespace ob::sim {

bool usable(const BookSample& s) {
  // A real touch has shares behind both prices; zero sizes would also make I undefined.
  return s.best_bid > 0 && s.best_ask > 0 && s.best_bid < s.best_ask && s.bid_size > 0 &&
         s.ask_size > 0;
}

double imbalance(const BookSample& s) {
  return static_cast<double>(s.bid_size) / static_cast<double>(s.bid_size + s.ask_size);
}

std::vector<ImbalanceBucket> imbalance_table(const std::vector<BookSample>& samples,
                                             std::size_t horizon, std::size_t buckets) {
  buckets = std::max<std::size_t>(buckets, 1);
  horizon = std::max<std::size_t>(horizon, 1);

  // Sums first, averages at the end: one pass over the day, no per-bucket vectors.
  struct Sums {
    std::size_t count = 0;
    double change = 0.0;
    double predicted = 0.0;
    std::size_t up = 0;
    std::size_t down = 0;
  };
  std::vector<Sums> sums(buckets);

  for (std::size_t i = 0; i + horizon < samples.size(); ++i) {
    const BookSample& now = samples[i];
    const BookSample& later = samples[i + horizon];
    if (!usable(now) || !usable(later)) {
      continue;  // a halt or an empty book at either end says nothing about prediction
    }
    const double I = imbalance(now);
    // min(): guards the last bucket against I * buckets rounding up to `buckets`.
    const auto raw = static_cast<std::size_t>(I * static_cast<double>(buckets));
    const std::size_t b = std::min(raw, buckets - 1);

    // Mids kept as bid + ask (twice the mid) until the end, so the change is exact.
    const auto change_x2 = (later.best_bid + later.best_ask) - (now.best_bid + now.best_ask);
    const double spread = static_cast<double>(now.best_ask - now.best_bid);

    Sums& s = sums[b];
    ++s.count;
    s.change += static_cast<double>(change_x2) / 2.0;
    // weighted mid - mid = (bid + I x spread) - (bid + spread / 2) = (I - 0.5) x spread
    s.predicted += (I - 0.5) * spread;
    s.up += change_x2 > 0 ? 1 : 0;
    s.down += change_x2 < 0 ? 1 : 0;
  }

  std::vector<ImbalanceBucket> table;
  table.reserve(buckets);
  for (std::size_t b = 0; b < buckets; ++b) {
    const Sums& s = sums[b];
    const double n = static_cast<double>(s.count);
    table.push_back(ImbalanceBucket{
        .low = static_cast<double>(b) / static_cast<double>(buckets),
        .high = static_cast<double>(b + 1) / static_cast<double>(buckets),
        .count = s.count,
        .mean_change_ticks = s.count ? s.change / n : 0.0,
        .mean_predicted_ticks = s.count ? s.predicted / n : 0.0,
        .up_share = s.count ? static_cast<double>(s.up) / n : 0.0,
        .down_share = s.count ? static_cast<double>(s.down) / n : 0.0,
    });
  }
  return table;
}

}  // namespace ob::sim
