#include "orderbook/sim/pull_when_thin.hpp"

#include "itch_builders.hpp"
#include "orderbook/sim/simulator.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

// Inner strategy for tests: always quotes 1.0000 / 1.0010 and counts how often it's asked.
class Fixed final : public Strategy {
 public:
  Quotes quotes{.bid = 10'000, .ask = 10'010};
  int calls = 0;
  Quotes quote(const MarketView& view) override {
    (void)view;
    ++calls;
    return quotes;
  }
};

MarketView sizes(std::uint64_t bid_size, std::uint64_t ask_size) {
  return MarketView{.now_ns = 0,
                    .best_bid = 10'000,
                    .best_ask = 10'010,
                    .bid_size = bid_size,
                    .ask_size = ask_size,
                    .inventory = 0};
}

TEST(PullWhenThinTest, BalancedTouchKeepsBothSides) {
  Fixed inner;
  PullWhenThin pull(inner, 0.2);
  const Quotes q = pull.quote(sizes(500, 500));
  EXPECT_EQ(q.bid, 10'000);
  EXPECT_EQ(q.ask, 10'010);
}

// Bid side 100 of 1,000 shares at the touch: 10 % < 20 %, so no bid. The ask stays.
TEST(PullWhenThinTest, ThinBidIsPulled) {
  Fixed inner;
  PullWhenThin pull(inner, 0.2);
  const Quotes q = pull.quote(sizes(100, 900));
  EXPECT_FALSE(q.bid.has_value());
  EXPECT_EQ(q.ask, 10'010);
}

TEST(PullWhenThinTest, ThinAskIsPulled) {
  Fixed inner;
  PullWhenThin pull(inner, 0.2);
  const Quotes q = pull.quote(sizes(900, 100));
  EXPECT_EQ(q.bid, 10'000);
  EXPECT_FALSE(q.ask.has_value());
}

// "Below" the threshold pulls; exactly at it stays.
TEST(PullWhenThinTest, ExactlyAtTheThresholdStays) {
  Fixed inner;
  PullWhenThin pull(inner, 0.2);
  EXPECT_TRUE(pull.quote(sizes(200, 800)).bid.has_value());
  EXPECT_TRUE(pull.quote(sizes(800, 200)).ask.has_value());  // same rule on the ask
}

TEST(PullWhenThinTest, ZeroThresholdNeverPulls) {
  Fixed inner;
  PullWhenThin pull(inner, 0.0);
  const Quotes q = pull.quote(sizes(1, 999'999));
  EXPECT_TRUE(q.bid.has_value());
  EXPECT_TRUE(q.ask.has_value());
}

// The inner strategy is asked every time, pulled or not, so it can keep warming up
// (Avellaneda-Stoikov's volatility estimate needs every message).
TEST(PullWhenThinTest, AlwaysAsksTheInnerStrategy) {
  Fixed inner;
  PullWhenThin pull(inner, 0.2);
  (void)pull.quote(sizes(100, 900));
  (void)pull.quote(sizes(500, 500));
  EXPECT_EQ(inner.calls, 2);
}

// The wrapper only takes away; it never adds a side the inner strategy left empty.
TEST(PullWhenThinTest, NeverAddsASide) {
  Fixed inner;
  inner.quotes.ask.reset();
  PullWhenThin pull(inner, 0.2);
  EXPECT_FALSE(pull.quote(sizes(500, 500)).ask.has_value());
}

// In the simulator: a thin bid side at the start means we never join the bid.
TEST(PullWhenThinTest, SimulatorDoesNotJoinAThinBid) {
  using namespace itch_test;
  constexpr std::uint16_t kAapl = 13;
  NaiveStrategy naive;
  PullWhenThin pull(naive, 0.2);
  Simulator sim(SimConfig{.locate = kAapl,
                          .quote_size = 100,
                          .latency_ns = 0,
                          .max_inventory = 500,
                          .start_ns = 0,
                          .end_ns = 24ULL * 3600 * 1'000'000'000},
                pull);
  const Bytes bid = add(kAapl, 1, 'B', 100, 10'000);
  const Bytes ask = add(kAapl, 2, 'S', 900, 10'010);
  sim.on_message(bid.data(), bid.size());
  sim.on_message(ask.data(), ask.size());
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_TRUE(sim.ask().has_value());
}

}  // namespace
}  // namespace ob::sim
