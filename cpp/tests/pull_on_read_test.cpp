#include "orderbook/sim/pull_on_read.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kMinute = 60 * kSecond;

// The market state scenario of MarketStateTest.ReadsAFailedBreakout: a poke above the
// day's high at 2:05, back below in the next bar, so B1 reads down from 2:30 to 3:15.
MarketState trap() {
  MarketStateConfig c{.from_ns = 0, .to_ns = 10 * kMinute, .vwap_from_ns = 0};
  c.settings.structure.atr_n = 2;
  MarketState m(c);
  const std::pair<std::uint64_t, Price> mids[] = {
      {0, 1'004'000},           {20 * kSecond, 1'002'000},  {40 * kSecond, 1'004'000},
      {80 * kSecond, 1'002'000}, {100 * kSecond, 1'004'000}, {125 * kSecond, 1'004'200},
      {140 * kSecond, 1'003'500}, {151 * kSecond, 1'003'500}};
  for (const auto& [t, ticks] : mids) {  // structured binding: names for the pair's parts
    m.advance(t);
    m.on_mid(t, 2 * ticks);
  }
  return m;
}

MarketView at(std::uint64_t t, const MarketState& m) {
  return MarketView{.now_ns = t, .best_bid = 1'003'400, .best_ask = 1'003'600, .bid_size = 500,
                    .ask_size = 500, .inventory = 0, .market = &m};
}

// While B1 reads down, the bid is pulled; the ask stays (we're happy to sell into a fall).
TEST(PullOnReadTest, PullsTheBidWhileB1ReadsDown) {
  const MarketState m = trap();
  NaiveStrategy naive;
  PullOnRead pull(naive, TurnRead::B1, 0, 10 * kMinute);
  const Quotes q = pull.quote(at(150 * kSecond, m));
  EXPECT_FALSE(q.bid);
  EXPECT_EQ(q.ask, 1'003'600);
}

// Before the read and after its 45 s: both sides, as the inner strategy says.
TEST(PullOnReadTest, BothSidesWithoutARead) {
  const MarketState m = trap();
  NaiveStrategy naive;
  PullOnRead pull(naive, TurnRead::B1, 0, 10 * kMinute);
  for (const std::uint64_t t : {149 * kSecond, 195 * kSecond}) {
    const Quotes q = pull.quote(at(t, m));
    EXPECT_EQ(q.bid, 1'003'400) << t;
    EXPECT_EQ(q.ask, 1'003'600) << t;
  }
}

// Outside the window the read is ignored: here the window ends at 2:00.
TEST(PullOnReadTest, OnlyInsideTheWindow) {
  const MarketState m = trap();
  NaiveStrategy naive;
  PullOnRead pull(naive, TurnRead::B1, 0, 2 * kMinute);
  EXPECT_EQ(pull.quote(at(150 * kSecond, m)).bid, 1'003'400);
}

// B2 has no read here, so pulling on B2 changes nothing.
TEST(PullOnReadTest, EachReadOnItsOwn) {
  const MarketState m = trap();
  NaiveStrategy naive;
  PullOnRead pull(naive, TurnRead::B2, 0, 10 * kMinute);
  EXPECT_EQ(pull.quote(at(150 * kSecond, m)).bid, 1'003'400);
}

// No market state in the view (as in other strategies' tests): nothing to read, no change.
TEST(PullOnReadTest, NoMarketNoChange) {
  NaiveStrategy naive;
  PullOnRead pull(naive, TurnRead::B1, 0, 10 * kMinute);
  MarketView v{.now_ns = 0, .best_bid = 1, .best_ask = 2, .bid_size = 1, .ask_size = 1,
               .inventory = 0};
  EXPECT_EQ(pull.quote(v).bid, 1);
}

}  // namespace
}  // namespace ob::sim
