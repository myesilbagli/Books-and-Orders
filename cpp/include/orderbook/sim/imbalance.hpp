#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Does the queue imbalance at the touch predict where the mid goes next? (D32)
//
//   imbalance I = bid_size / (bid_size + ask_size)       0: bid thin ... 1: ask thin
//   weighted mid = bid + I * spread                       the simple "microprice"
//
// The book is sampled on an evenly spaced clock grid, the samples are grouped by I, and
// each group's average mid change `horizon` samples later is compared with what the
// weighted mid would have predicted (weighted mid - mid).

namespace ob::sim {

// The touch at one grid point. A sample without a two-sided, uncrossed book (halts,
// before the open) is kept so the grid stays evenly spaced, but never used.
struct BookSample {
  std::uint64_t timestamp_ns;
  Price best_bid;  // 0 if no bid
  Price best_ask;  // 0 if no ask
  std::uint64_t bid_size;
  std::uint64_t ask_size;
};

[[nodiscard]] bool usable(const BookSample& s);
[[nodiscard]] double imbalance(const BookSample& s);  // s must be usable

struct ImbalanceBucket {
  double low;               // imbalance range [low, high)
  double high;
  std::size_t count;        // pairs (now, now + horizon) where both samples are usable
  double mean_change_ticks;     // realized: mid(later) - mid(now)
  double mean_predicted_ticks;  // weighted mid - mid, at now
  double up_share;          // fraction of pairs where the mid went up
  double down_share;        // ... down (the rest didn't move)
};

// `samples` evenly spaced in time; `horizon` in samples (> 0); `buckets` equal-width
// imbalance groups (> 0). Returns one entry per bucket, empty ones included.
[[nodiscard]] std::vector<ImbalanceBucket> imbalance_table(const std::vector<BookSample>& samples,
                                                           std::size_t horizon,
                                                           std::size_t buckets);

}  // namespace ob::sim
