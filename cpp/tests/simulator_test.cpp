#include "orderbook/sim/simulator.hpp"

#include "orderbook/sim/pnl.hpp"

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

// After quoting stops, the book can thin out (after-hours) and its mid stop meaning
// anything. Inventory is valued at the mid when quoting ended, not at the last mid.
TEST(SimulatorTest, PnlIsMarkedAtTheEndOfQuoting) {
  SimConfig c = config();
  c.end_ns = 5000;
  Simulator sim(c);
  open_market(sim, 1000);                               // mid 1.0005
  feed(sim, add(kAapl, 3, 'B', 100, 10'000, 2000));
  feed(sim, executed(kAapl, 3, 100, 3000));             // long 100 at 1.0000
  feed(sim, deleted(kAapl, 2, 6000));                   // after the end: the ask goes...
  feed(sim, add(kAapl, 5, 'S', 100, 12'000, 7000));     // ...and a far ask makes mid 1.1000
  EXPECT_DOUBLE_EQ(sim.pnl_dollars(), 0.05);            // still marked at 1.0005
}

// The breakdown recomputes the P&L from the fills alone; it must agree with the
// simulator's own cash and inventory accounting.
TEST(SimulatorTest, PnlBreakdownAgreesWithTheSimulator) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 300, 10'000));
  feed(sim, executed(kAapl, 3, 300));               // long 100 at 1.0000
  feed(sim, add(kAapl, 4, 'B', 100, 10'003));       // the mid moves up
  feed(sim, add(kAapl, 5, 'S', 100, 10'010));
  feed(sim, executed(kAapl, 5, 100));               // sell 100 at 1.0010: flat
  feed(sim, add(kAapl, 6, 'B', 100, 10'003));
  feed(sim, executed(kAapl, 6, 100));               // long 100 again at 1.0003
  const PnlBreakdown b =
      pnl_breakdown(sim.fills(), sim.mids(), 1'000'000'000, config().end_ns, 0.0);
  ASSERT_EQ(sim.fills().size(), 3u);
  EXPECT_NEAR(b.total / 10'000.0, sim.pnl_dollars(), 1e-12);
  EXPECT_NEAR(b.spread + b.adverse_selection + b.inventory, b.total, 1e-9);
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

// --- what the book looked like when we were filled (D33) --------------------------------

// Bid side: #1 100 and #3 300 at 1.0000, ask #2 100. Touch share 400 / 500 = 0.8.
// #3 (behind us) executes 60: 340 shares stay at our price.
TEST(SimulatorTest, FillRecordsTheBookOnOurSide) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 300, 10'000));
  feed(sim, executed(kAapl, 3, 60));
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_DOUBLE_EQ(sim.fills()[0].touch_share, 0.8);
  EXPECT_EQ(sim.fills()[0].shares_left, 340u);
}

// The mid just before the fill (1.0005), not after it: a fill that breaks a level moves
// the mid at once, and that move belongs to adverse selection, not to the spread (D35).
TEST(SimulatorTest, FillRecordsTheMidBeforeIt) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 1, 100));  // ahead of us
  feed(sim, executed(kAapl, 3, 100));  // behind us: fills us and empties 1.0000
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_EQ(sim.fills()[0].mid_x2_before, 20'010);
}

// Ask side: #2 and #4 at 1.0010 (200), bid #1 100. Share 200 / 300; #4 fully executes.
TEST(SimulatorTest, AskFillLooksAtTheAskSide) {
  Simulator sim(config());
  open_market(sim);
  feed(sim, add(kAapl, 4, 'S', 100, 10'010));
  feed(sim, executed(kAapl, 4, 100));
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_DOUBLE_EQ(sim.fills()[0].touch_share, 200.0 / 300.0);
  EXPECT_EQ(sim.fills()[0].shares_left, 100u);
}

// Picked off through a stale quote: nobody real is left at our price.
TEST(SimulatorTest, TradeThroughLeavesNothingAtOurPrice) {
  Simulator sim(config(50'000));
  open_market(sim, 1000);
  feed(sim, add(kAapl, 9, 'S', 100, 10'020, 60'000));
  feed(sim, deleted(kAapl, 1, 70'000));
  feed(sim, add(kAapl, 4, 'B', 100, 9'999, 71'000));
  feed(sim, executed(kAapl, 4, 100, 80'000));
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_EQ(sim.fills()[0].price, 10'000);
  EXPECT_DOUBLE_EQ(sim.fills()[0].touch_share, 0.5);  // bid 100 @ 0.9999 vs ask 100
  EXPECT_EQ(sim.fills()[0].shares_left, 0u);
}

// --- messages ----------------------------------------------------------------------------

TEST(SimulatorTest, CountsEveryQuoteMessageSent) {
  Simulator sim(config());
  open_market(sim);
  EXPECT_EQ(sim.quote_messages(), 2u);                // join bid, join ask
  feed(sim, add(kAapl, 3, 'B', 100, 9'999));          // not the best bid: nothing to do
  EXPECT_EQ(sim.quote_messages(), 2u);
  feed(sim, add(kAapl, 4, 'B', 100, 10'001));         // better bid: move up
  EXPECT_EQ(sim.quote_messages(), 3u);
  feed(sim, deleted(kAapl, 2));                       // ask side gone: pull our ask
  EXPECT_EQ(sim.quote_messages(), 5u);                // pull ask + pull bid (one-sided)
}

// --- the mid series ------------------------------------------------------------------

TEST(SimulatorTest, RecordsTheMidOnlyWhenItChanges) {
  Simulator sim(config());
  open_market(sim, 1000);                               // mid 1.0005 (x2 = 20'010)
  feed(sim, add(kAapl, 3, 'B', 100, 10'000, 2000));     // same best prices: no new point
  feed(sim, add(kAapl, 4, 'B', 100, 10'002, 3000));     // better bid: mid 1.0006
  ASSERT_EQ(sim.mids().size(), 2u);
  EXPECT_EQ(sim.mids()[0].timestamp_ns, 1000u);
  EXPECT_EQ(sim.mids()[0].mid_x2, 20'010);
  EXPECT_EQ(sim.mids()[1].timestamp_ns, 3000u);
  EXPECT_EQ(sim.mids()[1].mid_x2, 20'012);
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

// --- the ladder (D60) ----------------------------------------------------------------

// Orders at the touch and levels - 1 ticks behind it. The tests' prices move by 1, so
// their tick is 1 (a real stock's is 100: $0.01).
SimConfig ladder(std::size_t levels) {
  SimConfig c = config();
  c.levels = levels;
  c.tick = 1;
  return c;
}

std::vector<Price> prices(const std::vector<VirtualOrder>& orders) {
  std::vector<Price> out;
  for (const VirtualOrder& o : orders) {
    out.push_back(o.price);
  }
  return out;
}

// Two levels: the best bid and one tick below it, the best ask and one tick above it.
TEST(LadderTest, JoinsTheTouchAndTheTickBehindIt) {
  Simulator sim(ladder(2));
  open_market(sim);
  EXPECT_EQ(prices(sim.bids()), (std::vector<Price>{10'000, 9'999}));
  EXPECT_EQ(prices(sim.asks()), (std::vector<Price>{10'010, 10'011}));
  EXPECT_EQ(sim.quote_messages(), 4u);
  EXPECT_EQ(sim.bid()->price, 10'000);  // bid(): the best of them
}

// The bid moves up a tick: a new order joins the new best bid, the one at the old best
// bid stays where it was in its queue (ahead of #3), and the one that fell out of the
// window is the one moved. One message, as for the naive strategy.
TEST(LadderTest, KeepsItsPlaceWhenTheBidMovesUp) {
  Simulator sim(ladder(2));
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));  // joins behind us
  feed(sim, add(kAapl, 4, 'B', 100, 10'001));  // better bid
  const std::vector<VirtualOrder> bids = sim.bids();
  ASSERT_EQ(prices(bids), (std::vector<Price>{10'001, 10'000}));
  EXPECT_EQ(shares_ahead(sim.replay().book(kAapl), bids[1]), 100u);  // only #1, not #3
  EXPECT_EQ(sim.quote_messages(), 5u);
}

// The best bid's level goes away: we were already waiting one tick below, ahead of #3,
// so #3 trading fills us. The naive strategy arrives behind #3 and gets nothing.
TEST(LadderTest, AlreadyInTheQueueWhenTheLevelBreaks) {
  for (const std::size_t levels : {std::size_t{2}, std::size_t{1}}) {
    Simulator sim(ladder(levels));
    open_market(sim);
    feed(sim, add(kAapl, 3, 'B', 100, 9'999));  // after our 9'999 order, if we have one
    feed(sim, deleted(kAapl, 1));               // best bid is now 9'999
    ASSERT_EQ(sim.bid()->price, 9'999);
    feed(sim, executed(kAapl, 3, 60, 2000));
    if (levels == 2) {
      EXPECT_EQ(prices(sim.bids()), (std::vector<Price>{9'999, 9'998}));
      ASSERT_EQ(sim.fills().size(), 1u);
      EXPECT_EQ(sim.fills()[0].price, 9'999);
      EXPECT_EQ(sim.fills()[0].quantity, 60u);
    } else {
      EXPECT_TRUE(sim.fills().empty());
    }
  }
}

// The limit counts every buy order as if it all filled: inventory 100 and a limit of 200
// leave room for one order, so the deepest one goes and the touch keeps its order.
TEST(LadderTest, DropsTheDeepestOrderFirstAtTheInventoryLimit) {
  SimConfig c = ladder(2);
  c.max_inventory = 200;
  Simulator sim(c);
  open_market(sim);
  ASSERT_EQ(sim.bids().size(), 2u);  // 0 + 2 x 100 <= 200
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 3, 100));  // behind our 10'000 order: we buy 100
  EXPECT_EQ(sim.inventory(), 100);
  EXPECT_EQ(prices(sim.bids()), (std::vector<Price>{10'000}));
  EXPECT_EQ(prices(sim.asks()), (std::vector<Price>{10'010, 10'011}));  // selling: room for 3
}

// A sell at 9'999 passes our 10'000 bid and reaches our 9'999 bid, but 60 shares fill
// 60 of ours, the better price first.
TEST(LadderTest, OneExecutionFillsAtMostItsOwnSize) {
  Simulator sim(ladder(2));
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 9'999));  // behind our 9'999 order
  feed(sim, executed(kAapl, 3, 60));
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_EQ(sim.fills()[0].price, 10'000);
  EXPECT_EQ(sim.fills()[0].quantity, 60u);
  EXPECT_EQ(sim.inventory(), 60);
}

// --- the strategy --------------------------------------------------------------------

// A strategy for tests: answers whatever `quotes` holds and remembers what it was shown.
class FixedStrategy final : public Strategy {
 public:
  Quotes quotes;
  std::optional<MarketView> last_view;
  int calls = 0;

  Quotes quote(const MarketView& view) override {
    last_view = view;
    ++calls;
    return quotes;
  }
};

TEST(StrategyTest, NaiveStrategyJoinsTheTouch) {
  NaiveStrategy naive;
  const Quotes q = naive.quote(MarketView{.now_ns = 0,
                                          .best_bid = 10'000,
                                          .best_ask = 10'010,
                                          .bid_size = 100,
                                          .ask_size = 100,
                                          .inventory = 0});
  EXPECT_EQ(q.bid, 10'000);
  EXPECT_EQ(q.ask, 10'010);
}

TEST(StrategyTest, SeesTheMarketAndOurInventory) {
  FixedStrategy s;
  s.quotes = {.bid = 10'000, .ask = 10'010};
  Simulator sim(config(), s);
  open_market(sim, 1000);
  feed(sim, add(kAapl, 3, 'B', 200, 10'000, 2000));  // behind us
  feed(sim, executed(kAapl, 3, 100, 3000));          // fills our bid: long 100

  ASSERT_TRUE(s.last_view.has_value());
  EXPECT_EQ(s.last_view->now_ns, 3000u);
  EXPECT_EQ(s.last_view->best_bid, 10'000);
  EXPECT_EQ(s.last_view->best_ask, 10'010);
  EXPECT_EQ(s.last_view->bid_size, 200u);  // #1 100 + #3's remaining 100; we're not in it
  EXPECT_EQ(s.last_view->ask_size, 100u);
  EXPECT_EQ(s.last_view->inventory, 100);
}

TEST(StrategyTest, CanQuoteBehindTheTouch) {
  FixedStrategy s;
  s.quotes = {.bid = 9'998, .ask = 10'012};
  Simulator sim(config(), s);
  open_market(sim);
  ASSERT_TRUE(sim.bid().has_value() && sim.ask().has_value());
  EXPECT_EQ(sim.bid()->price, 9'998);
  EXPECT_EQ(sim.ask()->price, 10'012);
}

// Inside the spread we'd be a price nobody in the real data reacted to (D30): the
// simulator moves such quotes back to the touch.
TEST(StrategyTest, NeverImprovesOnTheTouch) {
  FixedStrategy s;
  s.quotes = {.bid = 10'005, .ask = 10'005};
  Simulator sim(config(), s);
  open_market(sim);
  ASSERT_TRUE(sim.bid().has_value() && sim.ask().has_value());
  EXPECT_EQ(sim.bid()->price, 10'000);
  EXPECT_EQ(sim.ask()->price, 10'010);
}

TEST(StrategyTest, EmptySideMeansNoQuote) {
  FixedStrategy s;
  s.quotes = {.bid = std::nullopt, .ask = 10'010};
  Simulator sim(config(), s);
  open_market(sim);
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_TRUE(sim.ask().has_value());
}

TEST(StrategyTest, NonPositivePriceIsNotQuoted) {
  FixedStrategy s;
  s.quotes = {.bid = 0, .ask = 10'010};
  Simulator sim(config(), s);
  open_market(sim);
  EXPECT_FALSE(sim.bid().has_value());
}

// The inventory limit is the simulator's, not the strategy's to lift.
TEST(StrategyTest, InventoryLimitOverridesTheStrategy) {
  FixedStrategy s;
  s.quotes = {.bid = 10'000, .ask = 10'010};
  SimConfig c = config();
  c.max_inventory = 100;
  Simulator sim(c, s);
  open_market(sim);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000));
  feed(sim, executed(kAapl, 3, 100));
  EXPECT_EQ(sim.inventory(), 100);
  EXPECT_FALSE(sim.bid().has_value());
  EXPECT_TRUE(sim.ask().has_value());
}

// Before quoting starts the strategy is still asked (to warm up), but nothing is quoted.
TEST(StrategyTest, AskedBeforeHoursButNotQuoted) {
  FixedStrategy s;
  s.quotes = {.bid = 10'000, .ask = 10'010};
  SimConfig c = config();
  c.start_ns = 5000;
  Simulator sim(c, s);
  open_market(sim, 1000);
  EXPECT_GT(s.calls, 0);
  EXPECT_FALSE(sim.bid().has_value());
}

TEST(StrategyTest, NotAskedWithoutATwoSidedMarket) {
  FixedStrategy s;
  Simulator sim(config(), s);
  feed(sim, add(kAapl, 1, 'B', 100, 10'000));
  EXPECT_EQ(s.calls, 0);
}

// --- the charts the strategy sees (D44) ------------------------------------------------

// Remembers, at every call, the start of the last closed 1 s bar and the VWAP it saw.
class ChartWatcher final : public Strategy {
 public:
  std::vector<std::optional<std::uint64_t>> bar_start;
  std::vector<std::optional<double>> vwap;

  Quotes quote(const MarketView& view) override {
    const std::optional<ChartPoint>& last = view.market->one_second().latest();
    bar_start.push_back(last ? std::optional<std::uint64_t>(last->bar.start_ns) : std::nullopt);
    vwap.push_back(view.market->vwap().value());
    return {};
  }
};

constexpr std::uint64_t kMs = 1'000'000;

// Charts and VWAP from time 0, so the tests' small clocks are inside the window.
SimConfig charts_from_zero() {
  SimConfig c = config();
  c.market = MarketStateConfig{.from_ns = 0, .to_ns = 24ULL * 3600 * 1'000 * kMs, .vwap_from_ns = 0};
  return c;
}

// The [0 s, 1 s) bar shows up only on the first message at or after 1 s.
TEST(ChartsTest, StrategySeesABarOnlyOnceItHasClosed) {
  ChartWatcher s;
  Simulator sim(charts_from_zero(), s);
  open_market(sim, 300 * kMs);                          // call 1 (the ask makes it two-sided)
  feed(sim, add(kAapl, 3, 'B', 100, 9'990, 900 * kMs));  // call 2, still inside the second
  feed(sim, add(kAapl, 4, 'B', 100, 9'980, 1'100 * kMs));  // call 3, the bar has closed
  ASSERT_EQ(s.bar_start.size(), 3u);
  EXPECT_FALSE(s.bar_start[0]);
  EXPECT_FALSE(s.bar_start[1]);
  EXPECT_EQ(s.bar_start[2], 0u);
}

// A trade counts in the VWAP the strategy sees on that same message: it has happened.
TEST(ChartsTest, StrategySeesTheVwapOfTradesSoFar) {
  ChartWatcher s;
  Simulator sim(charts_from_zero(), s);
  open_market(sim, 300 * kMs);  // bid #1 100 @ 1.0000, ask #2 100 @ 1.0010
  feed(sim, executed(kAapl, 1, 40, 500 * kMs));  // 40 shares trade at the bid
  ASSERT_EQ(s.vwap.size(), 2u);
  EXPECT_FALSE(s.vwap[0]);
  EXPECT_DOUBLE_EQ(*s.vwap[1], 10'000.0);
}

// The default window is the real day's (09:00 on): a test clock near 0 has no charts.
TEST(ChartsTest, OutsideTheWindowThereAreNoCharts) {
  ChartWatcher s;
  Simulator sim(config(), s);
  open_market(sim, 300 * kMs);
  feed(sim, add(kAapl, 3, 'B', 100, 9'990, 5'000 * kMs));
  EXPECT_FALSE(s.bar_start.back());
  EXPECT_FALSE(sim.market().one_second().latest());
}

// A fill records the charts as the strategy could see them at that moment (6c-2a): the
// bars closed by the fill's own time, and the VWAP of the trades before it, not the
// trade that filled us.
TEST(ChartsTest, AFillRecordsTheChartsAtThatMoment) {
  Simulator sim(charts_from_zero());
  open_market(sim, 300 * kMs);                             // we join bid 1.0000 behind #1
  feed(sim, add(kAapl, 3, 'B', 100, 10'000, 400 * kMs));    // behind us
  feed(sim, executed(kAapl, 2, 40, 500 * kMs));             // 40 trade at the ask, 1.0010
  feed(sim, executed(kAapl, 3, 60, 1'200 * kMs));           // fills our bid, in second 2
  ASSERT_EQ(sim.fills().size(), 1u);
  const Fill& f = sim.fills()[0];
  ASSERT_TRUE(f.chart_1s);
  EXPECT_EQ(f.chart_1s->bar.start_ns, 0u);  // [0 s, 1 s), closed by this very message
  EXPECT_EQ(f.chart_1s->bar.volume, 40u);
  EXPECT_FALSE(f.chart_1m);                // the first minute hasn't closed
  ASSERT_TRUE(f.vwap);
  EXPECT_DOUBLE_EQ(*f.vwap, 10'010.0);     // only the earlier trade: ours would make it 10'004
}

// The 15 s chart too: a fill at 16 s sees the [0 s, 15 s) bar; no 5 min bar has closed.
TEST(ChartsTest, AFillRecordsTheFifteenSecondAndFiveMinuteCharts) {
  Simulator sim(charts_from_zero());
  open_market(sim, 300 * kMs);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000, 400 * kMs));
  feed(sim, executed(kAapl, 3, 60, 16'000 * kMs));
  ASSERT_EQ(sim.fills().size(), 1u);
  ASSERT_TRUE(sim.fills()[0].chart_15s);
  EXPECT_EQ(sim.fills()[0].chart_15s->bar.start_ns, 0u);
  EXPECT_FALSE(sim.fills()[0].chart_5m);
}

// Outside the chart window a fill has no snapshot.
TEST(ChartsTest, AFillOutsideTheWindowHasNoCharts) {
  Simulator sim(config());
  open_market(sim, 300 * kMs);
  feed(sim, add(kAapl, 3, 'B', 100, 10'000, 400 * kMs));
  feed(sim, executed(kAapl, 3, 60, 1'200 * kMs));
  ASSERT_EQ(sim.fills().size(), 1u);
  EXPECT_FALSE(sim.fills()[0].chart_1s);
  EXPECT_FALSE(sim.fills()[0].chart_1m);
  EXPECT_FALSE(sim.fills()[0].vwap);
}

}  // namespace
}  // namespace ob::sim
