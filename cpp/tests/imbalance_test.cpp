#include "orderbook/sim/imbalance.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob::sim {
namespace {

// A sample on a 1-cent market (100 ticks of $0.0001) with the given bid and queue sizes.
BookSample at(std::uint64_t t, Price bid, std::uint64_t bid_size, std::uint64_t ask_size) {
  return BookSample{.timestamp_ns = t,
                    .best_bid = bid,
                    .best_ask = bid + 100,
                    .bid_size = bid_size,
                    .ask_size = ask_size};
}

TEST(ImbalanceTest, ImbalanceIsTheBidsShareOfTheTouch) {
  EXPECT_DOUBLE_EQ(imbalance(at(0, 10'000, 100, 300)), 0.25);
  EXPECT_DOUBLE_EQ(imbalance(at(0, 10'000, 500, 500)), 0.5);
}

TEST(ImbalanceTest, OnlyTwoSidedUncrossedBooksAreUsable) {
  EXPECT_TRUE(usable(at(0, 10'000, 100, 100)));
  EXPECT_FALSE(usable(BookSample{.timestamp_ns = 0, .best_bid = 10'000, .best_ask = 0,
                                 .bid_size = 100, .ask_size = 0}));  // no ask
  EXPECT_FALSE(usable(BookSample{.timestamp_ns = 0, .best_bid = 10'100, .best_ask = 10'000,
                                 .bid_size = 100, .ask_size = 100}));  // crossed
  EXPECT_FALSE(usable(at(0, 10'000, 100, 0)));  // a price with no shares behind it
}

TEST(ImbalanceTest, OneEntryPerBucketWithItsRange) {
  const auto table = imbalance_table({}, 1, 4);
  ASSERT_EQ(table.size(), 4u);
  EXPECT_DOUBLE_EQ(table[1].low, 0.25);
  EXPECT_DOUBLE_EQ(table[1].high, 0.5);
  EXPECT_EQ(table[1].count, 0u);
}

// Thin bid (I = 0.1) and the mid then falls a cent; thin ask (I = 0.9) and it rises.
//   t0 I=0.1 bid 1.0000 -> t1 bid 0.9900: change -100 ticks (in bucket 0)
//   t1 I=0.9 bid 0.9900 -> t2 bid 1.0000: change +100 ticks (in bucket 1)
TEST(ImbalanceTest, AveragesTheMidChangeOneHorizonLater) {
  const std::vector<BookSample> samples = {
      at(0, 10'000, 100, 900),
      at(1, 9'900, 900, 100),
      at(2, 10'000, 500, 500),
  };
  const auto table = imbalance_table(samples, 1, 2);
  ASSERT_EQ(table.size(), 2u);
  EXPECT_EQ(table[0].count, 1u);
  EXPECT_DOUBLE_EQ(table[0].mean_change_ticks, -100.0);
  EXPECT_DOUBLE_EQ(table[0].down_share, 1.0);
  EXPECT_EQ(table[1].count, 1u);  // t1 -> t2; t2 itself has no later sample
  EXPECT_DOUBLE_EQ(table[1].mean_change_ticks, 100.0);
  EXPECT_DOUBLE_EQ(table[1].up_share, 1.0);
}

// The weighted mid's prediction: (I - 0.5) x spread. I = 0.1 on a 100-tick spread: -40.
TEST(ImbalanceTest, ReportsWhatTheWeightedMidPredicted) {
  const std::vector<BookSample> samples = {
      at(0, 10'000, 100, 900),
      at(1, 10'000, 100, 900),
  };
  const auto table = imbalance_table(samples, 1, 2);
  EXPECT_EQ(table[0].count, 1u);
  EXPECT_DOUBLE_EQ(table[0].mean_predicted_ticks, -40.0);
  EXPECT_DOUBLE_EQ(table[0].mean_change_ticks, 0.0);
  EXPECT_DOUBLE_EQ(table[0].up_share, 0.0);
  EXPECT_DOUBLE_EQ(table[0].down_share, 0.0);
}

// The horizon counts in samples; pairs that would run past the end are dropped.
TEST(ImbalanceTest, LongerHorizonLooksFurtherAhead) {
  const std::vector<BookSample> samples = {
      at(0, 10'000, 500, 500),
      at(1, 10'100, 500, 500),
      at(2, 10'300, 500, 500),
  };
  const auto table = imbalance_table(samples, 2, 1);
  EXPECT_EQ(table[0].count, 1u);                         // only t0 -> t2
  EXPECT_DOUBLE_EQ(table[0].mean_change_ticks, 300.0);
  EXPECT_DOUBLE_EQ(table[0].up_share, 1.0);
}

// A halt (no usable book) at either end of a pair drops that pair.
TEST(ImbalanceTest, SkipsPairsWithAnUnusableEnd) {
  std::vector<BookSample> samples = {
      at(0, 10'000, 500, 500),
      BookSample{.timestamp_ns = 1, .best_bid = 0, .best_ask = 0, .bid_size = 0, .ask_size = 0},
      at(2, 10'100, 500, 500),
  };
  const auto table = imbalance_table(samples, 1, 1);
  EXPECT_EQ(table[0].count, 0u);  // t0 -> t1 and t1 -> t2 both touch the halt
}

// An imbalance just under 1 goes in the last bucket, never one past the end.
TEST(ImbalanceTest, NearOneGoesInTheLastBucket) {
  const std::vector<BookSample> samples = {
      at(0, 10'000, 1'000'000, 1),
      at(1, 10'000, 1'000'000, 1),
  };
  const auto table = imbalance_table(samples, 1, 10);
  EXPECT_EQ(table[9].count, 1u);
}

}  // namespace
}  // namespace ob::sim
