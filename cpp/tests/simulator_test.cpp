#include "orderbook/sim/simulator.hpp"

#include "itch_builders.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

using namespace itch_test;  // message builders: add(), executed(), deleted(), ...

constexpr std::uint16_t kAapl = 13;

// Quote all day (the tests' clocks start near 0), no latency unless a test sets it.
SimConfig config(std::uint64_t latency_ns = 0) {
  return SimConfig{.locate = kAapl,
                   .quote_size = 100,
                   .latency_ns = latency_ns,
                   .max_inventory = 500,
                   .start_ns = 0,
                   .end_ns = 24ULL * 3600 * 1'000'000'000};
}

void feed(Simulator& sim, const Bytes& b) { sim.on_message(b.data(), b.size()); }

// A two-sided market: bid #1 100 @ 1.0000, ask #2 100 @ 1.0010. Once both exist the
// strategy joins both, at the back of each queue.
void open_market(Simulator& sim, std::uint64_t ts = 1000) {
  feed(sim, add(kAapl, 1, 'B', 100, 10'000, ts));
  feed(sim, add(kAapl, 2, 'S', 100, 10'010, ts));
}

// --- quoting -------------------------------------------------------------------------

TEST(SimulatorTest, JoinsBestBidAndAskAtTheBackOfTheQueue) {
  Simulator sim(config());
  open_market(sim);
  ASSERT_TRUE(sim.bid().has_value());
  ASSERT_TRUE(sim.ask().has_value());
  EXPECT_EQ(sim.bid()->price, 10'000);
  EXPECT_EQ(sim.ask()->price, 10'010);
  EXPECT_EQ(shares_ahead(sim.replay().book(kAapl), *sim.bid()), 100u);  // #1 is ahead
}

// No two-sided market yet: nothing to join.
TEST(SimulatorTest, NoQuotesWithOneSidedMarket) {
  Simulator sim(config());
  feed(sim, add(kAapl, 1, 'B', 100, 10'000));
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_FALSE(sim.ask().has_value());
}

// A better bid arrives: we move up to it, and land behind it (we lose our old place).
TEST(SimulatorTest, FollowsTheBestBidToTheBackOfTheNewQueue) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'001));
  ASSERT_TRUE(sim.bid().has_value());
  EXPECT_EQ(sim.bid()->price, 10'001);
  EXPECT_EQ(shares_ahead(sim.replay().book(kAapl), *sim.bid()), 100u);  // #3 is ahead
}

// Messages for other stocks are replayed but never move our quotes.
TEST(SimulatorTest, OtherStocksDontTouchOurQuotes) {
  Simulator sim(config());
  feed(sim, add(14, 1, 'B', 100, 10'000));
  feed(sim, add(14, 2, 'S', 100, 10'010));
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_EQ(sim.replay().book(14).order_count(), 2u);
}

TEST(SimulatorTest, NoQuotesOutsideTradingHours) {
  SimConfig c = config();
  c.start_ns = 5000;
  Simulator sim(c);
  open_market(sim, 1000);
  EXPECT_FALSE(sim.bid().has_value());
  feed(sim, add(kAapl, 3, 'B', 100, 9'999, 6000));  // any message after start
  EXPECT_TRUE(sim.bid().has_value());
}

// --- fills and accounting --------------------------------------------------------------

TEST(SimulatorTest, OrderBehindUsTradingFillsOurBid) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));  // behind us
  feed(sim, executed(kAapl, 1, 100));          // ahead of us: no fill
  EXPECT_TRUE(sim.fills().empty());
  feed(sim, executed(kAapl, 3, 60, 2000));  // behind us: the seller reached us first

  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_EQ(sim.fills()[0].side, Side::Buy);
  EXPECT_EQ(sim.fills()[0].price, 10'000);
  EXPECT_EQ(sim.fills()[0].quantity, 60u);
  EXPECT_EQ(sim.fills()[0].timestamp_ns, 2000u);
  EXPECT_EQ(sim.inventory(), 60);
}

// Buy 100 at 1.0000, sell 100 at 1.0010: flat, and up 100 x $0.0010 = $0.10.
TEST(SimulatorTest, BothSidesFilledCapturesTheSpread) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, add(kAapl, 4, 'S', 100, 10'010));
  feed(sim, executed(kAapl, 3, 100));
  feed(sim, executed(kAapl, 4, 100));

  ASSERT_EQ(sim.fills().size(), 2u);
  EXPECT_EQ(sim.inventory(), 0);
  EXPECT_EQ(sim.cash_ticks(), 100 * 10);  // (10'010 - 10'000) ticks x 100 shares
  EXPECT_DOUBLE_EQ(sim.pnl_dollars(), 0.10);
}

// Inventory is marked at the mid: long 100 bought at 1.0000, mid 1.0005 -> +$0.05.
TEST(SimulatorTest, OpenInventoryIsMarkedAtTheMid) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 3, 100));
  EXPECT_EQ(sim.inventory(), 100);
  EXPECT_DOUBLE_EQ(sim.pnl_dollars(), 0.05);
}

// At the limit, stop quoting the side that would add to the position.
TEST(SimulatorTest, InventoryLimitPullsTheBid) {
  SimConfig c = config();
  c.max_inventory = 100;
  Simulator sim(c);
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 3, 100));
  EXPECT_EQ(sim.inventory(), 100);
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_TRUE(sim.ask().has_value());
}

// After a full fill we requote at the back of the queue.
TEST(SimulatorTest, RequotesAfterAFullFill) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 3, 100));
  ASSERT_TRUE(sim.bid().has_value());
  EXPECT_EQ(sim.bid()->remaining, 100u);
  EXPECT_EQ(shares_ahead(sim.replay().book(kAapl), *sim.bid()), 100u);  // behind #1 again
}

// --- latency -------------------------------------------------------------------------

// The bid drops to 0.9999. We decide to follow it, but for 50 us our old bid at 1.0000
// is still live. A seller hitting 0.9999 in that window passes our price: picked off.
TEST(SimulatorTest, StaleQuoteIsPickedOffDuringLatency) {
  Simulator sim(config(50'000));
  open_market(sim, 1000);
  feed(sim, add(kAapl, 9, 'S', 100, 10'020, 60'000));  // time passes: quotes go live
  ASSERT_TRUE(sim.bid().has_value());

  feed(sim, deleted(kAapl, 1, 70'000));                  // best bid gone...
  feed(sim, add(kAapl, 4, 'B', 100, 9'999, 71'000));     // ...new best bid lower
  feed(sim, executed(kAapl, 4, 100, 80'000));            // seller hits 0.9999

  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_EQ(sim.fills()[0].price, 10'000);  // filled at our stale, higher bid
  EXPECT_EQ(sim.inventory(), 100);
}

// The same moves with no latency: we were already at 0.9999 behind #4, no fill.
TEST(SimulatorTest, NoLatencyNoPickOff) {
  Simulator sim(config(0));
  open_market(sim, 1000);
  feed(sim, deleted(kAapl, 1, 70'000));
  feed(sim, add(kAapl, 4, 'B', 100, 9'999, 71'000));
  feed(sim, executed(kAapl, 4, 100, 80'000));
  EXPECT_TRUE(sim.fills().empty());
}

// With latency, a new quote isn't live until it reaches the exchange.
TEST(SimulatorTest, NewQuoteWaitsForLatency) {
  Simulator sim(config(50'000));
  open_market(sim, 1000);
  EXPECT_FALSE(sim.bid().has_value());                   // sent at 1000, arrives at 51000
  feed(sim, add(kAapl, 9, 'S', 100, 10'020, 50'999));
  EXPECT_FALSE(sim.bid().has_value());
  feed(sim, add(kAapl, 10, 'S', 100, 10'020, 51'000));
  EXPECT_TRUE(sim.bid().has_value());
}

}  // namespace
}  // namespace ob::sim
